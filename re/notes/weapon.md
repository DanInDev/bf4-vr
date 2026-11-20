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
server  player ServerSoldierEntity (weapon/server_soldier: no fixed global, see "The player's server soldier") +0x7C0 ServerSoldierWeaponsComponent
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

## The player's server soldier (2026-11-13, live, Baku)

The fixed global `0x142311DB8` used until now is one slot of a static table of spawned soldiers, and the
player is not always in it. Session 1: the player at `0x142311DB8`. Session 2 (a fresh load): `0x142311DB8`
null, the player at `0x142311E08`, also at `0x14237F3F0` and `0x1427248B0`. With the slot null the mod saw
"no magazine weapon in hand": physical reload and physical grenades were off and **the game reloaded by
itself** (the owner's report "after a respawn it automatically reloads itself").

- `ServerSoldierEntity` vtable `0x141E67BA0` (typeinfo map); four instances in the level (player and squad).
- `+0x720` (and `+0x730`) Vec3: the soldier's feet. Player 0.00 m from the client soldier's feet
  (`ClientSoldierEntity +0x1E8` rig, `+0x90 +0x30`); the nearest squadmate 1.53 m.
- `weapon/server_soldier` checks the last answer every call (vtable and position within 1 m of the client's
  feet), else the static tables (`0x142311D88 + 0x10 i`, i < 16; `0x14237F390 + 0x20 i`, i < 8; `0x1427248B0`),
  else a background heap scan for the vtable (at most every 3 s). Unit test `server_soldier`.
- Live (Release, `fix/server-soldier`): fresh load `server soldier: ...178980C50 (static table)`, reload
  active, grenades readable (3). A grenade at the feet killed the player; at the respawn the log has
  `server soldier: ...177C752D0 (static table)` and `reload: U_SCAR-H slot 0 AR`. 19 shots emptied the
  SCAR-H, the bolt locked, the game's magazine stayed at the 2 spare rounds and did not reload in 10 s.

## Weapons from the ground (#54, 2026-11-13, live, Baku after the fight)

- **Hold R takes it.** Standing 0.69 m from a dropped AK-12 the game showed "[R] HOLD TO PICKUP AK-12";
  `pipe key "52 1500"` (R through the game's Raw Input, BF4 not in front) put `U_AK12` in slot 1 and in hand.
  The SCAR-H stayed in slot 0. `ConceptPickUp` and `ConceptReload` are both R: R near no pickup is a reload.
- **The picked-up AK-12 came empty** (`reload: U_AK12 slot 1 AK, capacity 30, gun 0`): the game reloaded it
  by itself within 2 s of the take (2 -> 31, the mod took its rounds) before physical reload could hold it.
  Open: a picked-up gun with rounds keeps them; an empty one gets this one game reload.
- **Census:** `ClientPickupEntity` (vtable `0x141E5FD70`) found by a heap scan, 11 after the fight; each one's
  `PickupEntityData` at `+0x30` (InteractionRadius 2.5, AllowPickup 1) and position at `[+0x140] +0x30`.
- **In the mod (`weapon/weapon_pickup`, setting `weapon_pickup`, pipe `pickup`):** the census every 1.5 s in
  the background; each frame the pickups go to OpenXR LOCAL with the rig's own transform (`weapon_points`
  `world_origin` / `world_to_local`, the one every gun point uses). A fresh squeeze of a free hand within
  35 cm of a pickup, or of a hand at least 50 cm below the head over it (up to 1.2 m above, 50 cm around),
  holds R until the weapon in hand changes, the grip opens or 2 s pass, and only for a pickup within its
  radius of the head. These are the BF3 single-player mod's approved magnetic-take numbers (ideas only).
  The off hand's use reach (E) stays off meanwhile. Unit test `weapon_pickup`.
- **Monitor, bundle build (2026-11-13 21:10):** the checkpoint kept the fight's 11 drops (census 11, usable 11,
  scan 219 ms every 1.5 s). The LOCAL conversion matches the world: the mod's nearest pickup to the right palm
  1.32 m, the world distance from the right wrist 1.33 m. The fake off hand lowered over the AK-12 (0.62 m from it,
  0.9 m below the head) and squeezed: `pickup: take by the off hand (R held), 11 pickups near`, `pickup: taken`
  0.35 s later, R up, `U_AK12` in slot 1 and in hand, physical reload on it (29 + 1). A squeeze 1.24 m from any
  pickup: no R, the magazine untouched.
- Test pitfall: `fakehand`'s positions are in OpenXR LOCAL, turned against the game's camera by the body yaw.
  Find the axes first: place the hand at three points and read `arms` (`goal=` is the world position).
- The BF3 mod instead hooks the game's interaction update and calls its own take for the gripped gun; that
  would pick the exact gun when two lie together (here the game picks the one it offers). Later, if needed.

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

### Slot copies ahead of the chest (#41, 2026-11-16, monitor, Baku "Get Grenade Launcher")

- `slot_models_ahead` moves the copies, not the grab zones, along the body's facing. Pipe `extramesh` `at`
  is in a world frame turned 180 deg from the rig (`holster` body_yaw 0 = rig -z): offsets 0 / 0.08 / 0.14
  gave grenade and knife z 0.236 / 0.316 / 0.376, x unchanged.
- Pictures need the plain `shot` (pair rendering): `eyeshot` never showed either copy. Without a capture
  (`extramesh` grenade `captured=0`) the pouch draws nothing; `pipe grenade capture` first.
- Grenade copy: fakehead `0.064 0 0 -70`, it sits under the view at 0, further ahead at 0.08, out of the
  frame at 0.3. Follows the offset as intended.
- **Knife copy (open):** captured and replayed (`knife_copy captured=1 draws=2..5`, `copies` climbing, `at` moving
  with the offset), yet in no frame on the monitor at 0 / 0.14 / 0.3, also with the left arm swung out of the way
  and the head turned to the holster (2026-11-16, three runs). The black knife-like grip seen beside the grenade in
  one run moved with neither `slot_models_ahead` nor `knife_pitch`: not the copy (likely part of the gun seen from
  above). Copies are replayed right after a magazine draw of the same pass; with the arms forward and the gun out of
  view even the captured grenade copy did not show. Next: log which pass (targets / format) each knife replay lands
  in; a depth-only pass (targets=1, format 34) would explain draws without a picture.
- **Second pass (2026-11-16, `extramesh` now lists each captured draw's pass and scale and the passes the magazine is
  drawn in):** ruled out the pass and the capture. The knife's draw is in the G-buffer pass (4 targets, format 28)
  that the magazine is drawn in, with a full-size palette (scale 1.000), and is replayed (`copies` climbing). Yet neither
  the knife nor the grenade copy shows looking down (pitch -70) or ahead, with the arms raised out of the way; `at`
  reads y 1.73-1.76 for both, and did not follow `slot_models_ahead 0.5` (capped). Next: work out the frame `at` and
  `frame_item_matrix` are in (the copies may sit at head height, inside the near plane), e.g. by placing an item at the
  drawn gun's own Wep_Root matrix and checking it lands on the gun.
- **Solved (2026-11-16, third pass; pipe `arms` now prints `palette camera/wep_root/reference`, `extramesh` each
  draw's `from_root` and a per-item `last <item> capture`, `extramesh forget <item>` recaptures):**
  - The frame was right. Palette space is relative to the root (`local[0]`), which sits on a whole-metre grid point
    (reference = -437, 397, -649), not at the feet: camera y 2.05, so `at` y 1.7-1.9 is chest height. The knife's `at`
    was 11 cm left, 17 cm below and 17 cm ahead of the eye, as asked; the grenade mirrored it on the right.
  - Cause 1: the knife's mesh hangs 0.3-0.6 m from its Wep_Root (`from_root` (-0.43 0.43 -0.18), (0.05 0.36 -0.12),
    (-0.23 0.39 -0.02) in different captures: it moves against Wep_Root with the knife animation). Placing Wep_Root at
    the holster put the knife up to 60 cm away. The grenade avoids this through `grenade_in_fist`.
  - Cause 2: a level that starts with the knife out lists the knife's own pre-pass and depth draws among the gun's
    meshes, so the knife capture dropped them ("seen with the gun") and kept the G-buffer draw alone.
  - Dead ends: the hand's fist at the capture (position, then the whole transform with a guessed fist frame) put the
    knife beside the holster or out of view; its bind origin is 12.5 cm from the fist centre, and the fist frame's
    axes against the blade are unknown.
  - Fix: extra_mesh keeps each capture's first palette entry (`item_anchor`: the knife's bone frame, bind included);
    the holster puts that frame at the holster point (`root = sheath * anchor^-1`), turned by `knife_holster_turn`
    about x (default 90: blade up, handle down, flat side out, on the monitor beside the grenade). Draws from the
    knife's own index buffer stay even when seen with the gun (`palette::keep_knife_draws`).
  - Not settled headless: -90 (blade down) left the knife out of every frame; the drawn left arm covers the area below
    the holster in these views, so it may simply be hidden. The headset decides (GRN-11).
- **Blade down, handle up, edge left (owner 2026-11-16), same day:**
  - Why -90 failed: the turn was about the knife's own origin, and the hand's grip sits 11-12 cm from it
    (`knife_grip_in_anchor` (-0.032 -0.015 0.107) and (-0.016 0.006 0.124); one odd capture gave y 0.157), so turns
    swung the knife into the body. The grip is now kept at the grab spot: measured from the fist at a capture with
    hands, otherwise the default (-0.016 0.006 0.124).
  - Orientation from the hand's hold: the knife's Wep_Root in the hand is `frame * aim * hold` (knife_pitch/yaw/roll),
    so the controller's axes in the knife's frame are `conj(anchor) * conj(hold)`. The ray points down, the hold
    half-turned about it, and a 35-degree tilt about the left-right axis makes the blade vertical: in the hand the blade
    lies about 45 degrees off the ray. Checked by floating the copy 0.5 m ahead at eye height
    (`extramesh knifeturn x y z dx dy dz`, pipe only) and shooting it from the front and the side.
  - Side view at turn 0: the bayonet's saw-tooth spine faces back, so the edge faces ahead; `knife_holster_turn` 90
    (default) turns it to the left. On the chest the knife then hangs handle up, ring guard below, blade down.
  - Flake: once in five runs the level-start knife capture took other meshes (no 6000/4380-index draws, two draws from
    another buffer kept). Drawing the knife once (`extramesh forget knife` first) recaptures it.

## Where a physical throw goes (#85, 2026-11-17, monitor)

- **Cause of the finicky throws:** the hand's velocity and position (OpenXR LOCAL) went into the grenade's shot frame
  (x left, y up, z forward along the game's aim) as if LOCAL were that frame. Every stick/snap turn and the aim's pitch
  turned the throw away, and the hand's height above the tracking origin went in as height above the eyes.
- **Now:** the game's aim is turned along the throw (weapon_swap's action aim, as the C4 in #70) for 700 ms, the speed
  is all forward in that frame and the start is the hand's offset from the eyes in that frame (`throw_along_aim`).
  Pipe `grenade throwhand <vx> <vy> <vz> [safe]` throws through this path. Monitor: LOCAL (8, 4, 0) -> game aim yaw
  +90.0, pitch +26.6 for ~800 ms.
- The grenade entity (`ServerGrenadeEntity`, vtable `0x141E685B0`) holds no position within its first 0x800 bytes
  (dead end for measuring landing points; it is behind a component).

## The quick throw selects the grenade for a moment (#83, 2026-11-17, monitor, Baku tunnel)

A physical throw fires the game's quick throw (an 80 ms LB pulse). The game then makes the hand grenade (slot 6) the
active weapon from ~50-100 ms to ~410-450 ms after the throw (game slot sampled every few ms), then goes back. Modules
that follow the active weapon took that as a weapon change: the gun hand's pose, the sling and holster copies and the
reload hands flickered (owner: "the rifle hand bugs out"). `hide_quick_throw_switch` (grenade_hands) keeps the previous
weapon for 1 s after a physical throw in weapon_swap (`holster` shows `in_hand=`) and physical_reload. At ~5 fps (headless
debug) one scripted throw in two never reached the game: the 80 ms pulse fell between frames.

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

### The M320's own reload, bone by bone (2026-11-16, monitor, #64)

The standalone M320 (`U_SP_M320_HE`, slot 5) fired once down the Baku room (free hands off, RT only; the game reloads
it by itself right after the shot). Live local pose of the weapon bones every 120 ms (scratch tool `bone_series.py`),
translations in the parent in cm, the shot at ~0.9 s:

| Bone | At rest | During the game's reload | Is |
| --- | --- | --- | --- |
| 131 `Wep_Mag` | (-1.9, 3.9, 51.3) under Wep_Root | turns 15 deg about the gun's up axis 1.1 s -> 3.4 s, back by 3.5 s | **the barrel**, pivot at its front; the back end swings to the gun's left (+x), as the real one |
| 136 `Wep_Belt1`, 159 `Wep_Extra4` | | the same 15 deg with the barrel | parts of the barrel |
| 137 `Wep_Belt2` (child of Belt1) | (0, -1, 0) | 1.1-2.8 s: back and down to (15, -34, -55), tumbling up to 145 deg, then reset | **the spent case** falling out |
| 132 `Wep_Mag_Ammo` (child of Mag) | (3.5, 0.4, -5.0) | at the shot to z +11.4 (out of sight), 1.95-3.0 s in from below-left (-13, -12, -13) to its rest | **the new round** going in |
| 157 `Wep_Extra2` | (1.5, 4.3, 46.3) | to z 67.9 at 2.8-3.5 s | not identified (a hand target?) |

Ammo: server soldier (pipe `serversoldier`) weapons `+0x828 + 8*5` -> `+0x200` -> `+0x1A0`: 1 in the gun, 11-12 in
reserve (C4 slot 2: 1 and 5). The game reloads after every shot by itself, so a reload by hand must keep the game's
count from reaching 0, as `physical_reload` does for magazines. Test pitfalls: each `pad` command lets go of the held
grip for a moment and the fake gun hand, if in a holster or pouch zone, re-grips there (C4, M67 drawn instead); with
`free_hands 0` and no grip the trigger works alone. Weapon keys need BF4 in front (`game.ps1 keys -Keys 4`); it stays
on the M320 while BF4 stays in front.

### The M320 reloaded by hand (2026-11-16, monitor, #64)

`weapon/break_open` (state) and `physical_reload`'s launcher path (`profiles::is_break_open`: names starting
`U_SP_M320`); design after The Great Destroyer's `src/m320.h`. The game's count is kept at ours + 2, so its own reload
after each shot never starts; the trigger reaches the game only when shut and loaded. A pull while not ready opens the
barrel (drawn over 0.12 s: Wep_Mag, Wep_Belt1, Wep_Extra4 turned up to 15 deg about the parent's up axis); the spent
case slides out once the barrel points 30 deg up (falls 0.8 s from the mouth, then hidden) or is pulled out by an
off-hand grip within 8 cm of the barrel's back end (the round's bone, `weapon_points::barrel_mouth`); the off-hand grip
at the magazine pouch takes a round (drawn at the palm), which goes in within 5 cm of the back end; the open palm
within 5 cm of a point 5 cm to the barrel's left and 10 cm on (`barrel_side`), after being 12 cm away since it opened,
shuts it. `pipe reload` prints the launcher's state, points and recent events.

Monitor (fake hands, free hands off): fired (game 3 -> 2, no game reload), the next pull opened it, muzzle 50 deg
up dropped the case, a round from the pouch (reserve 11 -> 10), seated, shut, fired again. Standing at the gadget
stash the game raised the reserve again by itself (a resupply). Not judged: how the open barrel, the round in the hand
(placed at the palm with the gun's rotation) and the falling case look; sounds are the synthesised magazine cues.

- **The keyboard is Raw Input**: the image imports `GetRawInputData` / `RegisterRawInputDevices`; no
  DirectInput keyboard. Synthetic keys work without focus: `hooks::press_game_key` posts `WM_INPUT`
  with a marker handle (`0x7BF5A000`+n) and the `GetRawInputData` hook answers it (pipe `key <vk>`).

## The free hand kept out of the gun (#62, 2026-11-16, monitor, Baku, SCAR-H)

- `Wep_Root` is not at the grip: the SCAR-H's `Wep_Mag` (magazine top) sits at (0, -0.009, 0.465) in its axes, so
  the gun's boxes (body/hand_collision `gun_boxes`) are built from the bones (magazine top, the animated hand
  wrists), never from fixed numbers. Receiver from that: centre (0, 0.026, 0.396), half (0.025, 0.035, 0.149).
- `Wep_Mag` moves with the hand during a physical reload: it is sampled only while no magazine is detached.
- Monitor: `fakehand` left at x 0.15, z -0.45, raised y -0.25 to 0: from y -0.12 the palm (r 3.5 cm) is held
  at gun y -0.044 (the receiver bottom minus the radius) while the controller goes 89..209 mm in; the solved hand
  is 0 mm inside (`fingers`: `drawn_mm=0`). A hand that went through from a known side is held up to 0.35 m
  (a first build let go at 0.15 m: shorter than the receiver plus the palm, so slaps went through).
- The off hand near the magazine engages the magazine snap (#40): the push pauses there, but the side the hand is
  on must still be followed (a first build forgot it, and a slap from there went through).
- The game's reload (reload = button) moves `Wep_Mag` down: a sampling window of 2 cm crept along with it
  (-0.009 -> -0.043). Kept now: the highest magazine top and the furthest-forward support wrist per gun.
- Fake-hand sweeps sideways at y -0.25 never touch: that height is the magazine's, 15 cm below the magwell.

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

## Charging handles, every crate gun (2026-11-17, monitor, tools/handle_probe.py)

The 11-03 method automated: own crate gives each gun, `reload = button`, `arm_ik = 0`, pipe `reload setammo 0` (research)
starts the game's empty reload, the local pose is read as fast as the pipe allows (~2500 reads/s, the game animating at
~5 fps) and forward kinematics give Wep_Slide and the left hand (thumb tip and index middle) in Wep_Root axes. The handle:
the median, over every frame of the pull, of the hand moved forward by the bolt's travel (the hand riding the handle).

- **Check:** AK-12 (-0.027, 0.056, 0.527) against the hand measurement (-0.02, 0.06, 0.53): 0.7 cm.
- **Taken (18, `profiles.cpp`):** a closed pinch with <= 3 cm spread (75 % of frames), or the hand riding the bolt rigidly
  (max spread <= 4 cm over >= 100 frames): Scorpion, JS2, MX4, PDW-R, PP-2000, Hawk (the pump), Saiga, USAS-12, A-91,
  ACE 52, G36C, FAMAS, SAR-21, Ultimax, QBU-88, RFB; SCAR-H SV takes the SCAR-H's estimate.
- **No pull in the empty reload (bolt release):** M4, M16A4, M416, MK11, SG553, AUG, UMP-45. **Bolt moved, hand away (own
  cycle):** FY-JS, Scout, SV-98, M82A3. **Too loose or too few frames:** CBJ-MS, P90, AK 5C, AEK-971, CZ-805, QBZ-95-1,
  QBB-95-1, M39, SVD-12 (its animation matches the AK-12's exactly), SRR-61. These keep the variant's default point.
- Dead end: requiring a closed pinch (thumb-index < 4.5 cm) dropped guns whose animation grabs with a wider hand.
- Seen twice: the mod's first-person skeleton read failed (`arms` skeleton=-, reload `points_valid=0`). Once after a run
  of gives; on 2026-11-17 straight after a `dev-run -Resume` load, before any give: animatable+0x230 held no skeleton
  (its "bone count" unreadable) and nothing in the component or animatable pointed at one. Loading the level again
  (`dev-run -NoBuild -Resume`) fixed it.

### Probe v2: raw frames, magazines, bolt actions (2026-11-17, monitor, Baku tunnel, ~10 fps)

`handle_probe.py record` saves every changed frame (Wep_Slide, Wep_Mag, both hands' pinch) per gun to
`re/dumps/gun_points/` (git-ignored); `analyse` re-reads them, `timeline --file <gun>.<event>` prints one. A hand
"rides" a part only on frames where both moved > 8 mm since the last frame, the same way within 1.2 cm.

- **Why the stricter test:** the game locks a hold-open gun's bolt back the moment `setammo 0` empties it, so on the
  SCAR-H the bolt sat back for the whole magazine swap and v1's "hand within 15 cm while the bolt is back" took the
  hand on the magazine (11 cm too low). The SCAR-H's empty reload ends with the left hand slapping the bolt release
  beside the magwell (t = 1.9 s): no handle pull.
- **`setammo 0` needs a fresh give:** on a gun that had been in hand for a while, nothing animated; right after
  `givegun` it always did.
- **Handles:** v2 reproduced the Hawk, Saiga, QBU-88, Ultimax, PDW-R and AK-12 points within 1 cm (JS2 3 cm) and
  added CZ-805 (0.055, -0.009, 0.549) and M39 (-0.102, 0.013, 0.428). Bolt actions from one shot (`--event shot`,
  pad RT): the game cycles the bolt with the **right** hand; knob SRS (-0.084, 0.116, 0.243), FY-JS (-0.097, 0.097,
  0.345), Scout (-0.085, 0.116, 0.439); the SV-98's hand never moved with its bolt. Still the variant's default:
  CBJ-MS, MX4, QBB-95-1 (hand spread 9-14 cm), P90, AK 5C, AEK-971, QBZ-95-1, SVD-12, SV-98, M82A3 and the
  bolt-release guns.

## Magazine grab points (2026-11-17, monitor, tools/handle_probe.py v2)

The left hand's pinch while it moves with Wep_Mag on the magazine's first 25 cm out, as an offset from the seated
Wep_Mag (Wep_Root axes, +x left). Taken for 24 guns (>= 4 frames within 6 cm, `magazine_grab_for` in `profiles.cpp`):
long boxes 8-9 cm below (M4 family, AK 5C, G36C, UMP-45), most 4-6 cm, the bolt actions' short boxes 1-2 cm, the
QBB's drum at its middle. The SCAR-H's hand is 3 cm to the left. Too few or loose frames (default 8 cm below):
CBJ-MS, MX4, PP-2000, SAR-21 (no hand moved with it), P90, PDW-R, AUG, ACE 52 (2-3 frames), A-91, AEK-971, CZ-805,
M82A3, USAS-12, Ultimax, Scorpion (spread 7-11 cm), SRS (4.9 cm but above the bone: doubtful).

Way out (the magazine's first 3 cm, unit vector): AUG (0.54, -0.83), QBU-88 (0.53, -0.84), RFB (0.35, -0.93),
SAR-21 (0.31, -0.94) seemed to swing out to the left, but that sideways part is the animation's hand carrying it away:
a magazine cannot leave its well sideways. **In the gun's mid-plane** (angle from straight down, + forward, at 3 and
6 cm out) almost every gun reads within 10 degrees of straight down, the AK-12 10/5 and the SCAR-H 1/4 among them, so
their straight rail (owner 2026-11-17: the AK and SCAR feel right, the reference) is the game's own. Clearly off and
steady: ACE 52 17/17, Saiga 26/38, QBB-95 26/25 forward; MX4 -11/-13, PDW-R -12/-12 back; P90 -102/-102 (back off the
top). Unsteady, left straight: SG553 29/-6, UMP-45 17/-6, Scout -14/3, G36C -3/-15, FAMAS -4/-14, QBU-88 8/17.

Used since 2026-11-17 (`magazine_way_out_for`, setting `gun_points`): the rail the magazine rides out and back in,
the drop when released and the AK knock-out follow the gun's way out; a gun without a measured grab point has its
grab zone along it (the P90's behind its magazine instead of inside the receiver). The Great Destroyer does the same
in its own way (`src/handmag.cpp`): button guns slide straight down until the top clears the well, rock-in guns turn
up to 20 degrees about the front hook over 5 cm.

Dead end, 2026-11-17: the empty reload does not start while BF4 is behind another window (11 fps, `setammo 0`
accepted, 2-6 changed frames in 7 s); in front it records 50-300. `tools\game.ps1 focus` brings it to the front.
Also since then: "ahead of the magazine" (the handguard guard, `magazine_ahead_limit`) counts from the grab point,
not the magwell: the AK-12's measured grab is 3.9 cm ahead of its magwell, which the old guard called handguard.

Monitor 2026-11-17 (fakehead, fakehand on each gun's zone, pad grips, B): AK-12 out at its grab point, rides the
straight rail 6 cm; SCAR-H the same (twice); Saiga out, a 6 cm straight-down hand move reads 5.1 cm on its 32 degree
rail. **P90, found and fixed:** taken 16 cm behind its magwell, its magazine came out and seated again within 250 ms.
The re-arm (`rearm_distance` 18 cm) measured the drawn fist, which eases from the controller onto the locked
magazine for a few frames: it read > 18 cm, armed, and seated when the hand arrived. Since then a magazine taken out
of the gun re-arms by where the controller carries it (`from_gun_`). After the fix it stays in the hand. The P90 also
had no handle of its own: the AR default sat 2 cm from its magazine grab, so a hand there was the handle's.

Handles from the moment before the pull (`handle_probe` `pull_start`): the hand on the handle in the last frame before
the bolt leaves home, if it then follows the bolt. At ~10 fps the pull is one or two frames and the hand trails the
bolt by ~2 cm, which the "moves with" test rejects. It gives the AK-12 within 1 cm and the FAMAS within 0.4 cm and
added the P90 (0.049, -0.004, 0.46: left side, forward), QBB-95 and SVD-12 (the AK-12's point).

### Tube shotguns from the game's own reloads (2026-11-17, `handle_probe --all-bones`, re/dumps/tube_points)

Every Wep_ bone recorded through the empty reload and a shot of the 870, M1014, SPAS-12, UTAS and QBS-09. On these guns
**Wep_Mag is the shell**: at rest it sits in the loading port (870 and SPAS-12 (0, 0.067, 0.486), UTAS (0, 0.112, 0.282)
behind the grip, QBS-09 (0, 0.082, 0.461)); during the reload it comes up from below and is pushed forward into the
tube about every 0.75 s (Wep_Mag_Ammo and Wep_Mag_Extra1 move with it). The 870's empty reload starts with the pump
back (Wep_Slide 7.6 cm, the left hand on it), the first shell dropped into the open ejection port (2.7 cm above the
loading port), the pump closed, then the tube. Fore-end (left hand at rest): 870 (-0.002, 0.065, 0.725), SPAS-12
(0.007, 0.078, 0.721), UTAS (0.003, 0.070, 0.696); pump travel 7.4-9 cm. The QBS-09's Wep_Slide is its bolt on the
right (-0.03, 0.121, 0.478), locked back as soon as the tube is empty and closed by a release, no hand on it; the
M1014 moves Wep_Extra1 instead. Capacity read live: the 870's tube holds 7 (`reload` capacity=7).

**Found, 2026-11-17 (monitor):** the Hawk's measured handle is its fore-end, so gripping it there to hold the gun
started a rack (`racking=1 busy=1`) and the two-handed hold never came (`held=0`). Pump guns now pump with the hand
that holds the fore-end (`hand_frame::pump`): no rack zone there; the action unlocks after a shot (or on an empty
chamber), a stroke back past `rack_pull` and forward again cycles it. After the change: `held=1 weight=1.00`.

### The MP-412 REX and the M249 in the soldier rig (2026-11-17, `handle_probe --all-bones`, raw local transforms)

**Revolver: the frame is Wep_Slide** (found the same day; the first pass missed it). The game's empty reload breaks the
REX open (top break), throws the six cases and seats a speedloader. Wep_Scope1/2 never rotate (their position jumps at
0.61 s: a relocated reference), and Wep_Slide never moves, so the first pass (`--all-bones` positions plus the extra
bones' locals, Wep_Slide's travel only) called the frame "not in the rig". Every bone's local rotation through the
reload (all 179, arms left out) shows it: **Wep_Slide turns 85 degrees in place** about +x, q (0.675, 0, 0, 0.737),
its local position (0, 0.026, 0.137) the hinge: the barrel tips down. Open at 0.68 s, held to ~2.8 s, shut by 3.3 s.
The cylinder is Wep_Physic1 (0, 0.041, 0.126), turned with the frame (and spun); Wep_Physic2 flies off and is scaled
to 0 (a case). The six cartridges are Wep_Belt1-6 (Belt1 under Wep_Root, the rest a chain): thrown ~2 m at ~1.0 s,
then seated in the open cylinder at (-0.007, 0.065, 0.141), turned (0.264, -0.622, 0.678, 0.289); the speedloader is
Wep_Mag, from the right side up to (0, 0.098, 0.144). Dead end: scanning memory for other Animatables (vtable
141CD6F40) found 21, none with a weapon skeleton; the weapon components' animatables hold no skeleton at +0x230.
Lesson: look for a turning bone by its local rotation over the whole rig, not by position.

**Built and checked live 2026-11-17 (`revolver_by_hand`, fakehead/fakehand):** two shots (6 -> 4), B broke it open
(`lid_opened`, the round under the hammer back in the cylinder, the frame drawn fully open), a speedloader of 6 from
the pouch at the open cylinder filled the two empty chambers and put 4 back (reserve 12 -> 16), a 35-degree flick of the
gun hand shut it (`lid_closed`, a round under the hammer), the trigger fired. The speedloader's seat is published by
arm_pose as the turned cylinder's rear face (Wep_Physic1 4.7 cm back), not edit_weapon's Wep_Mag (8 cm off when open).
The screenshot of the open frame was too dark to judge: the look of it is for the headset (GUN-39).

**M249, workable:** the lid is Wep_Extra1, hinged at (0, 0.058, 0.475) (the front of the feed tray), turned up by
q (0.781, 0, 0, 0.625) = 102.6 degrees about the gun's x axis; Wep_Scope1 (the optic on the lid) turns with it: its
open position is its rest turned 102.6 degrees about that hinge (checked: (0.075, 0.295) -> (0.230, 0.531)). The belt
is Wep_Belt1-10: Belt1 at the feed tray (-0.006, 0.051, 0.418), the chain running left and down into the box
(Belt5-9 at x 0.074); the game's hand lays it at 4.3-6.2 s. The box is Wep_Mag (0, -0.03, 0.44), the charging handle
Wep_Slide on the right (-0.027, 0.021, 0.466). The lid opens at ~3.4 s, after the box change.
All five belt-fed guns share the layout (Wep_Extra1's local position is each one's hinge): lid fully open M249, M240,
MG4 102.6 degrees, Pecheneg 93.7, Type 88 81.7; the Pecheneg's belt runs off to the right (-x), as a PKM feeds.

**Built and checked live 2026-11-17 (`belts_by_hand`, M249, fakehead/fakehand):** the lid taken at the optic and swung
~60 % up opened (`lid_opened`; the round on the tray back on the belt, 32 -> 33), stayed up when let go, the trigger
did nothing; the belt taken at the tray, carried below the box mouth, brought back: `belt_laid`; the lid pushed down to
3 % and let go shut it, and the trigger fired five shots. Pitfall: reading the tray right after firing gave a point
8 cm off (the gun still moving from the burst): read points just before using them.

### Bolt actions and the Hawk's pump by hand (2026-11-17, `manual_cycle`)

The game cycles a bolt action with the **right** hand after each shot (shot recordings): the hand takes the knob,
lifts it (SV-98: the knob from (-0.064, 0.042, 0.402) up to (-0.048, 0.111, 0.406)), rides the bolt back ~10 cm and
forward. Knob points: SRS, FY-JS, Scout from the riding frames, SV-98 read off its timeline. In the mod a shot on these
guns and the Hawk leaves the chamber empty without feeding (`reload_profile::manual_cycle`): the existing trigger block
(`block_trigger = !chamber`) holds the next shot until a rack at the knob or fore-end feeds it. The mod counts shots
from the game's ammo, so the game's own (automatic) bolt animation and timing stay as they are underneath.

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

**The user's verdict on that run:** the AK-12 with the magnifier works as intended, best at the left
eye, with a white glitch at the bottom of the view; the AK-12's red dot alone shows a dot and circle
that are not centred and sometimes partly cut off (that is the game's own reticle, drawn at the view's
centre and masked by the lens: it shows now that the game really aims); the SCAR-H's holo: no change, no dot.

What follows from it:

- The SCAR-H's dot went to the right eye in both sessions (`dot_seen=0/1`) and was never seen; the only dot
  ever seen in a capture was a left-eye one. Whether SteamVR shows `XR_EYE_VISIBILITY_RIGHT` quads through
  Steam Link is therefore open. `sight_dot_eyes = both` draws one quad for both eyes (no single-eye layer)
  to decide it; VR View can also be switched to the right eye or both.
- `ipd_scale` (1.15 in that session) moves the rendered eyes apart, 0.5 cm each: the sight checks now use
  the rendered eye (`stereo::rendered_eye_position`), since that is where the drawn lens is seen from.
  Monitor with `ipd_scale 1.15`: `eyes off_line_cm=7.0/0.4` (was 6.5/0.1 with the runtime's eyes).
- Monitor, right eye on the line, magnifier in, two-handed: at the hip the eye looks into the magnifier
  (in line, lens dark where the scope quad goes). **In the game's ADS the right eye's image has a ring
  and mount about 210 px right of the sight and a second piece of the gun at the bottom right** (`eyeshot2`),
  where the left eye's image has the gun: parts of the gun are drawn at the first eye's place in the
  second eye while the game aims through the magnifier. Not followed up; a candidate for "best at the
  left eye". The white glitch at the bottom did not show on the monitor (asked for a mark next time).
  With the scope render on, two dark slivers appear on the wall below the gun and the window's glare changes.

**Second headset run, 2026-11-12 (12 marks, VR View on both eyes).** User: the green dot now turns red;
the grab rings got in the way (off from mark 9).

- **The red dot on the wall is the SCAR's laser** (`U_SCAR-H_TargetPointer`), not the mod's dot: it is in
  the marks' own game image too. It marks the game's aim (the body, `aim_with_body`), not the drawn barrel:
  on the monitor with the head turned 30 degrees it sits at the view's centre, 30 degrees from the barrel.
  It misled the first reading of these captures.
- **The mod's dot is shown by the compositor**, in both eye images, with `sight_dot_test 1` and
  `sight_dot_eyes both` (marks 8-12; the AK-12 has no laser, so its single red spot is the mod's). It is
  about 2 px in VR View's 830 px eye image. "The green dot turns red" is that test mode: the dot is
  always on, so the green one hides. With `sighting` and no test (marks 2-3) the captures, taken ~1.3 s
  after the tap, show no mod dot: undecided whether a right-eye-only quad is shown.
- **The mod's layers sit 15-20 degrees off the drawn optic in the right eye's image**: mark 7, same
  composited frame, the magnifier picture is ~11 degrees right and ~13 below the drawn magnifier (and has
  a white wedge at its lower edge: the "white glitch"); marks 10 and 12 have the dot the same way off the
  SCAR window and the AK sight. Run 1's mark 7 (left eye) had the picture down-left by as much.
- **Not reproduced on the monitor**: with the right eye on the sight line the SCAR's window is drawn where
  the barrel points with the head turned 30 degrees (32.3 seen, 30 x gun_scale 1.1 expected), pitched 25
  down (27.5 seen) and rolled 30 (centred), `ipd_scale 1.15`. `gun_scale` does move the drawn optic 10 %
  further from the view's centre than the mod's point (2.3 degrees at 30 off axis).
- So either the hold moved between the tap and the capture, or the layers lead the picture while the gun
  moves (quads use the newest pose, the eye images are frames older), or something only a real head
  sample has. Next: `grab_debug` now draws a small ring where the mod takes the lens to be
  (`grab_slot::sight_lens`, green while an eye sees the dot): held still in the headset it must sit on
  the optic. The capture script now keeps VR View frames from before each mark.

Test pitfalls: `pad` buttons replace the grips, so a fake gun hand needs `200` held or the gun is slung
(`free_hands`); `ctest` has only debug presets (`bf4-debug`, `bf3-debug`).

**Third headset run, 2026-11-12 night (no marks, VR View closed), `gun_scale` and `ipd_scale` 1.** User: no
large change; the AK's ring still shows to the right of the gun; no red dot but the mod's own (also at the
hip: `sight_dot_test` was left on and is saved as 1 in `bfvr.ini`, with `sight_dot_eyes both`); grab
distances felt off with the sizes at 1. Whether the lens ring sat on the optic was not reported
(`grab_debug` was saved off).

**Zoom meshes (typeinfo, static, 2026-11-12).** `WeaponMesh1p` has `Mesh` and `MeshZoom`;
`WeaponSocketObjectData` has `Used1p` / `Used1pZoom`; `WeaponStateData +0x48 ZoomMeshTransitionFactor`,
`+0x50 ZoomInOutMeshTransitionFactors`; `SocketData +0xD4 HideByZoomTransition`. The game swaps the weapon's
and each attachment's mesh part-way through its zoom. That would be the AK lens's two states and the
general switch for every optic. Not followed live yet; plan in `docs/tasks/sights-ads.md`
("Direction from 2026-11-12").

## The zoom mesh at the hip: one data field for every weapon (2026-11-12, monitor, #39)

Build: the deployed `test/headset-48-49` debug build (4 fps without SteamVR), SCAR-H with the EOTech,
fake head, right eye on the sight line (`eyes off_line_cm=6.4/0.0`), `aiming=0`, `game_fov=45.00`
throughout: the game never aimed.

| `WeaponStateData.ZoomMeshTransitionFactor` (SCAR-H state 0, `0x1777dc750` that session) | Right eye's image (`eyeshot2`) |
| --- | --- |
| 1.0 (the data's value) | Window clear, **no reticle** (only the laser's spot through it) |
| **0.0** (`pipe poke`) | **The game's holo reticle in the window** (ring arcs and three dots), and a **second copy of the sight about 250 px to the right** |
| 0.0, left eye (`eyeshot`) | One gun, no copy |
| 0.0, head turned 8 degrees, right eye still on the line | Window empty again, the copy still to the right |

So:

1. **The switch is data and is read live.** With the factor at 0 the game shows its zoom mesh (the optic's
   lens state and its own reticle art) without aiming: no LT held, no slower walk, no FOV change. The
   same field exists for every weapon (`SoldierWeaponData +0x88` `WeaponStates`), so nothing is keyed on a
   gun or an optic. `weapon/hip_accuracy` already patches weapon data the same way (keep originals, write
   back at off).
2. **The copy is the second eye's bug**, and is what the owner saw as "the ring to the right of the gun":
   with the zoom mesh shown, one of the two meshes is drawn at the first eye's place in the second eye
   (not in the first eye's own image). It also shows in the game's own ADS (2026-11-12, AK-12 magnifier).
   To find: which draws those are (`meshprobe` with the factor at 0) and why `eye_pass` does not move their
   per-view block.
3. **The reticle still follows the view's centre**: 8 degrees of head turn empty the window. Needs the
   reticle's placement (the zoom mesh's lens draw and its constants).

Not checked: the AK-12 (not carried in this checkpoint state; its black lens should be the hip mesh and
clear the same way), a factor between 0 and 1, the per-zoom-level `ZoomInOutMeshTransitionFactors`
(empty arrays on the SCAR-H), and whether other soldiers share the data asset.

**In the mod (`weapon/zoom_mesh`, setting `sight_zoom_mesh`, default on).** While an eye is behind the
sight (the same 3 cm / 5 cm + 0.3 s latch as `sight_ads`, but whatever the game's aim does) the factor of
every state of every carried weapon is 0; the data's values are kept and written back afterwards, and
only where our value still stands. Pipe `zoommesh [0..1|off]` reports and, for research, holds a value.
Release build, SCAR-H, right eye on the line, the game not aiming:

```
(gun away)   zoom_mesh=1 at_eye=0 forced=-1.00 patched=0 writes=0
(eye on line) eyes off_line_cm=6.4/0.0 ... aiming=0 ; scope game_fov=45.00 ...
             zoom_mesh=1 at_eye=1 forced=-1.00 patched=4 writes=4
             00000001787D7240 factor 0.00 (game 1.00)   (+3 more)
(gun away)   zoom_mesh=1 at_eye=0 forced=-1.00 patched=0 writes=4
```

`eyeshot2` with the eye on the line: the holo's ring and three dots in the window.

**The reticle is one draw.** `meshprobe` with the factor held at 1 and at 0 (same pose): the frames differ
in three skinned draws of 276 / 72 / 36 indices (the lens parts, other index buffers), two static ones of
7200 / 1260 and the frame's last draw: **6 indices** (a quad), VS `...6846F8`, PS `...183A78`, into the
final 2160 target, depth-stencil state shared with the zoom mesh's static parts, **stencil ref 2**, its
per-view block (32 constants) in the late constant buffer. `eyehide` with that pixel shader removes the
reticle and nothing else (the 2026-11-10 note "not one draw" was the AK-12 in the game's ADS; not
rechecked). So the reticle is a quad on the view's axis, cut to the lens by stencil.

Plan for the placement (not built): for that draw only, in each eye's render, a view turned so that its
forward axis is the sight line as that eye sees it, and without the second eye's shift, so the reticle
is collimated. The draw is to be recognised without names: it is what the frame gains in the final
target when the factor goes to 0 (learned per weapon, like the magnification), or by its stencil
reference and state. Needs a per-draw constant override in `stereo/eye_pass` (today blocks are patched
when bound, by layout).

**Headset 2026-11-12 evening (owner):** the holo sight's reticle seen for the first time; it follows the
head and does not stay in the optic.

**The reticle on the sight line (`sight_reticle`, default on; `stereo/eye_pass`, pipe `reticle`).**
Release build, SCAR-H, zoom mesh shown, fake head.

How the game draws it (pipe `reticle dump`, 413 stencil-tested draws of ~640 indexed in a first-eye render):

- A late pass of three blended draws without depth write, all "stencil reference 2, read mask F0, function
  GREATER_EQUAL, pass REPLACE, depth LESS_EQUAL" (the state of 347 other draws: no lens mask): 150 and 18
  indices from the gun's own index buffer (the holo's glass; the 18-index one is the tinted octagon) and
  the **6-index quad: the whole reticle, ring and dots**. A second 6-index quad earlier in the frame
  writes no colour.
- The quad sits at the lens, rigid with the gun. Its picture does not: the pixel shader takes the
  pixel's position times the per-view block's **inverse screen size (`+0x18`, `+0x1C`; `+0x10` / `+0x14`
  are the size)** and anchors the reticle where that is 0.5, the screen's centre. Hence "it follows the
  head": 8 degrees of head turn put the picture beside the quad.

How that was found (head turned 8 degrees, eye on the line, the window 141 px right of the centre):

| Change | Seen |
| --- | --- |
| none | window empty |
| the quad's viewport moved -141 px | the whole reticle, uncut, on the wall at the screen's centre |
| the 18-index draw's viewport moved | a dark octagon moves: the glass, not the dots |
| the quad drawn without stencil test, without depth test, without both | window still empty: no test hides it |
| view rows of every per-view block turned +-8 degrees (second eye) | nothing for the reticle (other draws break) |
| screen width times 1.13 or 0.885 | nothing |
| **inverse width times 0.885** | **the reticle in the window** |

The fix: for a draw that is "6 indices, stencil reference 2, blended, colour written, no depth write",
in both eye renders, the per-view block bound to PS (and VS) slot 2 is copied on the GPU into a buffer of
the mod's (the game's buffer cannot be read on the CPU during the first eye), the two inverse-size floats
are replaced by `1 / (width + 2 dx)` and `1 / (height + 2 dy)` with (dx, dy) the sight line's place on
screen from `weapon/two_handed` (the line in the rendered eye's view space, `camera_hook`), the quad is
drawn and the game's binding restored. The same anchor in both eyes: collimated.

```
straight R: eyes off_line_cm=6.4/0.0 | reticle valid=1 tan=(-0.0000 0.0000)
yaw8 R:     eyes off_line_cm=6.4/0.0 | reticle valid=1 tan=(0.1405 -0.0000)
yaw8 L:     eyes off_line_cm=0.0/6.3 | reticle valid=1 tan=(0.1405 0.0000)
yaw-8 R:    eyes off_line_cm=6.4/0.0 | reticle valid=1 tan=(-0.1405 -0.0000)
pitch-6 R:  eyes off_line_cm=6.4/0.0 | reticle valid=1 tan=(-0.0000 0.1051)
pitch+6 R:  eyes off_line_cm=6.4/0.0 | reticle valid=1 tan=(0.0000 -0.1051)
reticle ... draws=7691 moved=7465
```

In all six the ring and dots sit in the window (`eyeshot` / `eyeshot2`).

Limits: moving the anchor through the inverse size also scales the picture on that axis by
`1 / (1 + 2 dx / width)`: at 8 degrees the ring is about 13 % wider or narrower. The rule is the SCAR-H
holo's; other optics may draw their reticle with another index count (the AK-12's and scopes unchecked).
An earlier reading in this file, "the reticle is not one draw" (2026-11-10), was wrong for this optic.

**The stray piece in the second eye** came and went between captures of the same pose in the Release
build (a dark piece right of the gun in one, none in the next two); in the debug build at 4 fps it was a
full second sight. Not explained. The per-view blocks of a dumped frame (`eyeprobe`) are of two cameras
only, the world's and the first-person one in soldier space, re-uploaded many times late in the frame.

Test pitfall: a relative path given to `eyeshot` is written from the game's drive root (`E:\`).

## In-frame lens, step 0 (2026-11-13, monitor, Release, `fix/sights-in-frame`, #39)

Brief: `docs/tasks/in-frame-lens.md`. The saved checkpoint now carries only the SCAR-H (`slot_probe.py`:
0 `U_SCAR-H`, 6 `U_M67`, 7 `U_Knife`, 8 `U_SP_Visor`), so the AK-12's lens and the magnifier were not checked.

**Fake hold with the right eye on the SCAR-H's sight line** (`free_hands 0`, `fakehead 0.064`):
`fakehand 0 0 -0.003 -0.189 -0.60 0.075 -0.197 -0.3` gives `eyes off_line_cm=6.4/0.0 along_cm=-13.2`;
`... -0.70 ... -0.4` puts the gun 10 cm further out (along -23.2). The AK-12 hold above (`-0.024 -0.158 -0.60
0.054 -0.166 -0.3`) leaves the SCAR's line 3.8 cm from the right eye.

**The holo reticle is collimated** (`sight_zoom_mesh` and `sight_reticle` on). Eye fixed, gun moved sideways
by 0, 0.7 and 1.4 cm (`eyeshot2`, window cropped): the ring and dots stay at the same pixel while the window
slides left, as through a real holo. At 1.4 cm the dots are drawn just outside the window's inner edge, on the
housing: the stencil-2 lens mask reaches past the glass.

**The second eye's first-person draws change between frames.** With the gun, head and hands fixed, the lower
half of 6 `eyeshot` / `eyeshot2` captures (120x120 downscale, mean absolute difference against the first):

| Case | Left eye | Right eye |
| --- | --- | --- |
| Zoom mesh and reticle on, along -13 cm | 0.1 0.1 0.1 0.1 0.1 | 4.3 4.4 0.2 2.1 4.3 |
| `sight_zoom_mesh 0`, `sight_reticle 0` | | 0.1 0.1 4.4 0.1 0.1 |
| On, gun 10 cm further out (along -23 cm) | | 7.1 5.8 1.8 2.0 0.3 and 1.8 x5 |

The odd right-eye frames show a **second holo housing about 165 px right of the real one**, the gun body
drawn dark (unlit) instead of lit, or the hands in another place. It also happens with the zoom mesh and the
reticle fix off and with the gun 23 cm from the eye, so it is the eye pass, not the sights or the near
plane. This is the "ring to the right of the gun" and probably part of "best at the left eye".

**Lead: constant arenas are discarded during the eye renders.** A research counter (`eyepass`:
`arena_discards eye1= eye2=`, maps with `D3D11_MAP_WRITE_DISCARD` of a known arena while `in_eye_render`)
reads about **2 per first-eye render and 1 per second-eye render**, every frame (9401 frames: 18799 / 18415).
The first eye's blocks come from `begin_readback`, a `CopyResource` of each arena queued **before** the first
eye's draws. A block the first eye binds after a mid-render discard sits in the renamed arena, while the
read-back holds the pre-discard data at the same offset. `prepare_eye_blocks` then patches the wrong data, and
the second eye draws that piece with another camera (or another view's block). Which blocks fall after the
discard varies with how full the arena is, so it would come and go. **Not proven:** the next step is to count
first-eye bindings made after a discard of their arena, and to fix by copying such blocks at bind time on the
GPU (`CopySubresourceRegion` of the block from the arena into the mod's buffer, ordered in the command stream),
or by keying `patched_blocks` with an arena generation. The check is the table above: the right eye as steady
as the left (all values <= 0.2).

Test pitfall: `tools\pipe.ps1` takes the command's argument as one string: `pipe key "32 150"`, not `pipe key 32 150`.

**Steps 1 and 2 (2026-11-13 evening, Release, the red room again, the AK-12 now in the save):**

- **Stale second-eye lookups exist:** a new counter (`eyepass` `stale_after_discard`) counts second-eye binds that
  a copy made before an arena discard would answer: 637 in about 5 s, 4169 over a session. `eye_pass` now keys
  second-eye lookups by the arena's discards in this render (`discard_fix`, pipe `eyefix 0|1`, on by default),
  so such a bind takes the late path (the block as written now). Cost: none measurable (eye pass 1.36 ms,
  GPU 4.6 / 4.6 ms per eye). **The stray did not show in either mode here** (6 shots each, AK-12 and SCAR-H,
  lower-half differences 0.1-0.3): not proven to be the cause, kept because a pre-discard copy is wrong by
  construction.
- **The AK-12's lens without the game's aim:** with `sight_zoom_mesh` on and `sight_ads 0` (`aiming=0`) the lens is
  clear with the red dot, as in the game's ADS. The dot is the game's reticle re-anchored by `sight_reticle` and it
  is collimated: gun moved 0 / 0.6 / 1.1 cm off the eye, the dot stays at the same pixel (2 px) while the lens
  slides, the rim cutting it at 1.1 cm. So the AK-12 needs no game ADS either; `hip_accuracy` keeps the accuracy.
- **The magnifier (selected 2, type 2) with the zoom mesh:** the game hides the magnifier's mesh and shows the red
  dot's lens unmagnified (its own ADS magnifies by the camera's FOV, 27 degrees, which the mod does not use). Our
  scope render runs at the hip (1.725x, `scope_images` flowing) but is an OpenXR quad. With the zoom mesh off the
  lens is black and the magnifier's ring shows to the right of the sight in the right eye. Once, with the zoom mesh
  on, the first right-eye capture after flipping it in also had that ring: intermittent again.
- **Step 3, built (`scope_in_frame`, on by default):** the composed scope picture (round, premultiplied, with
  its dot) is drawn into each eye's finished image right after that eye's render, before it is copied out: a
  quad at the lens's LOCAL pose (the one the OpenXR quad used), seen from that eye (the first eye's pose from
  `camera_hook`, the second moved right by the eye shift), projected with `projection_sx/sy` as the reticle
  fix does. The theater leaves its scope quad out while it is drawn (`scope_drawn_in_frame`).
  - Monitor, AK-12 magnifier at the right eye, `sight_ads 0`: the magnified wall fills the lens in the right
    eye and sits on the magnifier housing in the left eye, at the scene's own brightness. 93.2 fps with it,
    93.8 without; the scope render 1.26 ms of GPU.
  - Dead ends on the way: drawn at the reticle quad's draw (inside the eye render) the picture came out nearly
    white (the frame is not tonemapped there); and the reticle's stencil state (reference 2, read mask F0,
    GREATER_EQUAL) is not a lens mask, it passes the world too. `VSSetConstantBuffers1` needs a multiple of 16
    constants: with 4 the binding was ignored and nothing showed.
  - Open: no occlusion (the housing does not cover the picture's edge; the quad is the lens's size); the left
    eye has the game's own stray lens picture left of the optic while the magnifier is selected (not the mod's:
    it did not move with `lensfix`); the picture is one frame old.

## The BF3-style lens on the game's reticle quad (`sight_lens`, 2026-11-13 night, monitor, #39)

Headset 2026-11-13 evening (owner) on the screen-space reticle (`sight_reticle`): sized right only at the game's
ADS eye distance (far too large with the gun held out), turning with the head, and the clear lens only within
3 cm of the sight line (the zoom mesh's switch). All three come from the game drawing its reticle in screen space
for its own ADS camera.

**What the reticle quad reads** (pipe `reticle grab <folder>`, SCAR-H EOTech and AK-12 PK-AS): ps `t3` 128x128
RGBA, **alpha = the reticle art** (EOTech: ring, four ticks, centre dot, two faint dots below; RGB white); `t4`
864x864 R11G11B10 (a small scene render, the lens reflection); `t5`-`t7` BC5 glass detail maps; `t1`, `t2` cube
maps. Material constants (ps cb1): **float 8 = the art's size as a fraction of the screen height** (EOTech 0.038:
72 px of ticks at 2160 measured; PK-AS 0.040), `0.5 0.5`, `0.00175 0.00175`, `6 6 6 6`. Two 6-index quads per
optic: the first writes no colour (stencil), the second is the reticle (blend ONE / INV_SRC_ALPHA).
At the game's 40 degree ADS view the EOTech art's ring is 1.13 degrees across: the real sight's 65 MOA.

**In the mod:** the zoom mesh is shown whenever a weapon is held (`set_zoom_mesh_always`), so the glass is clear and
the reticle quad is drawn at any distance. At that quad (both eyes) the game's pixel shader is swapped for ours
(VS, geometry, depth, stencil and blend kept): per pixel the ray from the eye, its slope off the gun's sight line
in the gun's own axes (`camera_hook`: the drawn gun's sight direction and up from `weapon_points`, in the view's
axes), and `t3` alpha at `0.5 + slope / span`, `span = size * 2 tan(fov / 2)` (`stereo/lens_math`, unit test
`lens_math`). Settings `sight_lens`, `sight_lens_fov` (40), `sight_lens_brightness` (4); the mod's own dot layer
(`sight_dot`) stays off meanwhile.

Monitor (Release, fake head and hands, `sight_ads 0`): SCAR-H with the eye on the line: ring and dot centred in
the window, art size 0.038 read from the material; 20 cm further out the ring keeps its angular size while the
window shrinks; 5 cm below the line no reticle in the window (the line passes above it, as a real holo); head
rolled 20 degrees: the eye 1 cm off the line, an arc of the ring at the window's edge. AK-12 PK-AS: clear lens
near and far, its own ring and dot, art size 0.040. Not checked: the colour per optic (all drawn red: `t3` RGB
is white, the game's tint not found yet), the magnifier with the zoom mesh always on (hidden at the hip), other
optics, the headset.

## The AK-12's optic "missing its top" (2026-11-16 night, monitor, Baku, #39)

Owner, headset 2026-11-15: the AK's scope shows a visual glitch, a missing texture on top. With `sight_lens` on
(default) the zoom mesh was shown **whenever a gun is held** (`set_zoom_mesh_always`): that is the optic as the
game's own ADS camera sees it, built without the parts that camera never sees, so from anywhere else the PK-AS
housing looks gutted. Pipe `zoommesh 1` (hip model) against `zoommesh 0` (zoom model) from above shows the
difference on the optic. Now the zoom mesh shows only while an eye looks into the lens from behind
(`weapon::eye_in_lens_cone`: up to 70 cm behind the lens, within `sight_lens_cone` degrees of the sight line
plus the lens radius; latched with 4 degrees more to leave). 15 degrees still caught a head looking down at the
gun from above (eye 12.7 cm off the line, 19 cm behind the game's eye point): default 8.

| Pose (fake head and hands) | `zoommesh` | Picture |
| --- | --- | --- |
| Head pitched 35 deg down at the gun at the hip | `in_view=0`, nothing patched | full optic (hip model) |
| Sight at the right eye (1.5 cm off the line) | `at_eye=1 in_view=1` | clear lens, the reticle's arc in it |
| Gun held out, eye 7.8 cm beside the line, 28 cm back | `in_view=1` | (zoom model) |

## Magazines held by their body (2026-11-20 night, monitor, Shanghai, #90)

The drawn held magazine's place in the fist comes from the authored grip (`arm_pose` `magazine_grip_ar`, captured on the
SCAR-H, the game's hand 5.3 cm below the top; `magazine_grip_ak`, on the AK-12, 4.3 cm) whatever the gun, or a plain
fist's fixed 7 cm (the gun hand reloading, #38). Now (setting `magazine_by_gun`, on) each gun's magazine is moved by its
own game-hand depth (`magazine_grab_for`, `magazine_hold_drop`) minus the grip's gun's, along the magazine's own up, in
both paths (`contact::authored_grip_shift`, `held_fist_drop`, tested: the depth is the game's pinch, ~3 cm off the fist's
grasp centre, so only the difference applies). Unmeasured guns (MX4, PP-2000, CZ-805; the P90's hand is above its
top) keep the grip's own. Pipe `fingers`: `held_top_above_fist` (the drawn top after the palming turn, fist axes),
`hold_drop`, `held`. Monitor (`reload take`, the setting off -> on on the same build): FAMAS 0.086 -> 0.073, M416 0.082
-> 0.112, QBZ-95-1 0.086 -> 0.077; AK-12 0.076, Galil 0.079, MX4 0.066, CZ-805 0.082 unchanged. `reload grab` takes
only AK-pattern magazines (an AR one is latched until B): a held-magazine test needs `reload take`. Not done here
(#141): magazine lengths (the issue's bottom-within-3-cm check), cupped short magazines, the missing grab points, the
Galil's "cannot grab" (its zone and a held magazine worked on the monitor). Seen once: a NaN magazine zone right after a
gun switch (SCAR-H), not reproduced in 15 reads.

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

## The muzzle flash: not at the drawn barrel (2026-11-16, monitor, #65)

Owner (headset): the flash comes from in front of the player, not from the barrel; it may show where the game
thinks the torso is. Earlier (#65, 2026-11-14) smoke and casings were seen at the drawn gun, the flash not caught.

- **Data** (`tools/fire_effect_probe.py`, typeinfo `FiringFunctionData` +0x150 `FireEffects1p`, +0x158 `3p`; each
  `FireEffectData` 0x50: Offset, Rotation, ZoomOffset, ZoomRotation, Effect, flags): SCAR-H three 1p effects at
  offsets (0, 0.019, 0.925) `UpdateTransform=1`, (-0.012, 0.022, 0.451) and (0, 0.019, 0.925) `StopLoopingEffects=1`;
  AK-12 the same at 0.890 / 0.440. The 3p list has the same offsets. Shot: `InitialPosition` (0, -0.064, 0),
  `SpawnVisualAtWeaponBone=1`, `ActiveForceSpawnToCamera=1`. So the flash is ~0.9 m straight ahead of some frame.
- **Where it shows** (fake head and hands, SCAR-H bursts, back-buffer shots during the burst): with the gun held 30-40
  cm right of and below the eyes and parallel to the view, the flash (the centre flame and the brake's two side jets)
  sat near the view's centre, about 1 m ahead, off the drawn barrel's line; turning the gun 50 deg right moved it only
  ~15 deg right; the shot frame meanwhile started at the drawn sight line (36 cm right, 20 cm down, 18 cm ahead of the
  camera) and points along the barrel, so a flash 0.925 m along it would have sat ~43 deg right. Neither the drawn
  barrel nor the shot frame places it; it follows the camera and the game's aim mostly.
- **Poking the shared offsets** (both 1p entries to 0, restored after): no visible change; the effect most likely takes
  its offset when the weapon is set up, not per shot.
- **Pinned down (second pass, same day):** projected with the game view's view-projection (RenderView +0x420), the
  flame lands on the flat gun's muzzle: rig bone **`Wep_Aim` (161)** plus the offset along its axes (gun straight:
  computed (1169, 1124), flame ~(1170, 1150); gun turned 50 deg right: drawn muzzle (1938, 1306), flame still at
  (1169, 1187), a jet ~90 px right of it, so its rotation may follow the drawn barrel). Raising Wep_Aim 0.3 m with
  pipe `bone 161 0.3 0 0` (local, model and FK copy all moved) left the flame where it was: the effect does not read
  the pose the mod edits. Read watches during 12-16 shot bursts on the FireEffects1p entries, the effect blueprint and
  Wep_Aim/Wep_Root in both poses found only per-frame animation readers, nothing firing-specific; full heap scans
  for a transform at the flat muzzle found casings, smoke and impacts only. The unedited animation pose lives at the
  rig's `+0x30` (re/symbols). The flash's transform is likely inside the emitter system (strings `EmitterSystem`,
  `EmitterManager`): not found.
- **Way forward:** move it at draw time, as The Great Destroyer does (src/effects.cpp `DrawQuadsMoved`, its quad
  compute shader; reuse allowed): the game's muzzle G = the unedited Wep_Aim x offset and the drawn one D = the edited
  pose's, both known in the pose hook; particle quads near G's barrel line are moved by D G^-1 (fully near the barrel,
  fading off along and across it), only while firing and shortly after. Needs a draw survey for BF4's particle
  batches (vertex stride, dynamic buffer) first.
- **Draw-level port, first try (branch `weapon/flash-at-gun`, setting `flash_at_gun`, off by default):** the pose hook
  records Wep_Root before and after the mod's edit (the move; Wep_Aim itself is not moved by the mod: game and drawn
  Wep_Aim 3 cm apart with the gun 35 cm off) and Wep_Aim (the game's muzzle point and barrel); `stereo/muzzle_move`
  (tested) and `stereo/muzzle_quads` (TGD's quad shader). Pipe `flash survey <ms>` during a SCAR-H burst, indexed
  draws from dynamic vertex buffers by kind: strides 16/24/44/64 are in another space (~870 m off), stride 48 (3 MB,
  BF3's particle layout) nearest 9.4 m, **stride 176 (1 MB, blended) nearest 0.21 m**. Moving stride 176 did nothing
  (pipe `flash check`: the quad nearest the game's muzzle 0.22 m before and after; `flash force` lifting all of them
  1 m did move them, so the shader writes): its indices are `0 1 5 5 4 0 2 6 7 7 3 2`, an 8-vertex box, not quads
  (probably the flash's light volume or a decal box). **BF4's flash particles are not among the indexed quad batches.**
  Next: survey `DrawIndexedInstanced`, `DrawInstanced` and `Draw` too (BF4 may instance its particles or draw them from
  a structured buffer), then point the move at that batch.
- **Instanced draws (same day, `note_instanced` from eye_pass's DrawIndexedInstanced):** BF4's instance streams are 4 bytes
  (an instance number: the per-instance data, positions included, is in shader buffers), so positions cannot be read
  from the vertex streams. The blended instanced kinds (instance stride 28 or 40, 12-2310 indices, 2-21 instances) are
  drawn every frame, not only while firing: none is the flash. Still open: `Draw`, `DrawInstanced` and the indirect
  draws (`DrawIndexedInstancedIndirect`, GPU particles), not hooked by the mod yet. The flash now follows the drawn torso
  at least (`aim_follows_torso`, main 4f3e8da): in front of the body, not on the gun.
- **Found (same day):** the flash particles are BF3's layout after all: indexed draws of 48-byte quads (4 vertices each,
  position first) from a 3 MB dynamic buffer, blended. The first try missed them because it read only each batch's first
  16 quads: **BF4's indices are absolute vertex numbers** (`2948 2951 2950 2948 2949 2951`, then `2568 ...`), so one
  batch's quads lie all over the buffer. The plain, non-indexed and indirect draws (now hooked for the survey) hold none:
  no indirect draws and no buffer-less draws at all while firing. **The move now copies the whole particle buffer**, moves
  every quad near the game's barrel line (TGD's shader) and draws with the game's own start, base and offset.
  `flash check` (whole buffer, gun 45 cm right and turned 40 deg, muzzles 0.76 m apart): before, a quad 0.05 m from the
  game's muzzle; after, the nearest quad 0.05 m from the drawn muzzle and none closer than 0.43 m to the game's. On by
  default (`flash_at_gun`). Pictures at ~8 fps with the recoil climbing are not conclusive; the headset judges (GUN-30).
  Cost: a 3 MB GPU copy and a 16k-thread dispatch per particle draw for 3 s after a shot. Like TGD's: dirt a shot throws
  up within ~3 m in front of the barrel moves with the gun too.
- **At 5 fps with recoil moving the view** the pictures cannot say more. Next: find the code that places the 1p fire
  effects (readers of `FiringFunctionData` +0x150 and of the effect's transform), and what frame it uses (the camera,
  the first-person rig's weapon bone before the mod's pose edit, or the aimer); then move that frame to the drawn
  muzzle, as `weapon/shot_origin` does for the bullets.

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

### Crate menu in the headset: what the monitor rules out (2026-11-13, monitor, #34)

Headset 2026-11-11 (CTRL-14, marks 16-19 in `E:\EA\Battlefield 4\bfvr_marks`): `menu_over_level=1` in all four, so the
stash menu **was** detected (it is letterboxed like the pause menu) and the flat screen with the pointer was up. The
game's red circle (its gamepad cursor) sat at the same lower-middle spot; the tabs alternated.

Monitor, Baku checkpoint, the owner's deployed Release build, stash opened with pipe `key 45` after walking up to it
(it is the black/green crate stack ~30 degrees right of the spawn direction, a blue stash icon above it). Every click
through `hooks::point_game_cursor` (pipe `pointer`, the function the headset laser calls) worked:

| Condition | Result |
| --- | --- |
| BF4 behind another window, virtual pad held connected and idle (`pad 0 0 0 0 8000`) | C4 -> Gadget One |
| BF4 in front (SetForegroundWindow), real cursor + SendInput path | Gadget Two tab, then M320 -> Gadget Two |
| BF4 behind, pad held connected with the mark's stick drift (`pad -0.1 -0.1 ...`) | Gadget One tab, M320 hovered |
| Pause menu, both paths, pad connected | hover follows (RESUME, RESTART MISSION, EXIT TO MENU) |

So neither the window focus, nor the gamepad mode, nor stick drift stops the pointer reaching the menu. What is left is
the headset-only stage before it: the laser tested against the flat screen (`theater::point_at_screen`), which needs an
XR session. If `point_game_cursor` had been called, `game_pointer_active` would have kept the triggers from the game,
and the tabs would not have flipped: the laser most likely never hit the screen (or the pointing hand was not tracked).

`pipe status` now has a line for the next headset run (branch `fix/crate-pointer`):
`menu_pointer frames=.. no_hand=.. misses=.. hits=.. hand=.. last_uv=u,v cursor front=.. behind=.. clicks=.. active=0|1`.
Marks save the status, so a mark in the stash menu shows which stage fails.

### Closing the stash (2026-11-13, monitor, #34)

Same checkpoint, stash open, both slots chosen by pointer clicks (C4 -> Gadget Two, M320 -> Gadget One):

| Input | Result |
| --- | --- |
| pipe `key 1B` (Escape through the Raw Input keyboard) | stash stays open |
| pad B (`pad 0 0 0 0 150 2000`) | stays open |
| a real Escape key (`tools\game.ps1 keys -Keys Esc`, BF4 in front) | stays open |
| pointer click on Done (0.648, 0.736) | closes; the soldier carries the M320, objective "Make an Exit" |
| pause menu (pad Start), then pipe `key 1B` | resumes the game |

So the stash has no back action: only Done closes it. The off-hand grip gesture (`input/menu_back`) sends Escape and,
if a menu is still up 500 ms later, clicks Done; the laser is kept off the cursor for 200 ms meanwhile. One observation:
the first pointer click after the stash opened (on C4) switched the slot tab to Gadget Two and filled nothing; the next
clicks behaved. Not explained yet.

### What the stash menu takes, and the stick cursor (2026-11-16, monitor, #34)

Baku checkpoint, the stash opened by the use reach (body snapped to face it, off hand ~45 cm ahead and low, off-hand
grip; E alone, sent with BF4 in front, opened nothing while the game's aim stayed level). Inputs tried in the open menu:

| Input | BF4 behind another window | BF4 in front |
| --- | --- | --- |
| pad left stick (0.6, 600 ms), D-pad down/right, RB | nothing | nothing |
| arrow keys (pipe `key 27` / `28`, Raw Input) | nothing (the game read them) | real Right key: nothing |
| pipe `pointer` clicks (the laser's path) | C4 -> Gadget One, M320 -> Gadget Two, Done closed it | (worked 2026-11-13) |

So this menu has no pad or keyboard navigation to drive; only the cursor clicks work, and they need no focus. The
headset failures (CTRL-14) are before that call: the laser test against the flat screen. `menu_input = stick` gives
the clicks a position without the laser: either thumbstick moves a dot over the flat screen (`input/stick_cursor`,
0.6 screen widths per second at full tilt, squared response, dead zone 0.15), either trigger clicks through
`hooks::point_game_cursor`; while it is active the sticks are kept from the game's pad.

### The hand locked on the magazine (owner 2026-11-16: "floaty", fingers not always on it)

- **Seated magazine:** the snap onto the seated magazine (#40) was a pure distance blend, so the drawn hand drifted with
  the controller even while gripping. Now a grip on the seated magazine that does not pull it out (an AR's) sets the
  grip lock's hold (`lock_hold_`): the drawn hand holds it at the magwell with the game's grip until the grip opens or the
  controller is `lock_leave` (16 cm) off. Monitor (SCAR-H): `lock=1.00/1.00`, the drawn left wrist unchanged to the mm
  with the controller 8 cm one way and 7 cm the other; let go released it. Unit test `seated hold`.
- **Palming (AR):** once palming is ready (a new magazine beside the seated one) and `grip_lock` is on, the off hand's
  pose is stored in the gun frame and the whole reload logic sees that frozen hand; the drawn hand is locked on the new
  magazine (`hand_lock`). The real controller `palm_lock_leave` (12 cm) away lets go; B still catches. Unit test
  `palm lock`; pipe `reload` shows `palm_lock=`. Not shown live: headless the fake hand reached palming-ready in one of
  two runs.

## The weapon crate and unlocks (2026-11-15, monitor, Shanghai-tower checkpoint "Reach the VIPs")

- **Weapon census**: every `SoldierWeaponUnlockAsset` (vtable `0x141E76F40`) is in memory during a level: 259 assets,
  167 without underslung variants (`_M320_*`, `_M26_*`), i.e. every BF4 gun and gadget including the MP-only ones.
  `tools/unlock_census.py` reads them; it needs a full heap walk (pipe `scan` returns at most 64 hits per region).
- **The crate menu** ("[E] OPEN WEAPON CRATE") is a `UIWeaponSelectorGridWidget` (vtable `0x141BF9970`, one instance).
  Its data at widget `+0x28`: `CategoryDisplayOrder` (+0x78) = SIDEARM, SHOTGUN, PDW, CARBINE, ASSAULTRIFLE, LMG, DMR,
  SNIPER; `SlotPrimary`/`SlotSecondary` 0/1; **`UnlockEverything` (+0xA0) = 0**.
- **The crate's list**: widget `+0x148` -> 51 entries of 0x38 bytes `{unlock asset, slot (0 primary / 1 secondary),
  attachment list, shared ptr, -1, 0, 1}`, once for each slot (102). No per-entry lock flag. The 51 in list order:
  MP412Rex | CBJ-MS Scorpion JS2 MX4 P90 MagpulPDR PP2000 UMP45 | 870 HAWK M1014 QBS09_SP SAIGA_20K SPAS12 USAS-12_SP UTAS |
  A91 GalilACE52 AK5C G36C M4A1 SG553LB | AEK971 AK12 SteyrAug CZ805 FAMAS M16A4 M416 QBZ951 SAR21 SCAR-H |
  M240 M249 MG4 Pecheneg QBB95 Type88 Ultimax | MK11 M39EBR QBU88 RFB_SP SCAR-HSV SVD12 | SRS FY-JS M82A3_SP Scout SV98.
  On screen the rows reorder: Shotgun shows 870, HAWK, M1014, SPAS-12, USAS-12, UTS, ... (QBS-09 and Saiga later);
  PDW shows MX4 ... UMP-45 then CBJ-MS. Whether the moved ones are the locked ones is not verified.
- **`SyncedBFSettings`** (constructor `0x141A39F90`, vtable `0x141C58A20`): three instances, `AllUnlocksUnlocked`
  (+0x54) 0 in each. Poked to 1 (all three) and `UnlockEverything` to 1 with the menu open: the open menu did not
  change. **Owner, after closing and reopening the crate: every gun selectable** (2026-11-15). Which of the two
  flags is needed is not known. Design and work order for all 51 guns: `docs/tasks/all-weapons.md`.
- **Which flag** (static, 2026-11-15): every reader of `SyncedBFSettings` goes through the cache `0x1423717C0`, and
  the unlock check `0x140157D10` tests `AllUnlocksUnlocked` before the player's unlock sets, so that flag on the
  cached instance is the lever. **Confirmed live** (fresh start, 2026-11-15): with only that flag held at 1 by
  `weapon/unlock_all` (the crate's `UnlockEverything` untouched, 0) the owner found every crate gun selectable. The
  level-load settings sync (`0x1402BA570`) writes the flag from the server's copy, so it must be set after it:
  `weapon/unlock_all` (setting `unlock_all_weapons`, pipe `unlocks`) holds it at 1 while a campaign level runs.
- **Per-gun data**: unlock asset +0x78 `StoredWeaponData` {WeaponClass, MagCount, MagCapacity} for every gun;
  +0x58 NonStreamedBlueprint is null until the gun is spawned, so its firing data (FireLogic: ReloadType +0x9C,
  FireLogicType +0xA0, BoltAction +0x1C) is read from the spawned weapon (+0x30 SoldierWeaponData -> +0x90
  WeaponFiringData -> +0x10 FiringFunctionData -> +0x168): `tools/weapon_catalog_probe.py [--held]`.
- **Clicks**: in this menu background pointer clicks (BF4 not in front) only hover; Done, the row arrows and items
  did not react, and a click on an item flipped the Primary/Secondary tab instead (as in the gadget stash, #34).
  Pad D-pad does not move the selection. `pipe watch <UnlockEverything> rw` saw no read while the menu was idle.

## Own crate: a gun into a slot without the game's menu (2026-11-16 night, monitor, Baku tunnel, #80)

The owner wants our own crate instead of the game's menu (which takes clicks only with BF4 in front). The level scripts
hand out kits with `ServerCustomizeSoldierEntity` (vtable `0x141E695A0`): its event `0x0C487271` passes its
`CustomizeSoldierData` to the soldier's `customize` (ServerSoldierEntity slot 100, `0x140FC0F30`), args
`{soldier, player (soldier+0x240), data, 0}`. With a player the call goes to every soldier component (slot 17) and
the weapons component (`0x140F7FDC0`) puts each `UnlockWeaponAndSlot` of `data+0x38` into its slot.

- **Works:** `weapon/own_crate`, pipe `givegun <debug id> <slot>` (research). The call runs on the camera hook's thread
  (the view-params copy, the one pipe `entityevent` uses). **Not established** whether that is the thread that updates the
  server soldier: a write watch on the soldier's feet (+0x720) while walking caught `rip 0x140FCE9AC`, call chain
  `0x1409FCF27 0x1409764E3 0x1407EC922 0x140988E3F 0x1409D6ADC 0x1409D7E23`, without a thread id. Eight gives so far ran
  without a fault; a race stays possible.
  `givegun U_M416 0` with the SCAR-H in slot 0: `ok`, 3.5 s later `reload: U_M416 slot 0 AR, capacity 30, gun 31,
  reserve 124`, slot listing `0 U_M416, 1 U_AK12, ...`, the M416 in hand on screen (carry handle, no attachments).
  A gun the level had never spawned: its bundle loads by itself (the 3.5 s).
- **Dead end:** copying the first `CustomizeSoldierData` found (the class prototype, header `0x01000001`) let a
  release free our copy during the call (`C0000005` at `0x140F7FE6F`, our object filled with `cc`/`fe`). Copy a level
  asset (top byte `0xB1`) and give the copy a large count (`0xB1001000`); the call is under `__try`.
- **The picker (#80):** settings `crate_primary` / `crate_secondary` (Weapon > Weapons). The panel row is a stepper,
  so a choice is given only after it has rested 1.2 s and no give runs (latest wins; `crate::change_watch`).
  MP412 Rex into slot 1 and 870 into slot 0 worked too. Each pick keeps one 4 KB page (the engine may keep pointers).
  A pick due while there is no soldier (cutscene, respawn) is offered again after another settle.
- **Known limit:** some given guns have no reload SoundEntity in the level (AK 5C, 2026-11-16; the M416 had one). The
  lookup stops after 3 searches (`game_reload_sounds`, it repeated a full heap search every 7.5 s before) and the
  synthesised reload cues play.
- **Not done yet:** attachments (`CustomizationUnlockAssets`, e.g. the crate list's attachment arrays at widget
  `+0x148`).

## Fist rack, two handle zones (2026-11-15, monitor: fakehead + fakehand, AK-12 from the crate)

- `rack_grip = fist`: the grab's zone decides the hold. AK-12 (handle on the right, (-0.02, 0.06, 0.53)): the left
  palm at the overhand zone's centre took `fist_over` (no game pinch, `authored=none`); 6.0 cm back and let go
  chambered (magazine 30 -> 29). Palm up 12 cm below the knob took `fist_under` (stroke palm-up 0.94); the same pull
  chambered (29 -> 28). Both holds put the fist on the same knob point, at the front of the receiver on the right,
  the left arm reaching over the top for overhand.
- SCAR-H's point (0.035, 0.04, 0.57) as `reload handle` on the AK: the overhand zone moved to the gun's left (centre
  x 0.064 against 0.179); overhand and underhand both chambered. The SCAR's own mesh was not in hand (see below).
- Crate: `pipe key 69` (E) did not open "[E] OPEN WEAPON CRATE" here; `game.ps1 keys -Keys E` (focus) did. In the
  menu keyboard Down scrolls the rows (three presses show Carbine and Assault Rifle); `game.ps1 click` works with
  the client at 1920x1080 and the menu in the 2160x2160 back buffer's centred 16:9 band (client x = bb x * 0.889,
  y = (bb y - 472) * 0.889). Done: client (1245, 992).
- With BF4 in front the pipe's `pad` stick no longer turned the soldier (it did while BF4 was behind), and
  `fakeyaw` turns only the camera, so the crate could not be faced again for the SCAR-H. A `fakehead` yaw turns
  the body with the head.
- **2026-11-15 evening (monitor, SCAR-H in Baku, empty gun)**: overhand fist with `rack_over_out` 0 sat on the
  receiver; with 0.05 / up 0.01 it holds the knob from outside with a gap to the gun. The SCAR-H's handguard spot
  lies inside the underhand rack zone (its handle sits above the front of the handguard), so the zone claimed the
  off hand and the two-handed grab never started (`busy=1`); the underhand zone is now only there while the gun
  needs charging (`needs_rack`: nothing chambered or the bolt open). Handguard lock (`support_lock`): grabbed at
  4.8 cm ahead of the magwell, the controller 7 cm forward left the hand at 4.8 cm (unlocked: 11.4 cm), 7 cm back
  moved it 3 cm with the game's own support spot as the barrel tilted; no regrab (`grabs` unchanged).
