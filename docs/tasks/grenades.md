# Task brief: physical grenades

Written 2026-10-26 (`feature/grenades`). Read `CLAUDE.md`, `docs/SAFETY.md` (binding) and this file
first. Issue: #14 (weapon handling: two-hand grip, physical reloads, grenades). Builds on the physical
reload (`src/weapon/`, `re/notes/weapon.md`).

## Goal

Overlord-style grenades: take one from the chest, pull the pin with the other hand, throw it with
your arm. The grenade leaves from the hand, along the hand's motion, as fast as it was thrown.

## Idea: steer the game's own throw

The game keeps throwing, counting and exploding its grenades. At the moment of release the mod
writes the throw's launch data, fires the game's throw, and restores the data once the grenade
exists. The reflected data classes have the fields:

| Class (offset) | Field | Use |
| --- | --- | --- |
| `FiringFunctionData` (+0x60 `Shot`) → `ShotConfigData` | `InitialPosition` +0x00, `InitialDirection` +0x10, `InitialSpeed` +0x20 (Vec3s), `SpawnDelay` +0x70, `ProjectileData` +0x50 | start at the hand, along the throw, at the throw's speed, no animation wait |
| `FiringFunctionData.FireLogic` (+0x168) → `HoldAndReleaseData` +0x00 | `MaxHoldTime`, `Min/MaxPowerModifier`, `PowerIncreasePerSecond`, `Delay` | the game's own hold-to-throw power |
| `ProjectileEntityData` | `InitialSpeed` +0x90, `TimeToLive` +0x94 (fuse), `DetonateOnTimeout` +0xD8 | fuse, cooking |
| `GrenadeEntityData` | `Gravity` +0x130, `CollisionSpeedMultiplier` +0x134 | trajectory model for aim previews |

## Stages

**0. Research on the monitor** (game running, pipe, campaign only)
- Which pad input throws (LB is expected: also the off-hand grip, so it needs claiming as in the reload)
- When the grenade spawns after the press (`SpawnDelay`, animation)
- The grenade count (slot 6 `U_M67_SP`, firing object `+0x1A0` as for guns?)
- Whether a write to the shot data changes the next throw, client and/or server side (the server owns
  the ammo; it may own the projectile spawn too)
- Whether the data object is shared with other soldiers (AI grenades): then write only around our throw
- Can the throw animation's grenade bone be held at the hand (show the grenade before release)?

**1. Gestures** (pure, `src/weapon/grenade_hands.*`, offline tests): take at the chest/belt (grip),
pin (other hand grips near the grenade top and pulls ≥ 5 cm), throw (release while the hand moves
> 1.5 m/s; the velocity is the average over the last ~80 ms), drop (slow release with the pin out:
grenade at the feet), put back (release without the pin pulled).

**2. Launch data** (`src/weapon/physical_grenade.*`): at the throw write position, direction, speed
(hand speed × factor, capped at the game's throw), `SpawnDelay = 0`; fire; restore. Research pipe
command `grenade throw <yaw> <pitch> <speed>` for monitor tests (screenshots of the landing point).

**3. Later**: cooking (fuse), haptics, other grenade types (flashbang, smoke; C4 differs), the grenade
mesh in the hand before release.

## Constraints

- Shared data: restore immediately after the spawn, never leave it changed.
- Every engine address verified and recorded (`re/symbols/bf4-179547.md`, `re/notes/weapon.md`).
- Feel (speed threshold, factor, pin distance) needs the headset; trajectories can be checked on the
  monitor.

## Results

(filled in as the stages land)
