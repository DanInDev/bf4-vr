# Task brief: HUD on the body by gesture, markers on their targets

Written 2026-10-26 (`feature/vr-ux-design`). Read `CLAUDE.md`, `docs/SAFETY.md` (binding) and this
file first. Issues: #26 (gestures), #27 (markers), #28 (element toggles); parent #3.

## Goal

The user's item 5 from 2026-10-26 (their ideas; alternatives below under "Options"):

- (a) **Ammo:** turn the gun to inspect it and hold it steady for ~1 s → the ammo counter appears and
  stays until you look away.
- (b) **Minimap:** appears when you look at the inside of the forearm, or can be dragged out from
  somewhere.
- (c) **Markers:** bring the HUD layer closer or re-project it so friendly/squad markers line up with
  their targets again.
- Item 6 (in the settings: toggle individual HUD elements with a click) shares the per-element
  machinery and is covered here and in `docs/tasks/vr-settings.md`.

## What exists

| Piece | Where | State |
| --- | --- | --- |
| HUD capture | `src/stereo/hud_capture.*`: while the UI render module (`0x1408863E0`, Twinkle) runs on a head-tracked frame, binding the back buffer binds our transparent texture instead | Works; the HUD is out of the eye images (headset 2026-10-25: "really cool") |
| HUD layer | `src/xr/theater.cpp`: one quad in VIEW space, `hud_distance` 1.5 m, `hud_width` 1.3 m (≈ 47°), premultiplied alpha, whole texture | Works. Markers are off their targets ("a little distracting") |
| Hand poses | `stereo::hand_sample` (grip, aim), head `eye_math::centre_pose` | Works |
| Rendered view of a frame | `stereo::latest_rendered_view()` (eye pose and symmetric FOV the game rendered with), `eye_math::symmetric_fov` | Works |
| UI draw tracing | pipe `frametrace` (calls, callers and stacks per frame) | Works; not yet used on the UI module |
| **Gesture detectors** (new) | `src/input/gestures.*`, `tests/gestures_test.cpp` | Engine-free, tested; not wired in |

### The detectors (`input::gestures`)

Pure functions plus small state machines over OpenXR LOCAL poses; all thresholds are config fields
(registry settings later).

- `forearm_glance` (minimap): a point on the forearm (`offset` from the grip pose, 12 cm towards the
  elbow) within `show_cone` (20°) of the gaze, closer than 0.8 m, and the palm normal (the inner
  forearm's normal) within 60° of the direction to the eyes. Shows after `dwell` 0.25 s; stays while
  the point is within `hide_cone` (35°) and the inner side still faces the head; hides 0.3 s after
  that fails or tracking is lost. Mirrors for the right hand.
- `gun_inspect` (ammo): a point on the gun within 20° of the gaze, closer than 0.7 m, the gun's side
  (grip x) facing the eyes (|cos| ≥ 0.6; aiming down the sights is ≈ 0, which keeps ADS from ever
  triggering), and steady: low-passed linear speed ≤ 0.15 m/s and angular speed ≤ 0.6 rad/s. Shows
  after 1 s steady (`progress()` for a fill-up ring); then stays until the gaze leaves `hide_cone`
  (40°) for 0.3 s, even if the gun is turned back.
- Grip axis convention (OpenXR spec): −z along the handle, +x = palm normal (out of the palm on the
  left hand, into it on the right). **Verify on SteamVR's Touch bindings in the headset**: if the
  palm normal is flipped there, the watch pose becomes "back of the hand" (one sign in config).

## Options for showing single elements, best first

1. **Crop by screen region (no RE, recommended first step).** The minimap and ammo counter sit at
   fixed places in the captured HUD texture (bottom-left, bottom-right at 16:9). Keep one HUD swap
   chain and submit several quad layers that each show a `subImage.imageRect` of it: the minimap
   rectangle on the left forearm (pose = left grip × offset, facing the eyes), the ammo rectangle on
   the gun's left side (right grip × offset). The head-locked HUD quad is then drawn in pieces (or
   with those rectangles cleared in a copy) so the elements do not appear twice. Rectangles are uv
   settings, tuned once per HUD layout. Fade in/out by layer alpha (`XR_KHR_composition_layer_color_scale_bias`
   if the runtime has it, else by a premultiplied copy).
2. **Per-element capture by draw-call ranges.** `frametrace` the UI module and split its draws by
   widget (scissor/viewport, bound texture atlas, or call order between known state changes). Each
   element renders into its own texture or region. Robust against layout changes, enables hiding any
   element (#28) and separating markers (#27 option 2). One RE session on the monitor.
3. **Own widgets from game values.** Read magazine and reserve ammo from the current weapon (typeinfo
   on `ClientSoldierWeapon` and its firing component; health likewise) and draw our own counter: a
   wrist watch, or a small number on the gun's side. Best look and readable at any distance;
   needs RE plus our own text (shared with the settings panel's font).
4. **Always-on wrist panel, dimmed until glanced at** (Half-Life: Alyx style). No gesture to learn;
   good fallback if the inspect gesture misfires in firefights.
5. **Drag out (the user's "dragged from somewhere").** Grab (grip) near the left wrist and pull: the
   minimap quad follows the hand and stays world-locked where released, until grabbed back. Can be
   added to option 1 or 3 later with the same quad.

## Markers (#27)

Why they are off: the game projects each marker with the rendered camera (FOV ≈ 100° or more per
`symmetric_fov`), but the HUD quad covers ≈ 47° at 1.5 m and follows the live head, so a marker at
screen position p appears at a different angle than its target, and at 1.5 m instead of the
target's depth.

1. **Match the projection (cheapest).** Size the quad to subtend exactly the rendered FOV:
   width = 2 d tan(h), height = 2 d tan(v) at distance d, and pose it with the rendered view pose
   (`latest_rendered_view().eye_pose`, or the left eye of the pair) in LOCAL space instead of the live
   head in VIEW space. Directions then match the world image of the same frame. Use d of 10–20 m so
   convergence roughly matches far targets. The fixed elements move into the lens corners with this,
   so it ships together with option 1 above (minimap/ammo cropped off and placed on the body). Behind
   `hud_markers = projected`; monitor check with `fakehead`, headset test with squad markers.
2. **Separate markers layer** (needs option 2's draw split): only marker draws get the projected quad;
   fixed elements keep a comfortable head-locked quad.
3. **Per-marker depth.** Sample the resolved scene depth (already located for reprojection,
   `docs/tasks/smooth-stereo.md` direction C) at each marker's pixel, or read marker world positions
   (RE: HUD marker list), and place each marker as a small quad at that depth. Correct in both eyes;
   most work.
4. **Own markers** from squad soldier positions (RE: squad list), drawn as world-locked billboards.
   Best quality, most RE.

## Element toggles (#28)

- Region mask (option 1's rectangles) for fixed elements; draw-call skip (option 2) for everything,
  including crosshair and hit markers; the game's own HUD options (Options → Gameplay: minimap,
  crosshair, hit indicators) found through typeinfo where one exists, which is cheapest.
- Each element gets `hud_show_<element>` (bool) in the settings registry; the in-VR panel toggles it
  with `step` (one click).

## Must not break

- HUD capture and the head-locked layer as the default (`hud_layer = 1`); menus and loading screens
  on the theater screen; the eye images stay HUD-free.
- No gesture may trigger while aiming down the sights or during normal play (headset-test this in a
  firefight, not only standing still).

## First session

Engine-free and monitor parts can run while the game session works elsewhere; the headset test is
batched.

1. Option 1: uv rectangles for minimap and ammo as settings; extra quad layers from the HUD swap
   chain; wrist and gun poses; detectors wired with a fade. Check on the monitor with `fakehead` and
   pipe `hudshot`.
2. Markers option 1 behind `hud_markers = projected`.
3. Headset checklist in `docs/HEADSET-TESTS.md`: glance and inspect reliability, false triggers while
   aiming and sprinting, minimap/ammo readability, marker alignment on squad mates.
4. If the region crop proves fragile: RE session for option 2 (`frametrace` on the UI module).

## Done when

In the headset: the ammo counter appears on the gun after a steady inspect and leaves when you look
away; the minimap appears on the forearm glance; neither triggers while aiming; squad markers sit on
their people; individual elements can be hidden from the settings; both presets build warning-free
and the tests pass.
