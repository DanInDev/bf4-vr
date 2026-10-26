# Gamepad detection (BF4)

Status: **fix implemented (`virtual_pad = 1`, default), pending live verification.** The VR
controllers reach our virtual XInput pad, but BF4 only reads it while a physical controller is
plugged in; see "Virtual pad" below for the fix.

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

Workaround until verified: keep a physical controller plugged in.

## Virtual pad (issue #4): static analysis, pending live verification

Addresses below come from static analysis of build 179547 and have **not** been confirmed in a
running game yet; they are not in `re/symbols/bf4-179547.md` until they are.

| Address | What | Evidence |
| --- | --- | --- |
| `0x140949BF0` | Input init, runs once: `DirectInput8Create(.., 0x800, IID_IDirectInput8A, ..)` (ANSI), then `EnumDevices(DI8DEVCLASS_GAMECTRL, 0x140946690, ctx, DIEDFL_ATTACHEDONLY)` | Disassembly |
| `0x140946690` | EnumDevices callback: per device `IsXInputDevice(&ddi->guidProduct)`; TRUE → XInput pad object (user index = running counter), FALSE → DirectInput joystick object | Disassembly |
| `0x14094A090` | `IsXInputDevice` (Microsoft's WMI sample: `Win32_PNPEntity` DeviceID with `IG_`, VID/PID vs `guidProduct.Data1`); prologue `48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57`; only caller `0x1409466F5` | Disassembly |

Devices are enumerated once at start-up, so without a pad at launch no XInput pad object exists
and nothing ever calls XInput. The XInput pad object polls by itself afterwards
(`XInputGetCapabilities` while disconnected, then `XInputGetState`).

Mechanism (`src/input/virtual_pad.cpp`):

1. The `dinput8.dll` proxy wraps the `IDirectInput8A` from `DirectInput8Create` only when the IID is
   `IID_IDirectInput8A`, there is no aggregation and the return address lies in the game executable.
   Every method is forwarded.
2. `EnumDevices(DI8DEVCLASS_GAMECTRL)` first installs a prologue-checked MinHook hook on
   `0x14094A090` (the game is running its input init, so the DRM has decrypted the code; a mismatch
   means another build and nothing is offered), forwards the enumeration, then, unless the
   callback stopped it or the call failed, calls the game's callback once more with a fake
   `DIDEVICEINSTANCEA` "bf-vr virtual pad" (type `0x00010215`, HID gamepad, made-up product GUID
   `{6E1C1F2A-9B4D-4C3E-A7F1-B4F0E5D2C8A1}`).
3. The hook answers TRUE for that product GUID and calls the original for everything else, so the
   game builds an XInput pad object for it: user index 0 without a real pad, 1 with one.
4. The existing XInput hook reports user index 0 connected while the VR controllers are active.
   `CreateDevice` for the fake GUIDs returns `DIERR_DEVICENOTREG`; `GetDeviceStatus` returns `DI_OK`.

Off with `virtual_pad = 0`; also off when `gamepad = 0` or `theater = 0`. x64 only (the bf3 build
wraps the interface but never offers the device). Live check: `bfvr.log` lines starting with
`virtual pad:`, and `pipe status` → `virtual_pad_offered=1 virtual_pad_xinput=1` and non-zero
`get_capabilities_calls` without a physical pad. Smoke-tested outside the game (wrapper forwarding,
ref counting, `CreateDevice` refusal); the offer path needs the game.
