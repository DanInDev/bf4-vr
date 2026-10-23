# Gamepad detection (BF4)

Status: **open.** The VR controllers reach our virtual XInput pad, but BF4 only reads it while a
physical controller is plugged in.

## Evidence (2026-10-23)

| Observation | Source |
| --- | --- |
| Stick and button input arrives at the virtual pad, e.g. left (13416, 29413), right (5934, -31993) | `controllers: input` lines in `bfvr.log` |
| Without a physical pad, BF4 makes **0** `XInputGetState` and **0** `XInputGetCapabilities` calls | `pipe status` → `xinput get_state_calls=0 get_capabilities_calls=0` |
| With the user's Cepter pad plugged in, the Quest controllers work in game | User test, theater and stereo |
| Posting `WM_DEVICECHANGE` (DBT_DEVNODES_CHANGED) to the BF4 window does not start polling | Counters unchanged |

So BF4 decides "a gamepad exists" through another API first and only then polls XInput.

## Candidates

1. **DirectInput enumeration** (`IDirectInput8::EnumDevices`): we are the `dinput8.dll` proxy and
   already see `DirectInput8Create`, so wrapping the returned interface can log, and later
   extend, what BF4 enumerates.
2. **Raw Input / SetupAPI / HID** device lists.
3. **Virtual pad driver** (ViGEmBus): a real virtual Xbox 360 controller that BF4 detects normally.
   Needs a one-time admin driver install; the most robust option.

Workaround until fixed: keep a physical controller plugged in.
