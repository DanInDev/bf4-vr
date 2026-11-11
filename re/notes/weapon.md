# Weapons, ammo and physical reload — BF4 build 179547

Status: **ammo mapped and written (2026-10-26)**; physical reload (#14) built on it
(`src/weapon/`), monitor-tested, not headset-tested.

## Where the ammo lives

```
client  [0x14230F260] soldier +0x570 weapons component -> current slot (aim.md) -> ClientSoldierWeapon
          +0x4850 unlock asset, +0x20 -> "U_AK12"                      weapon name for the reload profile
          +0x30 SoldierWeaponData +0x90 WeaponFiringData +0x10 FiringFunctionData
                +0x220 Ammo.MagazineCapacity (31 for the 30-round AK-12: counts the chamber)
          +0x49C0 / +0x49C8 -> two ammo blocks, magazine +0x1A0, reserve +0x1A4   (copies)
server  [0x142311DB8] ServerSoldierEntity +0x7C0 ServerSoldierWeaponsComponent
          +0x828 + 8 * slot -> ServerSoldierWeapon (same slot index as the client)
          +0x200 -> weapon firing object: +0x1A0 int32 rounds in the gun, +0x1A4 reserve   (authoritative)
```

- Found with `tools/ammo_probe.py` and a value search for the HUD's 31/124 across the client weapon
  and two levels of pointers: three pairs, one inside `ServerSoldierWeapon` (typed by its vtable),
  two in untyped blocks behind the client weapon.
- Correction (same day): the server pair is not a fixed weapon field. Both sides keep the ammo in a
  firing object at `+0x1A0` (server: weapon `+0x200`; client: weapon `+0x49C0`/`+0x49C8`). The
  AK-12's server firing object is embedded at weapon `+0x2A0`, which made `+0x440` look like a field;
  the SCAR-H's is allocated separately and `+0x440` reads garbage there.
- The single-player server runs in the game process (209 `ServerSoldierEntity` objects in the
  global scan, typeinfo.md), and it owns the ammo.

## Tests (campaign checkpoint "Get Grenade Launcher", AK-12 in hand, SCAR-H in slot 0)

| Test | Result |
| --- | --- |
| Write 5 into one client block | back to 31 within 1.5 s |
| Write 5 into the server weapon | both client blocks and the HUD show 5 within 1.5 s; persists |
| Write 0 into the server weapon | the game reloads by itself within ~4 s: 30/94 (no chambered round) |
| Write 1 | stays 1 (no reload); the HUD shows "RELOAD" |
| `bone 131 0 0.3 0` (research) | the magazine mesh moves 0.3 m off the gun on its own |

## Consequences for the physical reload

- Write only the server weapon; read shots back from it (the game counts down from what we wrote).
- Never let the game's magazine reach 0: it would start its own reload. The mod writes the rounds
  in the gun plus 2 spare rounds and holds the trigger back while the chamber is empty, so the HUD
  ammo counter reads 2 high in physical mode (`hud_show_ammo = 0`, or a wrist counter later).
- The game's reload button (pad X) is taken: AR magazine release / bolt catch.
- `Wep_Mag` is drawn by the mod while the magazine is out (in the hand, falling, or 50 m below).

## Pad D-pad and accessories (2026-10-26, AK-12 with RX01 and flip-to-side magnifier)

| Pad input | Effect |
| --- | --- |
| D-pad up | toggle accessory: the magnifier flips behind the red dot and back (lights, lasers, variable zoom share it) |
| D-pad down / left | fire mode and similar (pressed twice: back to AUTO) |
| D-pad right | weapon / attachment switch |

Gun bones relative to `Wep_Root` (its axes, +z along the barrel, metres): `Wep_Trigger` (0, -0.007, 0.321),
`Wep_Mag` (0, -0.002, 0.404), `Wep_Slide` (-0.009, 0.037, 0.497), `Wep_Scope1` (0, 0.062, 0.286),
`Wep_Extra1` (-0.024, 0.015, 0.641), `IK_Joint_LeftHand` (0.057, -0.062, 0.506), `IK_Joint_RightHand`
(-0.021, -0.070, 0.203), `Wep_Grenade1` (0.002, -0.105, 0.552), muzzle-side `Wep_Belt*` from 0.67.
The accessory touch uses `Wep_Scope1` and `Wep_Extra1`; `IK_Joint_LeftHand` is the handguard spot for a
future two-hand grip.

## Grenades (2026-10-26, `U_M67_SP` in slot 6; tools/grenade_probe.py, docs/tasks/grenades.md)

- **Pad LB throws** (quick throw: the grenade is equipped, wind-up, release; HUD "1/2 SINGLE").
- Count: firing object `+0x1A0` = (1, 2) (one ready + two spare, HUD "3"); it did not drop after
  throws in this checkpoint (the campaign seems to refill).
- Data (`FiringFunctionData` of the grenade weapon, `+0x60` Shot): InitialPosition (-0.2, 0, 0),
  InitialDirection (0, 0, 1), InitialSpeed (0, 3, 18), SpawnDelay 0.39, ForceSpawnToCamera 1;
  `+0x168` HoldAndRelease: Delay 0.5; projectile data: TimeToLive 3.0 (fuse), Gravity -9.8.
- Live grenade: `ServerGrenadeEntity` (vtable `0x141E685B0`, pooled and reused), reachable as
  [[server grenade weapon +0x8] +0x348]; world position at `+0x3F0` (and `+0x430`).
- **Writes before the press steer the throw** (measured launch from the server grenade's track):

| Write | Spawn after the press | Effect |
| --- | --- | --- |
| none | 0.97 s | 17 m/s, 0.6 m in front of the camera |
| InitialSpeed (0, 3, 9) | 0.94 s | forward speed halved |
| SpawnDelay 0 | 0.58 s | |
| SpawnDelay 0 + HoldAndRelease.Delay 0 | **0.12 s** (pipe latency included) | instant throw |
| InitialDirection (1, 0, 0) | | turned 90 deg to the left |
| InitialPosition (any) with ForceSpawnToCamera 1 | | ignored |
| ForceSpawnToCamera 0 + InitialPosition (0.3, -0.3, 0.2) | | spawn moved 0.3 left, 0.3 down, 0.2 forward |

- **Frame**: origin at the first-person camera, axes x left, y up, z forward, **pitched with the aim**
  (16.7 deg aim pitch raised the launch by 17.5 deg). OpenXR-style camera axes (x right, -z forward)
  convert by flipping x and z.
- Sharing: none of the 4 server soldiers in this checkpoint carries the player's grenade data (the
  player has the `_SP` variant); still restore the data right after the spawn.
- Lesson: a grenade bouncing off the ceiling indoors killed the player (checkpoint reload).

## Weapon slots and switching (2026-10-26, tools/slot_probe.py, docs/tasks/weapon-swap.md)

Slots in this checkpoint: 0 `U_SCAR-H`, 1 `U_AK12`, 6 `U_M67_SP`, 7 `U_Knife`, 8 `U_SP_Visor`; 2-5 empty.

| Input | Slot after | Time |
| --- | --- | --- |
| pad Y | 0 <-> 1 (next gun) | 0.15-0.18 s |
| pad right stick click | 7 (knife) during the stab, then back | 0.02 s |
| pad A, B, X, left stick click, D-pad left/right/down | no change | |
| key 1 / 2 | 0 / 1 | < 0.25 s |
| key 3 / 4 | no change (no gadgets in this kit; gadget slots expected) | |
| key 5 | 6 (grenade in hand) | < 0.25 s |
| key 6 | 7 (knife, stays) | < 0.25 s |
| key F | 7 during the stab, then back | |
| keys 7, 8, E, Q, T, V | no change (the visor's input is still unknown) | |

2026-10-27, second probe for the visor (slot 8), each press watched 1.5 s after release, nothing but
pad Y (0 <-> 1) changed the slot: pad Back, D-pad up/left/right/down, RB, left stick click, Y (1.2 s
holds each), Back and D-pad up taps; keys 7 and 8 held 1.2 s, 9, 0, B, Z, X, C, F1, F2, Q and T held.
LB (quick grenade) left out. The visor is most likely granted only by mission scripts; this checkpoint
has no player-selectable gadget, so gadget hand poses (their Wep_Root frames, like the knife's in
skeleton.md) need a checkpoint whose kit has one.

## Gadgets from the stash (2026-10-27, same checkpoint)

- The gadget stash is the green crate stack with "[E] OPEN GADGET STASH" next to the start; the prompt
  shows when facing it, E opens its menu (pad: the interact button). The menu (Gadget One / Gadget
  Two, items, DONE) takes clicks through the mod's pointer (pipe `pointer u v 0|1`).
- Picked C4 + M320 HE: slots 2 `U_SP_C4`, 5 `U_SP_M320_HE`; holster keys 3 / 4 draw them.
- **C4**: the gun hand holds the detonator. The aim button (left trigger) makes the off hand throw a
  pack; fire (right trigger) sets them off (monitor: LT = pack in flight, RT = explosion 3 m ahead).
  With `ads_trigger` off the left trigger never reached the game, so the C4 could not be thrown:
  `c4_trigger` (default on) lets it through while `_C4` is held.
- Hand poses under a fake controller: the detonator's animated hold is already a natural
  remote-in-hand pose and turns with the hand (unlike the knife). The M320's Wep_Root is gun-like:
  (0,-1,0,0) ahead, follows pitch/yaw exactly (up 60 -> (0,-0.868,-0.496,0)). No per-item fix needed.

- **The keyboard is Raw Input**: the image imports `GetRawInputData` / `RegisterRawInputDevices`; no
  DirectInput keyboard. Synthetic keys work without focus: `hooks::press_game_key` posts `WM_INPUT`
  with a marker handle (`0x7BF5A000`+n) and the `GetRawInputData` hook answers it (pipe `key <vk>`).

## Recoil data (2026-11-03, tools/recoil_probe.py, campaign checkpoint "Get Grenade Launcher")

```
WeaponFiringData +0x30 WeaponSway -> GunSwayData (0x450)
  Stand  +0x10   GunSwayStandData:       NoZoom +0x00, Zoom +0xBC (GunSwayBaseMoveJumpData, Recoil +0x90)
  Crouch +0x188, Prone +0x240 GunSwayCrouchProneData: NoZoom +0x00, Zoom +0x5C (GunSwayBaseMoveData, Recoil +0x30)
  +0x440 ShootingRecoilDecreaseScale, +0x444 FirstShotRecoilMultiplier, +0x448 CameraRecoil* (spring k, damping, min)
GunSwayRecoilData (0x18): RecoilAmplitudeMax, RecoilAmplitudeIncPerShot, HorizontalRecoilAmplitudeIncPerShotMin,
  HorizontalRecoilAmplitudeIncPerShotMax, HorizontalRecoilAmplitudeMax, RecoilAmplitudeDecreaseFactor
FiringFunctionData +0x168 FireLogic: +0x2C RecoilData (angle ranges), +0x60 RateOfFire
```

| Gun | Up per shot | Sideways per shot | Max (up / side) | Decrease | First shot | RoF | FireLogic.Recoil |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `U_AK12` | 0.30 | -0.12 .. 0.17 | 10 / 10 | 18 | x2.1 | 650 | all 0 |
| `U_SCAR-H` | 0.50 | -0.225 .. 0.225 | 10 / 10 | 12 | x1.75 | 620 | all 0 |

- All six blocks (stand, crouch, prone; hip, zoom) hold the same values for both guns: BF4's recoil does
  not depend on stance or zoom here (stance changes spread, not recoil). Grenade, knife and visor have
  no GunSwayData (null). Both guns: ShootingRecoilDecreaseScale 0.1, CameraRecoil spring 250 / 0.35.
- Units: the game's camera kick per shot, presumably degrees (Frostbite's convention; see the burst
  measurement below).
- The engine reads no scalar at GunSwayData +0x440/+0x444 directly (byte scan of `.text` for
  `[reg+0x444]`): the sway runtime copies the data. The aiming simulation adds its sway/recoil offset
  (aiming +0x28/+0x2C) after `0x140FA6640`, which writes it; not followed further.
- The mod's drawn-gun kick (`body/gun_recoil`) uses the hip values of the current stance relative to the
  AK-12's: the SCAR-H kicks 1.67x as far and settles 1.5x slower (18 / 12).
- Burst measurement (1.2 s, pad RT, `aim = controller`, fakehead + fakehand): the game's sway/recoil
  offset at aiming +0x28/+0x2C stayed exactly 0 for both guns, so with controller aim the game adds no
  camera recoil here; the units above stay unmeasured. Drawn kick: AK-12 first shot 5.1 deg (x2.1),
  peak 7.7 deg, sideways up to 1.5 deg (mostly right); SCAR-H peak 14.9 deg (cap 15), sideways
  +-1.3 deg, settle time constant 0.32 s (expected 0.33).

## Charging handles from the game's own reload (2026-11-03)

Method: `arm_ik = 0`, `reload = button`, write 0 into the server magazine (the game then does a full empty
reload), and sample the first-person local pose at ~100 Hz (forward kinematics in Python): the left
hand's Thumb4, Index2/Index3 and Wep_Slide in Wep_Root axes (+z along the barrel, +y up, **+x left**).

| Gun | What the reload does | Handle (Wep_Root axes) |
| --- | --- | --- |
| `U_AK12` | At 2.4-2.9 s the hand pinches (thumb to index side 3.0-3.3 cm) and pulls; Wep_Slide travels **10 cm** back | Pinch as the slide starts (-0.027, 0.062, 0.520) at 0.8 cm travel; at the peak minus the travel (-0.015, 0.060, 0.534): **(-0.02, 0.06, 0.53)**, right side ahead of the magwell. The old AK guess (0, 0.03, 0.40) was 13 cm back |
| `U_SCAR-H` | The bolt locks back on empty (Wep_Slide 12.4 cm); ~1.8 s later the open hand (thumb-index 12 cm) hits the bolt release left of the magwell, (0.02, 0.0, 0.46), and the bolt slams home | Not shown by the animation. The headset (2026-11-03) puts the real SCAR's handle front-left of the receiver: estimate **(0.035, 0.04, 0.57)**, ahead of Wep_Slide (0, 0.058, 0.505); to be checked against the drawn mesh |

- The empty AK reload ends with 30 in the gun, 30 taken from the reserve (as a real reload).
- Wep_Slide does animate on the AK-12 although offsetting it moves no visible mesh (A2): the bolt may be
  welded to the receiver mesh in first person.

## Sights and aiming down sights (2026-11-04, monitor: fakehead + fakehand, tools/sight_probe.py)

The game's first-person animation keeps the **CameraJoint on the gun's sight**: in its own aim down
sights (LT held, the zoom animation settled) the camera in Wep_Root axes is the eye point behind the
sight and the sight's line. At the hip the same relation is the rifle pose's cant and moves with the
game's aim pitch and the firing animation, so it is no sight line.

| Gun (optic) | ADS eye point from Wep_Scope1 (Wep_Root axes, +x left, +y up, +z barrel) | Sight vs barrel in ADS | Hip pose camera |
| --- | --- | --- | --- |
| `U_AK12` (red dot + flip magnifier) | (0.001, 0.035, -0.159) | 0.0 deg (0.03 up, 0.01 left) | 7.6 cm to the left, 5 cm up, 1.4 deg right of the barrel; after firing +-13 deg |
| `U_SCAR-H` (holographic) | (0.001, 0.036, -0.141) | 0.25 deg down | (0.075, 0.045, -0.202) from Scope1 |

- **Lenses**: the AK's red dot renders **opaque black** outside the game's ADS and **clear** in it
  (front post visible through it); the SCAR's holographic window is clear either way. No red dot is
  drawn in the lens on the monitor (red wall, maybe invisible); the mod's reticle (an XR layer on the
  sight line) stands in for it.
- **Gun pass FOV**: 45 deg at the hip, **17 deg in ADS** (the narrowest per-view constant block of the
  frame; the first-person shadow view also uses 45, the world its own). The life-size scale must use
  it, else the gun is drawn 2.7 times too large while aiming. The world FOV is the mod's and does not zoom.
- **Shots**: with the eye on the sight (ADS sample), five single AK shots at a wall ~2.5 m away made one
  hole, 2 px left and 20 px (~4 cm) below the view centre: a constant offset of the bullet origin, not
  an angle (it stays ~4 cm at any range).
- Mod: `body/arm_pose` samples the sight (steady only: not within 0.4 s of firing, within 5 deg of
  the barrel), prefers the ADS sample per gun (reset when the gun in hand changes), and before it
  uses Wep_Scope1 + (0, 0.035, -0.15) along the barrel. `weapon/two_handed` aims along it
  (`shot_line = gun`) and holds the game's LT while an eye is near the line (`sight_ads`; bands below,
  "The game's reticle follows the view").

## Magnified sights (2026-11-04, monitor: fakehead, LT with `ads_trigger 1`)

**The game's zoom.** The world FOV the game asks for is in the view-params block (`+0xB4`) before
the camera hook replaces it with the headset's. Live, AK-12: hip **45 deg**, red dot ADS **40 deg**
(1.14x), flip magnifier ADS **27 deg** (1.73x as tan(hip/2)/tan(aim/2)); it eases between them. The
weapon data has the pieces (SoldierWeaponData +0x138 WeaponZoomLevels: RenderFov 55/18 deg, the gun
pass; +0x148 AimingController +0x10 ZoomLevels: FieldOfView 55/42, FieldOfViewSP 45/-1), the
magnifier's value comes from a weapon modifier (+0x140, not decoded). The static candidate from REA,
the camera `+0x11C` override in `0x140FA8270`, stays -1/0 in ADS: not the zoom.

**The magnifier in the model**, from the sight's eye point (fakehead 1 mm), both eyes 64 mm apart:

| Part | Measured | Result |
| --- | --- | --- |
| Rear lens opening | 112 px across at 2160 px, tan(half FOV) 0.889 | 2.6 deg radius from the eye point |
| Housing | 522 px between the eyes | 14.9 cm ahead of the eye point, about 2 cm in radius |
| Rear lens | 0.73 of the housing | 1.45 cm radius (the `stereo/scope` lens) |
| `Wep_Scope2` | child of Wep_Scope1 at (-0.0185, 0.0013, 0) | probably the flip hinge, not the lens |
| Red dot ring (magnifier folded) | ~450 px between the eyes | ~17 cm ahead (Scope1 is 15.9 cm ahead) |

**What the mod does** (`stereo/scope`, `scope_compose`, `eye_pass` `render_scope`): when the game aims
at 1.25x or more and the drawn gun's sight is fresh, the world is rendered a third time from the sight's
eye point along the sight line (the gun's up), with the lens's narrow FOV (lens angle from the eye that
looks through it, divided by the magnification) widened so the lens circle covers 30 % of the render's
height. The constants are patched from the first eye's like the second eye's (a FOV change patches
correctly: `eyezoom` test, lighting matches); the gun's per-view blocks are blanked (gun and arms
not drawn). The lens circle is cut out with a pixel shader (round, premultiplied, a red dot on the
sight line) and shown on an OpenXR quad at the lens, for the sighting eye only; the aim reticle hides
meanwhile.

**Cost** (Release, 2160x2160 per eye, monitor), measured with GPU timestamps around each render
(`status`: `gpu_ms eye1 eye2 scope`):

| Version | Scoped fps | Scope GPU | Scope on the render thread |
| --- | --- | --- | --- |
| First, linear patch table | 33 | – | 22 ms (patching 11, render 7) |
| Patch table indexed + bit filter (a turned camera changes ~150 windows, not ~25) | 66-71 | 3.3-4.3 ms | 6 ms |
| Scope patching on a worker during the second eye | 72.5 | 4.3 ms | 3.4 ms |
| + scissor to the lens circle (`stereo/scope_scissor`) | **87-91** (50 s: avg 90.8, p5 88.8) | **1.2 ms** | 3.4-4.4 ms (overlapped) |
| Hip, no scope | 98-107 | – | – |

The frame is GPU-bound (hip: eye 1 ~5 ms + eye 2 ~4 ms of GPU per ~10 ms frame; the "readback" wait is
the CPU waiting for it). Tried and dropped: switching off shadow-map generation, the foreground passes
or the sky envmap update for the scope render (no measurable gain), and a scope at every second frame
(87.5 fps, but uneven frames and a 45 Hz lens picture: the scissor reaches the same with a steady one).
The scissor keeps 1.3x the lens circle on targets proportional to the back buffer whose viewport covers
them (shadow maps and other sizes untouched); compute passes still run over the whole image. Lens
images with and without it are identical (wall, and the room with a fake controller).

**Gun depth in ADS (bug found on the way, also on `main`)**: the life-size factor drops to 0.168 in ADS
(gun pass 17 deg), the gun camera's rows grow to 5.95, and `shift_per_view_block` refused rows over 5:
the gun had **no second-eye offset in ADS** (0 px between the eyes) and could not be hidden from the
scope. The limit is now 25 (g down to 0.04); ADS: 22 -> 56 per-view blocks moved per frame, the red dot
now ~450 px apart between the eyes (~17 cm, as expected).

## The game's reticle follows the view, not the sight (2026-11-10, monitor, #39)

Why the headset saw no red dot although the monitor did (AK-12 red dot, `gun_fov_life_size` on,
fakehead + fakehand, eye pass images):

- With the eye exactly on the sight line (0.4 cm) and the head looking straight down it, ADS engages
  and stays (`changes=1`), and the dot and ring sit centred in a clear lens.
- **The dot is drawn at the eye image's centre** (1080, 1080 of 2160), masked by the lens: it marks
  the view's forward axis, not the sight's. In the game's own ADS the camera always looks down the
  sight, so the two coincide.
- Eye on the line, head pitched **2 deg**: the dot moves up inside the lens. **5 deg** up or down:
  no dot, only an arc of the ring. Eye 2.6 cm off the line (still counted as aiming): no dot, the
  ring drawn outside the housing.
- In a headset the head is never held within ~2 deg of the barrel, so the lens shows nothing: the
  cause of "sights show nothing" (AIM-12), more than the 2.5/4 cm hysteresis flicker seen in the marks.
- Draw diff ADS vs hip (`meshprobe`): the same 933 draws in both; small gun-pass candidates for the lens
  and reticle are #594-596 (276/72/36 indices, blend `...271D88`) and #889-891 (24/72/474, HDR target).
  `eyeskip` on those ranges did not remove the dot (the second eye's draw numbering differs from the
  probe's), so the reticle draw is not identified yet.

**What the mod does** (`sight_dot`, chosen 2026-11-10): its own collimated red dot, an OpenXR quad
50 m down the sight line from each eye, shown (left/right eye visibility) only to an eye within the
lens radius (1.5 cm, the magnifier's rear lens) of the line and between 30 cm behind the game's eye
point and 2 cm short of the optic (`two_hand` `sight_dot_for_eye`), **aiming or not**: sights stay live
without the game's ADS, which (`sight_ads`) only clears the AK's lens and gives ADS accuracy. The green aim dot hides meanwhile;
the magnifier's own scope picture keeps its dot. `sight_ads` now enters at 3 cm (under half the eye
distance: a gun between the eyes does not aim) and leaves only after 0.3 s beyond 5 cm
(`sight_ads_latch`); monitor: held through a 0/4.5 cm shake and a brief 7 cm slip.

**The optics without the game's ADS** (monitor, `sight_ads 0`, sight on the left eye):

| Optic | Hip (no ADS) | Game ADS |
| --- | --- | --- |
| SCAR-H holographic | window clear, **no game reticle** (a faint glint only): our dot is the only reticle | the game's ring, cross and dots at the view centre |
| AK-12 red dot | lens **solid black** | lens clear, the game's dot and ring at the view centre |

**Hiding the game's lens and reticle (`eyehide`, 2026-11-10): not one draw.** No single gun-pass
draw (indexed, by pixel shader) and no single non-indexed draw removes the dot or ring; skipping
every non-indexed draw does (and also unlights the gun), so the reticle comes out of a full-screen
or compositing pass. With every gun draw hidden, depth-only draws included, the AK's black lens
still floats where the lens was: it is drawn outside the gun pass. Combinations of early draws on an
864x864 target (the first draws of the frame, where the gun pass also renders a small view) clear it,
consistent with the lens showing a separate small render of the scene (the "magnified grey patch"
first seen in #39). Draw order shifts between frames, so `eyeskip` bisection by index is unreliable.
Left for later: the 864x864 view and the compositing pass, or a game-side switch for the lens state.

Still open: with `sight_ads` on, the game's own reticle still shows when the head happens to look
down the barrel (then it nearly coincides with ours). Guns with iron sights
also get the dot (no optic type is read yet). The other route, turning the gun pass's view axis onto
the sight while keeping its view-projection, depends on whether the lens shader works in view space.

## Hip-fire spread (2026-11-10, monitor, #39)

With sights live without the game's ADS, the game's hip spread decides where shots go. Measured with
`recoil 0`, fake head and hand, the wall ~3.3 m away: in ADS 20 rounds made one hole; from the hip 12
rounds spread over a cone of roughly 3-7 degrees. The data (typeinfo, `GunSwayDispersionData`, degrees):

| Gun | Stand hip still / moving (min..max) | Stand zoom still / moving | Crouch hip | Prone hip |
| --- | --- | --- | --- | --- |
| `U_AK12`, `U_SCAR-H` | 3..7 / 3.5..7 | 0.2..1.7 / 1..1.7 | 2.5..7 | 2..7 |

Jumping (hip 6..6, zoom 7..7), sprinting and vaulting are not tighter zoomed. IncreasePerShot 0.11
(AK) / 0.08 (SCAR); DecreasePerSecond 15 hip, 9.2 / 14.7 zoom.

**The game reads these live**: writing the zoom values into the AK's standing hip block made the next
hip burst one hole (14 rounds). `weapon/hip_accuracy` (setting `hip_accuracy`, 1 = zoom values, 0 = the
game's) blends the still and moving hip blocks of every stance towards zoom for the player's carried
weapons (rescanned every 0.25 s), keeps the originals and writes them back at 0 or when the mod
disables itself. Monitor: patched hip bursts one tight group, `hip_accuracy 0` the game's spread again.
Headset 2026-11-11: at 1 a full-auto burst stays pin-point on the dot (before, it spread out a lot). Kept
at 1 for now; for more spread later, the blend is linear per field (`hip + (zoom - hip) * strength`), so
the standing-still cone (min..max degrees) is 0.2..1.7 at 1, 0.9..3.0 at 0.75, 1.6..4.35 at 0.5,
2.3..5.7 at 0.25 and 3..7 at 0; moving 1..1.7 at 1, 2.25..4.35 at 0.5. IncreasePerShot and the decrease rates
are blended the same way (their zoom values were not written down). `pipe hipaccuracy` shows the values as written.
GunSwayData is the weapon's data asset: soldiers carrying the same gun model may share it (not checked
for the campaign's AI).

## The magnifier without the game's ADS (2026-11-10, monitor, #39)

- Runtime zoom levels (weapon `+0x47D0`, `WeaponZoomLevelData*`) carry `SightType` at `+0x2C`
  (enum: 0 None, 1 Canted, 2 Magnifier). AK-12: level 0 hip, 1 the red dot, 2 **Magnifier**; SCAR-H:
  0 hip, 1 the holo (None). No world zoom there (`RenderFov` is the gun pass's, held at life size).
- Aiming simulation (weapon `+0x4988`) **`+0x1F8` = the level the next aim uses**: D-pad up at the hip
  flips it 1 <-> 2 with the magnifier (diff of the object around two flips); `+0x1E8` the level in use
  (0 at the hip, 2 while aiming through the magnifier).
- `weapon/sight_state` reads both; `stereo/scope` runs the lens picture at the hip when the selected
  level is a Magnifier (or any level with a learned zoom of 1.25x or more) and an eye is within 3 cm of the sight line, at the zoom learned while the game
  last aimed through that weapon and level (`scope_math::magnification_memory`; 1.725x measured, 1.73x
  until learned). Monitor: folded away no picture; flipped in, eye on the line: the 1.7x picture with
  its dot; 4.5 cm off: none; ADS learned 1.725 (and 1.14 for the red dot level).
- Found on the way: at 4-5 fps (Debug + the scope render) the drawn gun's data is sometimes older than
  250 ms; the sight latch reset on such a frame and ADS cycled ~3 times a second. A missing sight line
  now counts as "outside" for the 0.3 s hold. The scope's own 250 ms freshness still drops single
  frames there (not at headset frame rates).

## A gun's attachments (2026-11-10, monitor, #39)

The weapons component (soldier `+0x570`) keeps a record per slot at **`+0x120 + 0xC0 * slot`**: `+0x00`
the weapon's unlock, `+0x10` / `+0x18` the leftmost / rightmost node, **`+0x20` the root**, **`+0x28` the
count** (u32) of a tree of every unlock on that gun; a node has its children at `+0x00` / `+0x08`, the
parent (low bit = colour) at `+0x10`, **the unlock asset at `+0x18`** (name at asset `+0x20`). `+0xA0` is
the weapon blueprint. Live walk:

| Slot | Unlocks |
| --- | --- |
| 0 `U_SCAR-H` | `U_SCAR-H_EOTech`, `_Foregrip_Angled`, `_MuzzleBrake`, `_TargetPointer` |
| 1 `U_AK12` | `U_AK12_PK-AS`, `_Magnifier`, `_Foregrip_Folding`, `_HeavyBarrel`, `U_Grass_Woodland` |
| 6 `U_M67_SP`, 7 `U_Knife` | themselves only |

Found by scanning the weapon object (only `+0x4850`, the weapon unlock) and then the weapons component
for asset names. `weapon/optics` classifies the optic by its suffix (red dot / holo, magnified with a
nominal zoom, the magnifier; unit-tested; only PK-AS, EOTech and Magnifier seen live).
`weapon/sight_state` walks the tree when the record's unlock is the current weapon's: the red dot
(`sight_dot`) now needs a red dot or holo among the attachments (iron-sight guns and scopes get none;
unreadable attachments keep the old behaviour), and the scope at the hip falls back to a magnified
optic's nominal zoom before the game has shown it.

**No game aim for a clear holo**: `weapon/optics::needs_game_aim` is false for a gun whose only optic
is a holo (EOTech, clear at the hip, no game reticle there). With `hip_accuracy` at 1, `sight_ads` then
leaves the game's aim alone: no game reticle over the dot and full walking speed. Monitor: AK-12
aims (`aiming=1`), SCAR-H does not (`aiming=0`, dot shown), SCAR-H with `hip_accuracy 0` aims again.

## Why the headset never aimed: the two-handed hold ate the aim button (2026-11-12, #39)

Read from the headset marks of 2026-11-11 (`bfvr_marks`, their `twohand` / `scope` sections), then
reproduced on the monitor. Every headset mark had `held=1` (the gun in both hands); every earlier
monitor check used one fake hand.

- **Cause 1.** `weapon::filter_two_hand_pad` zeroes the pad's LT while the off hand holds the handguard (its
  trigger is the accessory toggle). It ran after `input::for_game`, which had set LT to 255 for `sight_ads`.
  So with both hands on the gun the game never aimed: lens black, `game_fov=45`, zoom level in use 0, no
  magnifier picture, while the mod reported `aiming=1`.
- **Cause 2.** `body/arm_pose` trusted that report: 0.5 s later it stored the hip pose's camera as the
  "ADS sight line" (`from_ads=1`, `eye_from_optic=(0.076 0.050 -0.223)`, 1.4 deg right). The line jumped
  7.6 cm to the left: the dot was gated on a line beside the optic, shots and the green dot followed
  it, and the left eye (2.4 cm from the wrong line) held the latch. Marks 2-4 show exactly these values.
- **Fix.** The sight aim's LT goes in after the two-handed filter (`input::with_sight_aim`, order in
  `hooks/xinput_hook.cpp`); `game_aiming` answers from the pad the game finally gets
  (`input::note_game_pad`); an ADS sample counts only with the camera behind the optic on its axis
  (`body::ik::ads_eye_behind_optic`, unit-tested with both guns' values and the hip pose).

Monitor, AK-12, fake head 6.4 cm, right hand (0.054, -0.166, -0.3), left (-0.024, -0.158, -0.60), pad
`300` (both grips; `free_hands 0`, else the fake gun hand without a grip slings the gun):

| Hold | Before (main 40ce38c) | After |
| --- | --- | --- |
| One hand (`200`) | `game_fov=40 current=1`, eye_from_optic (0.000 0.035 -0.159), eyes 6.5/0.1 cm, `dot_seen=0/1` | same |
| Both hands (`300`) | `game_fov=45 current=0`, eye_from_optic **(0.077 0.051 -0.222)**, eyes 2.4/8.0 cm, `dot_seen=0/0` | `game_fov=40 current=1`, (0.001 0.035 -0.159), eyes 6.5/0.1, `dot_seen=0/1`; `eyeshot2`: lens clear |
| Both hands, magnifier in | (not run) | `game_fov=27 magnification=1.725 active=1 eye=1 current=2` |
| Both hands, eye 1.9 cm off | | `dot_seen=0/1` (`sight_dot_reach` 2 cm) |

**The SCAR-H's dot is not explained by this.** Mark 1 (SCAR, right eye 0.9 cm off the correct line) had
`dot_seen=0/1`: the layer was submitted to the right eye and still not seen. Not verifiable on the
monitor (no compositor). Changed on suspicion: the dot was 0.3 degrees of plain red (core ~3 px) in a
red room, now 0.6 with a near-white middle; `sight_dot_reach` (cm, default 2) replaces the fixed 1.5 cm;
`sight_dot_test` shows it to both eyes wherever the gun is, to tell "layer not shown" (single-eye quads
have never been confirmed in the headset: the scope picture uses them too) from "shown but missed".
Needs the headset with a VR View capture.

**Headset 2026-11-12 (8 marks, VR View captured ~1 s after each mark; VR View shows the left eye).**
All two-handed (`held=1`). What the marks and captures show, before the user's own verdict:

- The game now aims with the AK-12 in both hands: marks 3-5 and 8 `game_fov=40 current=1`,
  `eye_from_optic=(0.001 0.035 -0.159)`; marks 6-7 the magnifier `game_fov=27 magnification=1.725 active=1`.
  `sight_dot_frames` rose from 1509 to 8666 over five minutes (997 in four the day before).
- **The single-eye dot layer is shown by the compositor**: mark 8 (left eye 0.3 cm off the line,
  `dot_seen=1/0`) has the red dot in VR View, beside the red dot sight's clear lens, about 50 px (~5-6
  degrees) left of the lens centre. Marks 1-6 had the right eye on the sight (`dot_seen=0/1`), which
  VR View cannot show.
- **The magnifier picture is shown too** (mark 7, left eye), but about a lens diameter down-left of the
  drawn magnifier housing, and larger than the housing's rear lens (picture ~100 px across, drawn lens ~60).
- Both offsets point at lead 2 of the brief (the drawn optic is not where the mod's points say), but one
  delayed capture each is weak: tapping both stick clicks jolts the gun and the scope picture is a few
  frames old. Settings in that session that scale the picture against LOCAL: `gun_scale 1.1`,
  `ipd_scale 1.15`. Next: a pipe report of the optic's and the dot's projected pixel per eye to compare
  with `eyeshot` off-axis, and a capture without the stick tap (`pipe mark` + `vrview-shot` from the PC).
- `changes` went 2 -> 21 in five minutes (11 by mark 3, steady through marks 3-5).

Test pitfalls: `pad` buttons replace the grips, so a fake gun hand needs `200` held or the gun is slung
(`free_hands`); `ctest` has only debug presets (`bf4-debug`, `bf3-debug`).

## Open

- Other magnified optics (4x ACOG, sniper scopes) carry SightType None: at the hip their lens picture
  runs once the game has aimed through that weapon and level (learned zoom >= 1.25x); before that only
  in the game's ADS. Their lens size and reticle are still the magnifier's (untested: no such optic in
  this checkpoint).
- Scopes on other guns (4x ACOG, sniper scopes): their lens size and distance (one `scope_math::lens`
  per optic, now the magnifier's for every gun), and the game's own scope overlay at high zoom.
- The scope's compute passes (tiled lighting, SSAO) still cover the whole image; a smaller render
  target for the scope pass would cut them too.
- Hardware-watch the writer of the server ammo to find the auto-reload decision, if the spare rounds
  ever show (HUD) or leak (a full-auto burst faster than one frame).
- Shotguns, bolt actions, belt-fed guns, pistols: variants and their bones (`Wep_Belt*`, `Wep_Slide`).
- Underslung launchers (`Wep_Grenade1/2`) use a second firing function.

## Where shots start (2026-11-05, monitor)

- Every shot starts at the weapon's **shot frame**: ClientSoldierWeapon `+0x49A8` -> ClientWeapon, `+0x40`: 4x4
  rows right, up, forward (`+0x60`), position (`+0x70`), world space. The game sets it every frame at the
  camera joint (0.000 m) along the aim (fake controller 30/10 deg -> 29.2/9.6, the sway). Found by scanning
  the weapon objects for a transform at the camera.
- Its only writer is a generic 32-byte setter `0x1401473B0` (copies rdx to rcx +0x40..+0x7F; 10 callers),
  called for the weapon from `0x141013119`. Read-watch while firing: `0x14092B0CB` reads it once per bullet
  (10 reads in 0.9 s of AK fire), others every frame.
- `weapon/shot_origin` hooks the setter; for the local player's current weapon (pointer cached 200 ms)
  under controller aim it moves the frame to the drawn gun's sight line (`weapon/two_handed`'s start, the
  sight eye point else the optic or Wep_Root) along the barrel, converted from OpenXR LOCAL with the body
  yaw and the position the game passes in (plus the physical-stance lift); never more than 0.8 m from
  the camera. Engine-free maths: `weapon/shot_frame`, `tests/shot_frame_test.cpp`.
- Check: gun 25 cm below and 5 cm left of the eye, wall ~2 m: a burst with `shots_from_gun 0` hit near
  the view centre (from the eyes towards the point 25 m along the barrel); with 1 the holes sat ~23 cm
  lower: the bullets leave the gun. Live frame: 37 cm from the camera, forward exactly the barrel's 20 deg.
- Still from the camera: the game's own aim (the aimer, so the body and dispersion) and the green dot's
  depth (drawn at 25 m: from the hip, a close hit sits off the dot by parallax; on the sight line it is on it).
- Since #37 the game's aim stays with the body (`aim_with_body`): the bullet direction comes from the
  shot frame alone (holes follow the gun 15 degrees left and right with the aimer fixed).

## Empty hands: the gun in the off hand (#38, 2026-11-09, monitor)

- `weapon/free_hands` (with `free_hands = 1`) has a third holder besides the gun hand and the floor: the
  off hand. Letting go with the gun hand while the off hand holds the handguard (the two-handed hold,
  `weapon/two_handed::off_hand_holds_gun`) hands the gun over; the gun hand gripping within 12 cm of the
  pistol grip takes it back; the off hand letting go drops it. Unit-tested (tests/free_hands_test.cpp).
- `body/arm_pose`: the carrying hand is solved first; the gun keeps its animated placement relative to it
  (for the left hand: where the animation holds the handguard) and turns with that controller as it was at
  the hand-over (no jump). The trigger is blocked, the two-handed lock is off. The pistol grip (the
  animation's right-hand point on the gun) is published (`weapon_points::grip`, pipe `holster` grip=).
- `weapon/physical_reload` passes the free gun hand as the reloading hand with `handle_only`: only the
  charging handle (no magazine or pouch grabs); it is drawn with the palm on the handle (the pinch and hook
  shapes are the left hand's).
- Monitor (fake hands, pad grips): two-handed -> right let go: in the left hand, no jump; left moved 15 cm
  and turned 25 deg: the gun follows (the grip point swings back to the right as expected); right hand at
  the handle zone, grip, 6 cm back: racking, pull 0.057; right grip at the published grip point: taken back.
  Test pitfall: `pad` holds for at most 10 s; the grips must be re-sent, or the off hand "lets go".

### Sling, grenade in either hand, finger shapes, the left-hand pistol grip (#38, 2026-11-09, monitor)

- **Sling** (`sling`, default on, with free hands): a gun let go by both hands hangs at `body_rig::sling_point`
  (belly height, gun side, 12 cm in front, in the holsters' lazy body frame), barrel 75 degrees down, and
  follows the body; the gun hand (or the off hand) grabs it back within 15 cm (`sling_reach`: the grenade
  pouch is 23 cm above it). Off: the floor as before. Monitor: let go -> slung; a room turn moved it with
  the body; gripped there -> in hand.
- **Racking hand's fingers**: the hook and underhand points are measured on whichever hand racks
  (`racking_hand()`); the right hand hooks the handle in its own index (the pinch is the left hand's
  authored pose). Monitor: the right hand's task is `handle` while racking.
- **Grenade in either hand** (`grenade_hands`, `either_hand` = the gun dropped or slung): whichever hand grips
  at the pouch takes it, the other pulls the pin, the holder throws; drawn at that palm (theater), the gun
  hand's fingers make a fist. Unit-tested incl. a throw by the gun hand; monitor: taken by the right hand
  (hand=gun, fist), let go unarmed -> put back (no throw in the checkpoint room).
- **Left-hand pistol grip**: the off hand picks the dropped/slung gun up -> `in_off_grip`: the gun upright
  along the left aim ray, placed so the right hand's spot on the grip, mirrored across the gun, is the left
  hand; the hand takes the mirrored gun-hand grip (`fingers::mirrored_hand`), its fingers `gun_grip`; the
  left trigger fires (the latched controller value, not the game's LT); letting go puts it back on the
  sling/floor. Monitor: picked up at the sling, aimed 20 degrees right, the left trigger fired 4 rounds,
  let go -> slung.

### Left-handed two-handed hold, use by reaching under #37, crate menu clicks (2026-11-09, monitor)

- **Two-handed with the left on the grip** (#38): `weapon/two_handed` swaps the roles while the gun is in the
  off hand by its grip (the left aims, the gun hand's grip and trigger are the support hand's, the support
  offset mirrored); `arm_pose` applies the barrel line to the hand on the grip and draws the gun hand on the
  handguard at the animation's left-hand spot mirrored across the gun (`fingers::mirrored_hand`). Monitor:
  held, steering the right hand 12 cm swung the barrel from -9 to -29 degrees, released.
- **Melee gesture** off while the gun is in the off hand or dropped: a fast reach of the free gun hand (the
  fake hand jumping to the handguard) counted as a strike, the game drew the knife for it, and the weapon
  change reset free hands.
- **Use by reaching** (`use_reach`) with `aim_with_body` (#37): the game picks the use target with its aim,
  which now stays with the body. While the off hand reaches to use, the aim goes from the head towards that
  hand (`weapon::use_reach_direction`, camera_hook): what the hand reaches for is used. Monitor: the gadget
  stash opened by reaching right-low while facing ahead.
- **Crate menu clicks (#34)**: with BF4 not the front window (the usual case in VR) the pointer posted
  WM_MOUSEMOVE (hover worked) and WM_LBUTTONDOWN/UP, which the game ignores: it reads mouse buttons as Raw
  Input. The click now also posts the synthetic WM_INPUT (marker handle, `GetRawInputData` hook answers
  RIM_TYPEMOUSE with RI_MOUSE_LEFT_BUTTON_DOWN/UP), like the keys. Monitor (pipe `pointer`, the same
  function as the headset pointer): C4 -> Gadget One, the Gadget Two tab, M320 -> Gadget Two, Done closed it.

## The dot on what the gun points at (2026-11-09, monitor: fakehead + fakehand in the prologue corridor)

- **Depth source**: while the HUD binds the back buffer (stereo/hud_capture's OMSetRenderTargets hook) the
  depth target is the resolved scene depth: `D24_UNORM_S8_UINT` view of an `R24G8_TYPELESS` 2160² texture,
  1 sample, bind `0x48` (depth-stencil and shader resource), so it can be read as `R24_UNORM_X8_TYPELESS`.
  In pair rendering it is the second eye's (the first eye's image was copied away before eye 2 drew):
  the readings were identical with the eyes 6.4 cm and ~23 cm apart.
- **Projection** (RenderView `+0x320`, `eye_pass::world_projection`): row-major x/y scale 0.9324/1.2272,
  `[2][2]` -1, `[3][2]` -0.06: reversed infinite depth with the near plane at 6 cm, depth = 0.06 / d, so
  d = -0.06 / (depth - 1).
- **Method** (`stereo/aim_depth`, math in `aim_depth_math`, tested): 32 points along the shot line
  (`input::shot_line`, 0.75 to 80 m, spaced geometrically) projected into that eye; a 32-thread compute
  shader reads the depth at each, after unbinding the targets and keeping the game's compute state; the
  result goes through a 3-deep staging ring read with `DO_NOT_WAIT`, so it is 2-3 frames old. The first
  point at or behind the surface (interpolated with the one before) is the hit; surfaces nearer than
  0.8 m (the drawn gun and arms) and points far behind a nearer surface (a pillar between eye and line)
  give no information. `weapon::two_handed` puts the dot there and converges the aim on it
  (`reticle_depth`, falls back to `aim_convergence` when nothing is hit or the reading is over 300 ms old).
- **Monitor**: aimed across the flat red wall at yaw -15/-25/-35/-45 degrees the hits (2.16, 1.92, 1.78,
  1.70 m) lie on one straight line within 0.7 cm, steady from frame to frame; 20 degrees down hits the
  floor at 1.5 m; down the corridor 9 m. Cost: no measurable frame time (131.3 fps on, 131.9 off).
- **Limits**: a line that leaves the view (aiming more than ~50 degrees from where the head looks) has
  no depth to read and the dot goes back to `aim_convergence`; so does a wall nearer than 0.8 m.

## The free right hand reloads while the left holds the gun (#38, 2026-11-09, monitor)

- **Roles**: while the off hand carries the gun (by the handguard or by the grip) the reload reads the
  right hand as the reloading hand (`physical_reload`: its palm, aim rotation, RB as its grip, haptics on
  it; B stays the release). `free_hand_reload` (default on) lets it change magazines; off, it only racks
  (`handle_only`, as before). AR palming stays the left hand's (the magazine turns to that hand's outside).
- **Drawing** (`arm_pose`): the held magazine goes in the reloading hand's fist (`attach_held_magazine` /
  `fist_magazine` on `racking_hand()`); the grasp frame comes out mirrored on the right hand, and a rifle
  magazine is symmetric across the gun, so the same placement holds. No palm cup and no authored magazine
  grip on the right (both are the left hand's). Monitor: the AK magazine upright in the right fist, its top
  11 cm above the palm.
- **Right-hand pinch**: the left hand's authored pinch applied to the right hand with each local rotation
  mirrored, `(-x, -y, z, w)` (the hand bones mirror in z). Monitor: the knob point in hand axes came out
  mirrored (left (0.126, 0.023, -0.036), right (0.117, 0.040, 0.037)), but the right thumb ends ~2 cm closer
  to the index (gap -11 mm vs +8 mm): its rest pose is not an exact mirror. Follows `rack_grip`.
- **Grip conflict**: with the gun carried by the handguard, a right grip near the gun can mean "take the gun
  back" (free hands) or "take the magazine / handle" (reload). `grip_before_reload` (tested): the pistol grip
  wins only within `regrip_reach` and nearer than every reload zone; the reload claims the hand while it
  holds a magazine or racks (`reload_claims_free_hand`, free hands' `busy`).
- **Monitor** (AK-12, prologue): by the grip: pouch -> magazine taken (30), seated, racked (round ejected),
  the left kept the gun; by the handguard: magazine out and dropped with the gun still in the left hand,
  then a grip at the pistol grip took it back.

### Use by reaching: why the stash would not open in the headset (2026-11-09, monitor, #34)

Headset: the gadget stash did not open with the left hand. The use pose (`weapon/use_reach`) was too narrow
for a natural reach; pipe `holster` now prints the off palm's `palm_forward`, `side`, `up` from the head.

| Rule (before) | Effect | Now |
| --- | --- | --- |
| Measured at the drawn wrist | ~8 cm behind the controller: a 33 cm reach read 25 | At the palm (controller grip point), like every grab zone |
| At least 40 cm ahead of the eyes | Needed a stretched arm | 28 cm (every body zone sits within 15 cm ahead) |
| Down to 70 cm below the eyes; ground pickup from 95 cm | A crate at hip height fell between the two | Down to 95 cm, meeting the pickup |
| Not within 30 cm of the gun hand | Reaching ahead-right for the crates with the gun held low was refused | Not within 18 cm of the gun itself (grip through handguard, 20 cm past it; the two-handed grab takes 12.5 cm); a dropped or slung gun refuses nothing |

Monitor (head level, gun held): a bent-arm reach 33 cm ahead, 30 cm right of centre, and one 40 cm ahead on
the off side count; the chest pouch (12 cm ahead) does not. Head pitched 30 degrees down, hand 40 cm ahead
toward the crates: the stash opened. Its menu closes with the pointer when it hovers before the click
(Done at 0.648, 0.736).
