# Haptics — BF4 build 179547 (#32)

Status: **controller haptics in place, monitor-verified up to the OpenXR call** (2026-10-27). Firing,
nearby explosions and damage all reach the mixer; the vibration itself needs the headset (rows 17, 18
of docs/HEADSET-TESTS.md).

## How the game rumbles

| Step | Where | Notes |
| --- | --- | --- |
| Rumble manager | function containing `0x140838013` (`0x140837FA0`..) | Takes the strongest (low, high) of a linked list of active rumble effects plus a base pair, and passes them through a vtable call (+0x88). When the list is empty or expired it passes 0, 0 |
| Rumble setter | `0x14094D490` (prologue `40 53 48 83 EC 50 48 8B 05 5B 77 A1 01 0F 29 7C 24 30`) | `(pads, float low, float high)`, levels 0..1. **Zeroes them unless the window has keyboard focus**: `GetFocus()` (IAT `0x141B40F90`) must be non-null, unless a settings byte (+0xF6 of a settings object) is set. Also zeroes for pad +0x1E2 set or a vtable check (+0x80). Stores the clamped levels in the pad object +0x3C / +0x40 |
| Pad write | `0x14094F8D0` (leaf) | Pad object: +0x14 user index + 1, +0x3C/+0x40 levels, +0x48 vibration enabled (zeroes the levels when 0; it was 1). Scales to 0..65535 at +0x4A/+0x4C and tail-jumps to `XInputSetState` (pointer argument = pad + 0x4A) |
| Caller | pad update `0x14094C311`.. (`call 0x14094F8D0` at `0x14094C515`) | Every pad poll: `XInputSetState` is called exactly as often as `XInputGetState` |

Settings file: `GstInput.Vibration 1` (on).

## Measurements (monitor, no headset, BF4 window not focused)

- `XInputSetState` hook: 19,138 calls, as many as the pad reads, every one with both motors 0 (firing a
  burst, a quick grenade).
- Rumble setter hook (before the focus gate): also 0, 0 throughout: the manager's effect list stayed
  empty. Effect producers (weapon fire, explosions, damage) are not yet found; they may check focus or
  "pad is the active device" themselves. Next: watch the list head while the window has focus.
- So the mod makes its own firing feedback: the current weapon's magazine (server ammo, as physical
  reload reads it) dropping = a shot = `input::recoil()` (gun hand, and the off hand while two-handed).
  Verified: 7 shots -> 7 pulses (physical reload), 8 -> 8 (button reload).

## What drives each feeling (2026-10-27, monitor)

| Event | Source | Measured |
| --- | --- | --- |
| Firing | Our shot detection (magazine drop) with the weapon's own `WeaponFiringData.Rumble` (+0x38: low, high, duration; `RumbleFiringData`) | AK-12: 0.2, 1.0, 0.12 s -> pulse 1.0 for 0.06 s per shot. The game produced no firing rumble in any test (its effect list stayed empty while firing) |
| Nearby explosion | The game's own rumble, at the setter hook (before its focus gate) | Grenade ~4 m away: low 0.50, high 0.20 -> 0.50 for ~0.35 s, then 0.24 / 0.10 fading; a grenade ~15 m away: nothing. Works with the window unfocused |
| Damage | Health watch: soldier +0x140 health component, +0x20 health, +0x24 max (100) | The same grenade: 100 -> 28.7 (hit 71.3) -> one hit pulse 1.0 for 0.31 s, both hands; regeneration is ignored |

Explosion and damage rumble in data comes from the sound system: `PadRumbleNodeData` (audio-graph node,
ports `RumbleHigh` +0x10, `RumbleLow` +0x18). `ClientSettings.PadRumbleEnabled` is at +0xDC.

## In the mod

- `input/haptics`: mixer (game levels per hand, pulses, strength, toggles), `tests/haptics_test.cpp`.
- `hooks/rumble_hook`: the setter hook (x64), feeding the mixer; the `XInputSetState` capture stays as
  the fallback (BF3, or if the hook does not match).
- `xr/controller_actions::vibrate`: `XR_ACTION_TYPE_VIBRATION_OUTPUT` on `/user/hand/{left,right}/output/haptic`,
  renewed every frame (50 ms) while non-zero, stopped once.
- `weapon/hit_feedback`: the health watch (hits, both hands, `input::hit_pulse`).
- Firing: `weapon/physical_reload` reads the weapon's rumble with its magazine; `input::firing_pulse`.
- Gesture taps: reload events, grenade take/pin/throw, two-handed grab/release/accessory, holster
  zone/draw, melee, squad order, use, panel clicks.
