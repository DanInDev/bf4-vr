# Haptics — BF4 build 179547 (#32)

Status: **controller haptics in place, monitor-verified up to the OpenXR call** (2026-10-27). Shots give a
recoil pulse in both reload modes; the game's own rumble is captured before its focus gate but stayed at
0 in every test here. The vibration itself needs the headset (rows 17, 18 of docs/HEADSET-TESTS.md).

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

## In the mod

- `input/haptics`: mixer (game levels per hand, pulses, strength, toggles), `tests/haptics_test.cpp`.
- `hooks/rumble_hook`: the setter hook (x64), feeding the mixer; the `XInputSetState` capture stays as
  the fallback (BF3, or if the hook does not match).
- `xr/controller_actions::vibrate`: `XR_ACTION_TYPE_VIBRATION_OUTPUT` on `/user/hand/{left,right}/output/haptic`,
  renewed every frame (50 ms) while non-zero, stopped once.
- Gesture taps: reload events, grenade take/pin/throw, two-handed grab/release/accessory, holster
  zone/draw, melee, squad order, use, panel clicks.
