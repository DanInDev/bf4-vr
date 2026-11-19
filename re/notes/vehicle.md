# Vehicles (driving)

Status 2026-11-19: driving works with the left stick and with a virtual steering wheel (`drive_controls`,
`src/input/drive_wheel`, `src/input/vr_controls.cpp` `for_vehicle`). Checked on the monitor at the Baku
checkpoint "Reach Secondary Extraction" (the player starts in the driver's seat of an SUV), headless debug
run at 10-24 fps.

## The controlled entity

`[0x14230F260]`, long used as "the local soldier", is the player's **controlled entity**: the
`ClientSoldierEntity` on foot, the `ClientVehicleEntity` (vtable `0x141D3D200`) in the driver's seat.
`tools/object_probe.py 0x14230F260 --deref` in the car: +0x130 `ClientVehicleSpawnEntity`, +0x140
`ClientVehicleHealthComponent`, +0x148 `ClientVehiclePhysicsComponent`, +0x238 `DynamicPhysicsEntity`,
+0x3E0/+0x540 `ClientChassisComponent`, +0x6A0 `ClientUnlockComponent`, +0x6C0/+0x720/+0x740
`ClientCameraComponent`, +0x6E0 `ClientPlayerEntryComponent`.

Consequence: every module that followed soldier offsets from it (weapons +0x570, rig +0x1E8, stance
+0x4F0, health +0x140, data +0x30) read the vehicle's memory while driving, and the aim and arm code wrote
through those chains. All of them now go through `common/controlled` `game::local_soldier()`, which is 0
unless the vtable is the soldier's.

## How the game drives on the pad

Measured with `pipe pad` and the camera position (`GameRenderer` -> RenderView +0x30 translation):

| Input | Result |
| --- | --- |
| left stick forward (y +32000), 4 s | 9.6 m forward |
| left stick back | brakes, then reverses (5 m in 2.5 s) |
| left stick forward + right | turned ~70 degrees in 4 s |
| right trigger alone, 4 s | no throttle (rolled 1.5 m) |
| stick released | the car stops within ~1 m by itself (arcade engine braking) |

So BF4's pad layout drives with the left stick only (`ConceptMoveFB` / `ConceptMoveLR`); the triggers do
not throttle or brake.

**Handbrake: none found on the pad.** Reverse at full stick for 2.5 s covered 7.4-7.7 m with A, B, X, LB,
RB, LT, RT or left stick click held, the same as with nothing held. Right stick click gave 10.5 m once (a
boost?), not repeated. The vehicle data has `AutoHandBrakeIfNoThrottleAndSteer`, and the input concepts
`ConceptHandBrake` (59) and `ConceptBrake` (58) exist, but the pad mapping does not seem to bind them.
Not tried: Y (likely exit vehicle), D-pad, and the keyboard's vehicle binding through `pipe key`
(`hooks::press_game_key`), which is the next thing to try for a handbrake / drift button.

## Dead ends (do not repeat)

- The input mapping data (`InputActionsData` vtable `0x141D86960`, `PadInputActionData` `0x141D86A40`,
  getType at `0x141ABE0F0` / `0x141ABE220`) is not resident at runtime: a full `scan` found one freed
  object (`0xdddd...` fill). The bindings must live in runtime tables built from it.
- `PROFSAVE` / `PROFSAVE_profile` hold no pad bindings (defaults are not saved).
- Options > Key Bindings > In Vehicle: the Joystick column lists DirectInput joystick buttons, not the
  XInput pad; and its sub-tabs ignore the pad and `pipe pointer` clicks (the pointer only hovers there).
- Coast distance after throttle says nothing about a handbrake (the car stops by itself).

## Menu handling found on the way

- With the mod's `x_menu` on, pad X is Start: a test pressing X opens the pause menu.
- The pause menu ignored `pipe pointer` clicks and pad Start; pad A (`pad 0 0 0 0 400 1000`) on the
  highlighted RESUME closed it. In Options pad D-pad down / A / RB work; B backs out.

## The view in a vehicle

Controller aim levelled the world camera at the mod's body yaw, so the view stayed where the car first
faced and the car turned under it (screenshot: the view faced the passenger after a turn). Now
`input::vehicle_view` keeps the game's vehicle camera as it is (yaw, pitch, roll: the cockpit stays put
around the head) and makes the body yaw follow it, so stepping out carries on from the car's heading.
