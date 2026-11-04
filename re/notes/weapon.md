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
  (`shot_line = gun`) and holds the game's LT while an eye is within 2.5 cm of the line (`sight_ads`).

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

## Open

- Scopes on other guns (4x ACOG, sniper scopes): their lens size and distance (one `scope_math::lens`
  per optic, now the magnifier's for every gun), and the game's own scope overlay at high zoom.
- The scope's compute passes (tiled lighting, SSAO) still cover the whole image; a smaller render
  target for the scope pass would cut them too.
- Hardware-watch the writer of the server ammo to find the auto-reload decision, if the spare rounds
  ever show (HUD) or leak (a full-auto burst faster than one frame).
- Shotguns, bolt actions, belt-fed guns, pistols: variants and their bones (`Wep_Belt*`, `Wep_Slide`).
- Underslung launchers (`Wep_Grenade1/2`) use a second firing function.
