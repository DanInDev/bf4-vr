# Task brief: things let go of lie on the real ground (magazines, cases, the dropped gun)

Written 2026-11-13 for a fresh agent. Read `CLAUDE.md`, `docs/SAFETY.md` (binding), `docs/ARCHITECTURE.md`, and this
file. Issue #47 (thrown magazines; AK quick reload), and the physics part of #54/#35 (dropped guns).

## Goal

A magazine let go of, thrown or flicked out, a launcher's spent case and the player's own dropped gun behave as
objects in the world:
- they fly with the hand's speed and spin and fall under gravity;
- they bounce, slide, roll and tip onto a flat side on **the game's real ground** (terrain, floors, stairs,
  crates, cars), not on a flat floor height;
- they come to rest, never sink into the ground, and stay a while.

Done means the headset rows below are confirmed by the owner, with `tools\gate.ps1` green and the monitor checks
written down in `re/notes/`.

## Where it stands

- **2026-11-14, branch `physics/ray-ground`:** steps 2 and 3 written, gate green, **not yet run in the game**.
  - The ray query `0x140986CA0` and the client world `[0x142668F90]` were found statically (`re/notes/physics.md`).
  - `physics/ray_cast` casts the rays on the game thread behind a byte check; pipe `raycast`.
  - `physics/ground_cache` holds 4 cm columns (test `ground_cache`).
  - `physics/loose_body` is the rigid box (test `loose_body`).
  - The reload's discarded magazine uses the loose body in world space (test `reload` `test_hands_loose_on_ground`).
  - Next: the monitor check in `re/notes/physics.md`, then step 4 (the dropped gun).

- **Branch `weapon/magazine-flight` (this branch), #47 parts 1 and 2:** built, gate green, not merged, **never
  tried in the headset**. It is 20 h older than `main`.
  - `weapon/magazine_flight` (engine-free, unit test `magazine_flight`): a released magazine leaves with the hand's
    smoothed velocity, tumbles, falls and bounces softly on **the OpenXR STAGE floor**, then lies about 4 s.
    Without a known floor it drops away after 0.6 s as before.
  - The AK quick reload is two steps (`reload` event `flicked_out`, unit test `reload`).
  - Setting `magazine_flight`. Its headset row is GUN-19 (renumbered at the 2026-11-13 integration; #57 has GUN-16).
- **`main`** (since 2026-11-13 evening) has weapon pickup from the ground (`weapon/weapon_pickup`, #54) and the
  respawn fixes (`weapon/server_soldier`). Merge `main` into this branch first and keep the gate green.
- **The magazine** is drawn by the mod by moving the `Wep_Mag` bone (`body/arm_pose`, `magazine_pose`) while it is
  out of the gun: in the hand, falling, or parked 50 m below. A second magazine is an extra mesh (`stereo/extra_mesh`,
  #36). So a loose magazine is only drawn while its own gun is drawn (the same limit the BF3 mod had).
- **The dropped gun** (free hands, `weapon/free_hands`) lies at `weapon_points.floor_y`, the rig's AITrajectory
  height: a flat plane under the soldier, wrong on stairs and slopes.

## The reference: the BF3 single-player VR mod (`..\BF3-VR-code-and-notes-2026-11-13`)

**No licence: ideas only, never copy code.** It is a 32-bit BF3 mod, so its addresses mean nothing here. Its user
confirmed it in the headset ("it works really well", "the 40 mm casing shells do stay on the ground").

- **`code/src/loose.h` / `loose.cpp`**, pure rigid bodies with offline tests (`code/tools/loosetest.cpp`, 128 checks).
  - Starts: `Eject` (slides down the well with the gun, tips about the lower lip), `TipOut` (out of an open barrel
    when the muzzle is raised past ~30 degrees or the gun is jerked), `Release` / `ReleaseBy` (a hand's speed and
    turn over its last frames, plus the walk, but not the stick's turn), `Flick` (knocked off the gun).
  - Free motion: gravity, air, corners on the ground, bounce, friction, rolling, tipping onto a flat side, sleep at
    rest.
  - At most 10 big objects plus 192 casings.
  - Their user's rule: things lie **30 s** once at rest, then go only while nobody looks (never pop out in view);
    **guns lie until taken**.
- **`code/src/ground.h` / `ground.cpp`**, the game's own ray cast against its own collision.
  - It runs on the game's level-update thread, after the spatial lists are flushed and before the physics step,
    a few rays a frame. Each answer is cached for a 4 cm column; the soldier's feet are the fallback.
  - Every function touched is byte- and hash-checked first; any mismatch leaves it off for the session.
  - Floors are planes asked at each corner (stairs, edges); only surfaces facing up count; "nothing within 50 m"
    is answered as "falls".
- **`notes/physics-objects.md`, `notes/mag-removed-by-hand.md`, `notes/bf3-pouch-and-casings.md`:**
  - **Throws:** read the controllers' own velocity (`XrSpaceVelocity`); a tracking-jump filter had eaten hard throws.
  - **Landing sounds** come from the game's own sound files.
  - Their user did not want casings ("we might not implement that at all").

## What to do, in order

1. **Merge `main` into this branch**, renumber the headset row, gate green. Ask the owner for a short headset check
   of #47 as it is (it was never tried).
2. **Find BF4's physics ray cast (research, REA/Ghidra on the dump, `docs/TOOLING.md`).**
   - BF4 runs Havok; the type registry has `HavokPhysicsData`, but no type named like "RayCast". Look for the
     cast the game uses for its own dropped weapons (the server side places pickups on the ground) and for the
     client's.
   - Find a safe thread and moment to call it (or answer from the game's own casts).
   - Verify the bytes before any hook (CLAUDE.md), and write every address in `re/symbols/bf4-179547.md` with its
     evidence in a new `re/notes/physics.md`.
   - The world positions you need are in `ClientPickupEntity [+0x140]+0x30` (a dropped gun lying on the ground,
     useful to check a cast against) and the rig's `weapon_points.world_origin` / `world_to_local` (world to
     OpenXR LOCAL, added for #54).
3. **An engine-free loose-body module** (the BF3 idea, our code): starts as above, a ground given as planes under
   points, bounce, friction, rest, a cap on how many. Unit-test it under `tests/` as `loosetest` did. Then replace
   `magazine_flight`'s STAGE floor with the ray-cast ground, falling back to the STAGE floor, then `floor_y`.
4. **The dropped gun on the real ground** (free hands), lying until taken, with the game's own drop sound.
5. **Later, only if the owner wants them:**
   - drawing a loose magazine while another gun is out (an extra-mesh capture, as the grenade and knife have);
   - the M320 / rocket spent cases;
   - casings.

## Rules that apply

- `tools\gate.ps1` green before calling anything done; state what was not checked.
- Every new behaviour gets a live setting (`src/common/settings_store.cpp`), a panel row
  (`src/common/setting_catalog.cpp`) and a line in `tools/bfvr.ini.example`.
- Engine-free logic gets a unit test.
- One branch per task from `main`, merged only when the owner asks; never push unasked.
- **Don't change your main checkout or the sights work:**
  - The owner's main checkout (`C:\Users\dnlni\Desktop\hobby\bf-vr`) is on a headset test build; work in a worktree.
  - Do not touch `fix/sights-in-frame` or the `test/headset-*` branches.
- **Testing without a headset:**
  - `pipe fakehead 0.064`; `fakehand` positions are in OpenXR LOCAL, turned against the game's camera by the body's
    yaw, so place a hand at three points and read `arms` `goal=` (world) to find the axes.
  - Walk with `pipe pad`.
  - Turn by writing the aimer yaw (`re/symbols`: aimer `+0x14` and `+0x60`).
- **Live game:**
  - No tools while Battlefield 6 / EasyAntiCheat games run.
  - Ask before taking BF4's focus.
  - **Restarting BF4 loses the owner's unsaved progress** (dropped weapons, killed enemies): ask first.
- Approved behaviour must not change unasked: hip accuracy, shots from the barrel, weapon pickup, the respawn fixes.
- The owner wants **short headset lists** (5-10 broad checks), with a mark (both stick clicks) at each.

## Headset checks to add (`docs/HEADSET-TESTS.md`, new IDs)

1. A magazine tossed sideways flies, tumbles and lands on the floor (not through it), then lies a while.
2. The same on stairs, a crate or a slope: it lands on what is there.
3. The AK quick reload: the old magazine is knocked out first, then the new one seats.
4. The dropped gun lands on the real ground (stairs, a crate), lies until taken, and is taken back by gripping it.
