# Camera (BF4 build 179547, x64)

Status: **camera hooked; per-eye camera control and FOV control verified on screen (2026-10-23).
Alternate-eye stereo implemented (`stereo = 1`), awaiting the first headset test.**

## Verified chain

| Step | Value | How verified |
| --- | --- | --- |
| Global `GameRenderer*` | `[0x142672378]` (in `.data`) | Live reads (`tools/camera_probe.py`) |
| View params | `[GameRenderer + 0x60]` → block of **three RenderViews** (`+0x0` world camera, `+0x4E0` probably first-person view, `+0x9C0` unknown) plus extra data | Disassembly of the copy function below |
| Camera world transform | `RenderView + 0x40`: rows right, up, forward, position (row vectors, x right / y up / z forward, metres) | Strafing moved position along row 0 |
| Vertical FOV | `RenderView + 0xB4` (radians). `+0xB8` = 0.96 rad = the player's FOV setting (BF4's "70 (Hor+ 86)" = 55.3° vertical) | Overriding `+0xB4` to 1.6 rebuilt the projection to exactly 91.7° vertical |
| Aspect | `RenderView + 0xC4` (1.778 = 16:9) | Projection m11/m00 = 1.778 |
| Dirty flags | `RenderView + 0xE0`; set bits = cached matrices invalid. Setting `0x7` after editing makes the engine rebuild them | Projection rebuilt after FOV override; image shifted after position edit |
| View matrix | `RenderView + 0x260` (inverse of the transform) | Math matches the transform |
| Projection matrices | `+0x320`, `+0x3A0` (and copies `+0x800`, `+0x880`); `+0x3E0`/`+0x8C0` another projection (88° × 57°, aspect 0.563) | `tools/projection_probe.py` |

## Who writes it

| Address | Role | Evidence |
| --- | --- | --- |
| `0x140B79700` | Leaf copy of the 0xDA-byte view description (transform, FOV, aspect…) | Hardware write-watch on `RenderView+0x70`: the only writer, 200 hits/s at 200 fps |
| `0x140B79380` | `RenderView` copy: description + cached matrices, gated by the dirty flags | Disassembly (`tools/disasm.py`) |
| `0x14083A710` | **View-params copy** (3 RenderViews + extras), once per frame on the game thread. **Hooked** | Watch backtrace `0x140B79395 → 0x14083A725 → 0x140848C03 → 0x14083F1CF → 0x14082891E → 0x14000F089` |

Timing: exactly one copy per Present (counts equal). The copy runs on the game thread, Present on
the render thread, and the presented image always belongs to the **latest** copy (4/4
alternating-offset screenshots matched the latest copy's parity).

## Experiments (screenshots in `re/screenshots/`, git-ignored)

- `eye ±1.0`: world shifted sideways correctly; the weapon did not move (separate first-person view).
- `fov 1.6` / `fov 0.5`: wide / zoomed world view; weapon unchanged.

## Alternate-eye stereo as built (`src/stereo/camera_hook.cpp`, `src/xr/theater.cpp`)

1. The XR waiter thread samples both eye poses and FOVs (`xrLocateViews`, LOCAL space) for each predicted display time.
2. The copy hook alternates eyes (odd copy = left, even = right). It composes the eye pose onto the game camera **unchanged**, for the world view and the first-person view. The camera's local axes match OpenXR (x right, y up, looking along −z); a first build with a z-mirror ran yaw and pitch backwards in the headset (user test 2026-10-23). It then sets `+0xB4` to cover the headset's vertical FOV, marks matrices dirty, and publishes {eye, pose, symmetric FOV}. Menu/loading frames (identity camera at (0, 1, 0)) are left mono, and the presenter falls back to the theater screen.
3. Each Present copies the back buffer into that eye's texture; each headset frame submits a projection layer (array swap chain, slice 0 = left, 1 = right) with exactly the pose and FOV each eye was rendered with.

## Second headset test (2026-10-23) and follow-up experiments

User report after the rotation fix: menus readable (theater fallback), head look works, but a
fish-eye impression, the gun glued under the eyes, and the HUD crosshair locked to the view.

Experiments (mono, `pipe view <i> <fov> <shift>`, screenshots `re/screenshots/views-*.png`):

| Change | World | Weapon |
| --- | --- | --- |
| View 0 (`+0x0`) shifted 0.3 m | moves | **does not move** |
| View 1 (`+0x4E0`) shifted 0.3 m / FOV 1.6 | no change | no change |
| View 2 (`+0x9C0`) shifted 0.3 m | no change | no change |

- View 1 is an exact copy of view 0 (same transform and 45° FOV, flags 0) and has no visible effect: probably a previous-frame or culling copy, **not** the first-person view. `stereo_first_person` should default to off.
- View 2: 90° × 90°, aspect 1.333, at the origin looking +z, flags 0xF; role unknown (environment/reflection?).
- **The weapon is drawn in a separate first-person pass with its own camera and FOV, locked to the screen.** In alternate-eye stereo it sits at the same screen spot in both eyes, so it reads as distant and head-locked, and it is drawn with its ~55° FOV inside a 110° image (looks oversized). The fix needs that pass's camera: apply the inverse head pose there so the gun stays where the soldier aims while the head looks around. Next research target.
- The HUD crosshair is likewise screen-locked; the true aim point is the game camera's forward axis.

## Per-eye aspect (2026-10-25)

The world view's aspect (+0xC4) is set once per frame after our copy hook by `0x140B89D60`
(RenderView set-aspect: stores +0xC4 = viewport width/height, recomputes the horizontal FOV at
+0x250, marks the matrices dirty; prologue `40 53 48 83 EC 30 0F 29 74 24 20 0F 28 F1 48 8B D9`;
caller `0x140848E4C`). Writing +0xC4 ourselves is overwritten. The hook replaces the value: with
`aspect 1.0` the projection became m00 = m11 = 2.414 (horizontal FOV 72.7° → 45°) and the world and
the gun stretched together, so the first-person pass uses the same aspect. In stereo
(`stereo_aspect = headset`) the aspect is tan(half horizontal)/tan(half vertical) of the headset's
widest eye FOVs, so the 1920×1080 image covers exactly the lens FOV (anamorphic; the compositor maps
it with the submitted FOV). Window resizing does not help: the game keeps a 1920×1080 borderless window.

## Open questions

1. ~~Find the first-person weapon pass's camera~~: view-params `+0xED0`, see [first_person.md](first_person.md). Its projection/FOV is still open.
2. Third RenderView `+0x9C0`: role unknown.
3. Game pitch from the stick/mouse adds to head pitch; ordinary gameplay probably needs the game pitch neutralised.
