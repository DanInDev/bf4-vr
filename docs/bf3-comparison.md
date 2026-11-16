# BF3 VR mod vs. BF4-VR: what they have that we don't

Written 2026-11-13 from the BF3 campaign mod's shared code and notes (local copy at commit `07e1bbb`;
the GitHub repo `criminalvictory/Battlefield-3-VR-The-Great-Destroyer-Source` is 5 commits newer: the
EoTech reticle, a recoil-drift fix, aim for picked-up guns). The repo has no licence file, but its maintainer
allows copying its code and setup (owner, 2026-11-16): reuse it with credit. Overlord stays design-only (GPLv3).

## Summary

- **Different strengths.** BF3 goes deep on one level at a time: every gun has its own fitted hands,
  reload steps, recoil and sounds, and the world has real physics. BF4-VR goes wide: the whole campaign is
  playable, with a launcher, an in-VR settings panel, remapping, body calibration and HUD on the body.
- **What we're missing most:** magazines and dropped items landing on the real level geometry, per-gun
  reload mechanics (levers, belts, lids, launcher flaps), hands that collide with the gun, harsh
  one-handed recoil, and scopes drawn on the game's own glass.
- **Easy wins:** they found the BF3 ray cast takes any two points, which opens up "the gun stops at walls".
  The ideas are the same in BF4; the addresses are not.

## How the two projects differ

| | BF3 mod | BF4-VR |
|---|---|---|
| Binary | 32-bit `bf3.exe`, injector | 64-bit BF4, `dinput8.dll` proxy + launcher |
| Stereo | The game's split-screen test path: 2 native views | Both eyes from one game frame (our eye pass) |
| Hands / gun | Re-skins the game's own arms and gun draws per gun (bone tables, fitted poses per gun) | Arm IK + authored grips, engine-free, with tests |
| Scope | Second WorldRenderer at 720², drawn on the scope's own glass mesh | Third world render; the AK-12 lens is still black (#39) |
| Physics | Rigid bodies on the **game's own ray cast** (real ground) | Fake flight onto the OpenXR floor (#47, branch not merged) |
| Sounds | Waves decoded from the game's sound banks, cut per gun and per step | The game's own reload sound entity, fired by event (#30) |
| Settings | Text files in `build\` | Live settings, VR panel, launcher, remapping |
| Scope of play | One mission played through in depth (Rock and a Hard Place), parts of others | The whole campaign playable |
| Verification | Films, an offline self-test, the user in the headset | `gate.ps1`, unit tests, pipe checks, HEADSET-TESTS |

## Cool features they have and we don't

Ranked roughly by how much they'd add to BF4-VR.

1. **Magazines and dropped things on the real ground.** The game's own ray cast (any two points) puts
   magazines, launcher cases and guns on the real terrain. Thrown items carry the controller's own sensed
   velocity and spin. They lie for 30 s, then vanish only when nobody is looking. Ours: a flat STAGE floor,
   about 4 s.
2. **Per-gun reload mechanics.** Over- and under-hand lever pulls. The M240's lid opens, the box comes
   out, then you pinch the belt and lay it into the feed. The Stinger's aerial folds and springs open. The
   M320 is loaded by hand with a 40 mm round. The SMAW's case and rocket are handled by hand. The M4 family
   has a slap on the bolt release and an ejection-port cover on its hinge. The AN-94 is the base for most
   guns: magnet at 10 cm, flick by force. Ours: magazine plus rack for the AR and AK family, AK flick (#47).
3. **Hands collide with the gun.** Each gun has a distance field built from its mesh, and the glove is a
   set of spheres pushed out of it, so the hand never clips the gun. The bolt-release slap is detected by
   real contact and speed. Fingers close one by one until they touch the gun. Ours: contact grips on a
   magazine box only.
4. **One-handed recoil that punishes.** One hand on a rifle gives about 5x the climb, 2.5x the push back
   and hip-fire scatter: near useless, on purpose. The game's own camera climb is removed from the gun data
   so bullets follow the drawn gun. Ours: each gun's kick from its `GunSwayData`, 0.4x when two-handed,
   no one-hand penalty.
5. **Scopes on the game's glass.** The lens shader rebuilds each eye's ray and divides its slope by the
   magnification. The field stop goes black at the edge like a real scope, and the reticle is sampled the
   same way so it stays on the target. Their user called it very sharp. Already written up for us on
   #39.
6. **Muzzle flash, smoke and casings at the drawn barrel.** The game's own particles are moved on the GPU
   (compute shader) to where the drawn gun is. We have not checked this on our side.
7. **Grabbing the magazine by hand.** On guns with no release button (AK, G3, SVD and others) you grab the
   magazine and pull it out. The user called the Afghan flick "crucial". We have the flick; the plain pull
   out by hand needs checking against our AK path.
8. **The game never reloads a held gun by itself.** A reload the game starts is undone, so only manual
   reloads exist. A gun you pick up gets its own reload, recoil and haptics.
9. **6DoF walking and real crouching under the head.** Walk in the room and the soldier follows; really
   crouch and the soldier crouches with the eyes kept in place. We have lean and peek, plus physical
   stance (#33).
10. **Grenades: catch between hands.** Toss a grenade from one hand and catch it with the other. Designed
    there, not built. We have the pin pull.
11. **Smaller items:**
    - Untracked hands are hidden.
    - Ammo from over the left shoulder for every gun.
    - A dry click on empty, plus gear and handle sounds (designed).
    - A stream view for OBS and Discord (one steadied eye, 16:9).
    - Bodies stay 10 minutes.
    - Orange enemy markers removed.
    - A fix for the game's 11 ms-per-frame `GetPerformanceInfo` stall. Worth checking whether BF4 does
      the same.

**Designed there but not built** (useful as a reference when we get there):
- The gun stops at walls and rests on cover; hands stop on walls but **never stick while moving** (their
  user's hard rule).
- Shooting round corners.
- Knife stab, ladders hand over hand, mounted guns with both hands, tank and jet back seat.

## What we have that they don't

- **A whole playable campaign**, plus a launcher with install, update and remove, and a game build check.
- **A live settings registry**: the in-VR panel, remapping and presets, all with matching ini lines.
- **Body:** T-pose calibration, a sling that keeps the primary drawn, holsters with grab zones, and a
  body that turns by threshold.
- **HUD:**
  - Separate layers.
  - The minimap on the wrist by a watch glance.
  - A mag check (tilt the gun to see ammo).
  - A laser pointer for the menus.
- **Use by reaching and melee by gesture.** BF3 keeps use by reaching as an experiment.
- **Quest 3 first.** BF3 is tuned on the Steam Frame.
- **Engine-free logic with unit tests and a gate**, which BF3's film-based checks don't give.

## Plan

The order is a proposal for you to change. Each step goes on its own branch. Every new feature gets the
usual setting, panel row, ini line and unit test, ends with `gate.ps1` green, and gets a row in
`HEADSET-TESTS.md`. Headset checks are batched (A, B, C), so you get three short sessions instead of one
per feature.

**Step 0: clear what's in flight first.** Check the `test/headset-2026-11-13` build in the headset, then
merge it together with `weapon/magazine-flight` (#47) and `fix/sights-headset` (#39). Steps 2 and 4 build
on those branches.

| # | Step | What it takes | How we know it works | Size |
|---|---|---|---|---|
| 1 | **One-handed recoil penalty** (#60) | A rifle, LMG or shotgun fired one-handed gets several times the climb and push back, plus hip-fire scatter. Pistols stay mild. Before that, check whether the game's own camera climb still pulls shots away from the drawn barrel. | Unit test on the gun's offsets per stance. On the monitor, a one-handed burst with `fakehand` drifts and a two-handed one doesn't. | S |
| 2 | **The game's ray cast in BF4** (#59) | Use REA to find the physics ray query (callers of the soldier's ground check, the `RayCastLength` and `RaycastWithDetailMesh` data). Verify its bytes, record it in `re/symbols`, and add a pipe `raycast` command. | `pipe raycast` from the head straight down reports the floor at about the eye height. Towards a wall, the distance matches what's in the picture. | M |
| 2b | **Magazines land on the real level** (#61) | Replace the STAGE floor with the ray cast: a magazine bounces once and lies about 30 s, then goes only when you aren't looking. | Unit test for the lie and vanish rule. On the monitor, a magazine dropped on stairs or a car roof stays on it. | S |
| 3 | **Hands collide with the gun** (#62) | Boxes per gun (receiver, handguard, magazine well) from our existing contact code. The free hand is pushed out of them, never into the gun. The bolt-release slap only counts on real contact and speed. A distance field built from the mesh only if boxes aren't enough. | Unit test: a hand swept through the receiver stays outside. On the monitor, `pipe fingers` shows contact at 0 mm with no penetration. | M |
| **A** | **Headset batch A** | Recoil one and two-handed, magazines on stairs, the hand on the gun. | Your verdict | |
| 4 | **Scopes on the glass** (#39) | The lens drawn as the game's own glass draw with our shader: each eye's ray, the slope divided by the magnification, a field stop, and the reticle sampled the same way. | `eyeshot` pictures show the reticle on the target with the head off-axis, and black past the field stop. | L |
| 5 | **The gun stops at walls** (#63) | Uses step 2. Only the drawn gun gives way. It never holds the player back, and it lets go as soon as you move or the drawn hand is too far off the real one. Shots keep coming from the near side of the wall. | Unit test for the let-go rule. On the monitor, walking along a wall with the gun poked in shows no stuck frames. | M |
| **B** | **Headset batch B** | Scopes and walls. | Your verdict | |
| 6 | **Per-gun mechanics, one gun at a time** (#64) | Start with what the current checkpoints carry. LMG: lid, box and belt. Launchers: case and rocket by hand. Bolt-action and pump guns. Levers pulled over and under. Each gun gets its own profile row; the game's own reload animation is the reference. | Per gun: a unit test of its reload steps, and a monitor run through the steps with `fakehand`. | L, ongoing |
| 7 | **Small checks** (#65), a few tool calls each | (a) Muzzle flash and smoke: do they come from the drawn barrel? (b) Does BF4 make the 11 ms `GetPerformanceInfo` call every frame? (c) Does the game reload a held gun by itself? (d) Hide a hand that loses tracking. | Each one either becomes a small fix or is noted in `re/notes` as not needed. | S |
| **C** | **Headset batch C** | Each gun as it lands, plus whatever step 7 changed. | Your verdict | |

**Why this order:**
- Step 1 is the quickest feel win.
- Step 2 is the foundation: one RE session unlocks both 2b and 5.
- Step 3 fixes the clipping during reloads that you notice most.
- Step 4 is already high priority, and the method is now known.
- Step 6 is open-ended, so it goes last and runs gun by gun.
- Step 7 can run alongside anything else.

**Left out for now:** catching a grenade between hands (wait for the held grenade from #36 to settle),
knife stab, ladders, vehicles. BF3's stream view now has a BF4 counterpart (`stream_view`, 2026-11-15:
same idea, the left eye re-aimed at a steadied level camera, drawn into the game window; docs/TOOLING.md "Recording").
