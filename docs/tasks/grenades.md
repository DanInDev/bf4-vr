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

**Stage 0 (2026-10-26, monitor):** see `re/notes/weapon.md` "Grenades". LB is the game's quick throw.
All four launch fields steer the next throw when written just before the press: speed and direction
(shot frame: camera, x left / y up / z forward, pitched with the aim), position (only with
`ForceSpawnToCamera` = 0), and `SpawnDelay` 0 + `HoldAndRelease.Delay` 0 make the grenade leave 0.12 s
after the press. No other server soldier in the checkpoint shares the player's `U_M67_SP` data. The
count (1 + 2) did not drop after throws in this checkpoint. Measured launch speed is ~83 % of the
written one in every throw (the game's damping); `grenade_throw_scale` (1.5) covers it.

**Stage 1:** `src/weapon/grenade_hands.*` (pure; tests in `tests/reload_test.cpp`): take at the belt
(70 cm below the head, 10 cm ahead, 15 cm reach) with the off-hand grip; the gun hand grips within
10 cm of the grenade and pulls 5 cm = pin out; letting go throws with the fastest 80 ms hand velocity
of the last 0.15 s (>= 1.5 m/s), else drops it; pin in = put back; tracking lost = put back.

**Stage 2:** `src/weapon/physical_grenade.*`, live settings `grenade = button|physical` and
`grenade_throw_scale`. At a throw: launch data written (hand position, velocity x scale, capped at
30 m/s, no delays, not forced to the camera), an 80 ms LB pulse, data restored after 400 ms. In
physical mode the game never sees LB itself; the gun-hand grip (RB) is held back while it holds the
pin. Research pipe `grenade` (state) and `grenade throw <yaw> <pitch> <m/s> [safe]`.

**Monitor-verified:** pipe throws straight / 30 deg right / 30 deg left + 10 deg up go where commanded,
0.12-0.16 s after the command; the full gesture with synthetic hands and pad grips (take, pin, a
0.6 m forward-up arm swing in ~0.12 s) launched the grenade 0.115 s after the release at 9.7 m/s
forward and up.

**Grenade in the hand (2026-11-04, `feature/held-grenade`):** the game draws no grenade until the
throw, so the compositor shows one. `ui/held_grenade` paints a shaded M67 (body, yellow band, fuse, lever,
and the pin ring until the pin is pulled) into a 128 px texture. It is a quad 11 cm wide at the off hand's palm
(`weapon_points.palm[0]`). The quad faces the eye: a lit sphere looks the same from any side. Its roll keeps the
fuse along the off hand's aim pose axis (0, 1, -0.4), as seen from the eye. It is an overlay, so the fingers
never cover it. **Removed (#48, 2026-11-12):** the game's own 3D grenade (`stereo/extra_mesh`) replaced this
stand-in; the theater no longer draws it and its settings `grenade_model` / `grenade_mesh` are gone. The
engine-free `ui/held_grenade` texture and placement stay, with their test, for a future flat fallback.
The texture and placement are unit-tested (`held_grenade`); how it looks in the hand is a headset row.

**Not done / for the headset:** feel of the belt position, pin distance, throw threshold and scale;
flashbangs/smoke/C4 (only names are recognised); cooking (fuse). Haptics: done (#32).
