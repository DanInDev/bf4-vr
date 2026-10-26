# Task brief: right-stick turning and stance, laser-pointer menus

Written 2026-10-26 (`feature/vr-ux-design`). Read `CLAUDE.md`, `docs/SAFETY.md` (binding) and this
file first. Issues: #22 (right stick), #23 (laser pointer); related #4, #5, #7, #9, #12.

## Goal

The user's two input items from 2026-10-26:

1. **Right stick.** Horizontal = turn the view only (snap turn or smooth turn), never aim. Vertical =
   stance: flick up stands up one step, flick down crouches, a second flick down goes prone. Pitch
   comes from the head (later the controller, #12), never from the stick.
2. **Menus by pointing.** Point a controller at the theater screen (or the HUD layer, or our own
   settings panel) and click with the trigger, instead of driving menus with the controllers as a pad.

## What exists

| Piece | Where | State |
| --- | --- | --- |
| Controllers → virtual Xbox pad | `src/xr/controller_actions.*` (`sync`), `src/input/gamepad.*`, `src/hooks/xinput_hook.*` | Works without a physical pad (#4, `re/notes/gamepad.md`). Right stick goes to the pad's right stick today |
| Fake XInput device for BF4's DirectInput enumeration | `src/input/virtual_pad.*` | We already wrap the game's `IDirectInput8A`, so wrapping its mouse device is the same pattern |
| Aim/grip poses of both hands | `stereo::hand_sample` (`src/stereo/tracking.hpp`), located each frame in `controller_actions::locate` | Works (arm IK uses grip) |
| Aimer yaw/pitch | `[[ClientSoldierWeapon+0x4988]+0x10]` +0x14 yaw, +0x18 pitch, radians (`re/notes/aim.md`) | Mapped and writable; writes turn the camera 1:1 and persist; stick right increases aimer yaw. Not authoritative in scripted no-weapon sections or vehicles |
| Theater screen | `src/xr/theater.cpp`: quad 2.5 m ahead, 3 m wide, LOCAL space | Shown for menus and loading |
| HUD layer | `src/xr/theater.cpp` + `src/stereo/hud_capture.*`: head-locked quad in VIEW space, `hud_distance` 1.5, `hud_width` 1.3 | Works (headset 2026-10-25) |
| **Right-stick state machine** (new) | `src/input/right_stick.*`, `tests/right_stick_test.cpp` | Engine-free, tested; not wired in |
| **Ray → quad hit** (new) | `src/input/pointer.*`, tested in `tests/gestures_test.cpp` | Engine-free, tested; not wired in |

### The state machine (`input::right_stick`)

`update(x, y, seconds)` returns `{ turn, stance }` per frame:

- **Deadzone** (0.2 radius): inside it nothing happens and every gesture re-arms.
- **Axis lock:** the first axis to exceed the other by `axis_bias` (1.5×) owns the stick until it
  returns to the deadzone. A pure diagonal does nothing; turning and sweeping round the rim to the
  top never changes stance, and vice versa.
- **Snap turn:** fires `±snap_angle` (30°) when |x| crosses `snap_press` (0.7); re-arms below
  `snap_release` (0.4). `snap_cooldown` (0.25 s): a held press during the cooldown fires when it
  ends; a flick released during the cooldown is dropped, never queued.
- **Smooth turn:** `smooth_speed` (2.6 rad/s ≈ 150°/s) × deflection rescaled from the deadzone edge.
- **Stance flicks:** `up`/`down` when |y| crosses `stance_press` (0.75); re-arms below
  `stance_release` (0.35). `next_stance(current, command)` gives stand ↔ crouch ↔ prone.
- `turn` is radians, positive = turn right: add it to aimer +0x14.

All thresholds are config fields meant to become registry settings (`docs/tasks/vr-settings.md`).

### The pointer (`input::point_at`)

`point_at(aim_pose, quad_target, max_distance)` intersects the aim ray (−z of the aim pose) with an
OpenXR quad (its pose, width, height, visible from +z) and returns the hit point, distance and
texture uv (0, 0 top left). Back faces, misses outside the quad and hits behind the controller are
rejected. A head-locked quad (view space) must be composed with the head pose first.

## Approaches, most promising first

### Right stick

1. **Aimer write + pad right stick zeroed (recommended).** In `controller_actions::sync`, when
   `right_stick = vr`, feed the look vector into `right_stick` instead of the pad and publish
   `{ turn, stance }` beside the pad. On the game thread, once per frame (the view-params hook the
   camera already uses), add `turn` to aimer +0x14. Pitch: hold aimer +0x18 at 0 (head pitch only),
   which also fixes #5's tilted horizon; with #12 the controller supplies aim pitch.
   - Fallback when the aimer chain is null (vehicles, scripted sections): pass the stick to the pad
     unchanged, so nothing breaks.
2. **Synthesised pad stick pulses.** Turn snap angles into a timed right-stick deflection on the pad.
   No memory writes, but the angle depends on the game's sensitivity and acceleration curve; only if
   aimer writes misbehave.

### Stance (needs RE: game session)

- **Find the current stance.** Candidates: a pose enum on the local soldier (Frostbite's
  `CharacterPoseType`: stand 0, crouch 1, prone 2) on `ClientSoldierEntity` or its character physics
  component. `python -E -P tools/typeinfo_dump.py` names fields; `tools/object_probe.py` on the local
  soldier `[0x14230F260]`; crouch/prone in game and diff. Record in `re/symbols/bf4-179547.md`.
- **Request a stance**, best first:
  1. Pad pulses on the virtual pad: BF4's controller default is B = crouch (toggle), hold B = prone
     (check the bindings in Options → Controls). The adapter compares `next_stance` with the current
     stance and sends a tap or a hold (~0.4 s). Engine-free apart from reading the stance.
  2. A writable "wanted pose" / input field next to the current pose, if the typeinfo shows one.
  3. The game's input action values (`EntryInputActionEnum`-style crouch/prone actions), if the pad
     route has latency or toggles out of sync.
- Stance changes need the current stance to stay in sync after deaths, cutscenes and ladders: always
  derive the target from the read stance, never from our own count.

### Laser pointer

1. **Cursor from uv (recommended).** When the theater screen (menus) is shown, run `point_at` for
   the hand that last pulled its trigger, map uv to the game window's client rectangle, move the
   cursor there and send a left click on the trigger edge.
   - **RE (game session):** how BF4 reads the mouse in menus: its DirectInput mouse device (we are
     the `dinput8` proxy: wrap `CreateDevice(GUID_SysMouse)` and return absolute positions), raw
     input (`WM_INPUT`), or `GetCursorPos`/`WM_MOUSEMOVE`. Check that the menus accept a cursor we
     move while `keep_focus` hides focus loss.
2. **Pad navigation from the pointer.** Translate pointer movement into D-pad steps. Works without
   mouse RE but feels worse; fallback only.

Drawing: one small quad layer for the dot at the hit point and a thin quad (or cylinder layer) from
the aim pose to it, both from our own swap chain with alpha. Gameplay input stays untouched while
no menu is shown. Setting `menu_input = pointer | pad`.

## Must not break

- The pad mapping for everything else (move, buttons, triggers), the virtual pad without a physical
  pad, the multiplayer guard, arm IK (reads grip poses), menus on the theater screen.
- Vehicles and scripted sections: stick falls back to the pad.

## First session (game)

1. RE the stance field (above); 15 min of crouch/prone diffs is enough to find it.
2. Wire `right_stick` behind `right_stick = vr` (default `pad`), aimer yaw writes on the game thread,
   pad right stick zeroed, stance via B pulses. Monitor check with pipe `pad` and screenshots.
3. RE the menu mouse path; wire the pointer for the theater screen behind `menu_input = pointer`.
4. Batch the headset checks into `docs/HEADSET-TESTS.md` (snap feel, smooth speed, flick reliability,
   no stance on diagonals, pointer accuracy at the screen edges).

## Done when

In the headset: snap turn is instant and never double-fires, smooth turn is comfortable, a flick down
crouches (again: prone), a flick up stands, diagonals never trigger both; menus work with only the
pointer and trigger; both presets build warning-free and the tests pass.
