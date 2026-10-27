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

- **The keyboard is Raw Input**: the image imports `GetRawInputData` / `RegisterRawInputDevices`; no
  DirectInput keyboard. Synthetic keys work without focus: `hooks::press_game_key` posts `WM_INPUT`
  with a marker handle (`0x7BF5A000`+n) and the `GetRawInputData` hook answers it (pipe `key <vk>`).

## Open

- Hardware-watch the writer of the server ammo to find the auto-reload decision, if the spare rounds
  ever show (HUD) or leak (a full-auto burst faster than one frame).
- Shotguns, bolt actions, belt-fed guns, pistols: variants and their bones (`Wep_Belt*`, `Wep_Slide`).
- Underslung launchers (`Wep_Grenade1/2`) use a second firing function.
