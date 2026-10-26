# Task brief: live VR settings (registry, pipe, in-VR panel, persistence)

Written 2026-10-26 (`feature/vr-ux-design`). Read `CLAUDE.md`, `docs/SAFETY.md` (binding) and this
file first. Issues: #25 (live settings), #28 (HUD element toggles), #7 (in-VR menu), #8 (FOV and
render scale).

## Goal

The user's items 4 and 6 from 2026-10-26: tune values by hand while playing, from the headset or the
desktop, and keep them. Named so far: chest/shoulder position, gun scale, IPD / eye offset, HUD
distance and size, view crop, and individual HUD elements on/off "with a click". Every future
tunable should get the same treatment for free.

## What exists

| Piece | Where | State |
| --- | --- | --- |
| `bfvr.ini` + `BFVR_<KEY>` environment overrides | `src/common/config.*` | Read once, lazily; `get(key, fallback)` returns text. Each module parses its own keys (`std::atof`, `config_vector`) |
| Hand-written live pipe commands | `src/control/control_pipe.cpp`: `gunscale`, `viewcrop`, `armik`, `stereorender`, `aspect`, `fov`, … | Each value has its own command, parser and storage; nothing is saved |
| **Settings registry** (new) | `src/common/settings.*`, `tests/settings_test.cpp` | Engine-free, tested; in `bfvr_common`, not wired in |

### The registry (`settings::registry`)

- `add(definition)`: name (ini key), kind (`boolean`, `integer`, `real`, `choice`, `vec3`), min/max,
  step, default (ini text), choices (`"aer|pair"`), one-line help. Rejects duplicates, invalid
  defaults and defaults outside the range.
- `load(lookup)`: takes each value from a lookup (wire it to `config::get` with a sentinel fallback
  so "missing" is distinguishable). Out-of-range numbers are clamped, unparsable text keeps the
  default; both produce warnings for `bfvr.log`.
- Reads: `boolean`, `integer`, `real`, `text`, `get` (all components). Thread-safe (one mutex).
- `revision()` moves on every real change: per-frame consumers cache values and re-read only when it
  moved.
- Writes: `set(name, text)` (rejects out of range, says why), `step(name, n)` (booleans toggle,
  choices cycle, numbers move by `step`, clamped: exactly what panel buttons need), `reset(name)`.
- Pipe text: `command("list" | "get n" | "set n v" | "step n [k]" | "reset n")`. `list` prints
  `gun_scale = 0.8  [float 0.25..2, default 1] *  apparent gun size` (`*` = changed, unsaved).
- `merge_ini(existing)`: rewrites the changed keys' lines in place (indentation and trailing comment
  kept), appends keys that are missing, keeps every other line and the line endings. `mark_saved()`
  after writing.

## Tunables (first set)

| Setting | Kind, range | Today | Consumer | Live? |
| --- | --- | --- | --- | --- |
| `gun_scale` | real 0.25..2 | ini + pipe `gunscale` | camera hook | yes |
| `view_crop_h`, `view_crop_v` | real 0.4..1 | ini + pipe `viewcrop` | camera hook | yes |
| `hud_distance`, `hud_width` | real 0.3..20 / 0.2..10 | ini only (read once in the theater) | theater HUD quad | yes (quad pose/size per frame) |
| `hud_layer` | bool | ini only | theater + HUD capture | restart (capture install) |
| `stereo_render` | choice aer\|pair | ini + pipe | eye pass | yes |
| `arm_ik`, `arm_ik_weapon` | bool | ini + pipe `armik` | arm pose | yes |
| `arm_ik_wrist`, `arm_ik_pole` | vec3 | ini only | arm pose | yes |
| `shoulder_offset` (new) | vec3 −0.3..0.3 m | none | arm IK: moves the shoulder anchor (camera axes) before solving; "chest/shoulder position" | yes |
| `chest_offset` (new, #13) | vec3 | none | body presence (torso below the head) | yes |
| `ipd_scale` (new) | real 0.5..2 | none | eye offset multiplier = world scale (1 = runtime IPD); eye pass + camera hook | yes |
| `turn_mode`, `snap_angle`, `smooth_turn_speed`, `stick_deadzone`, stance thresholds (new) | choice / int degrees / real | none | `input::right_stick` (#22) | yes |
| `menu_input` (new) | choice pointer\|pad | none | laser pointer (#23) | yes |
| `hud_show_<element>` (new, #28) | bool | none | HUD layer masks / draw skips | yes |
| gesture thresholds (new, #26) | real | none | `input::gestures` | yes |
| `render_size`, `xr_runtime`, `theater`, `stereo` | — | ini | start-up | restart only: list them, mark "restart" |

## Approaches, most promising first

### 1. One registry, three front ends (recommended)

1. **Registry instance** owned by `core` and filled at start-up: each module registers its own
   settings next to the code that uses them (`register_settings(registry&)`), then one `load` from
   `config::get`. Warnings go to `bfvr.log`. Start-up-only settings get a `restart` note in their
   help text.
2. **Pipe command** `setting <list|get|set|step|reset|save>` in `control_pipe.cpp` forwards to
   `registry::command`; `save` reads `bfvr.ini`, `merge_ini`, writes `bfvr.ini.tmp`, then
   `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` and `mark_saved`. The existing hand-written commands
   (`gunscale`, `viewcrop`, …) become thin aliases so `tools/pipe.ps1` users keep working.
3. **Desktop tool** `tools/settings.ps1` (or a small Python TUI, `python -E -P`): list, arrow keys to
   pick, left/right to `step`, `s` to save. Lets the user tune with the headset on while someone (or
   the user, peeking) drives the desktop. Cheapest way to get item 4 working.
4. **In-VR panel** (#7): our own quad layer in LOCAL space (world-locked when opened, ~0.8 m ahead,
   where the theater screen sits for menus), drawn into our own swap chain on the game's D3D11 device
   at the start of the theater frame. Rows = registry entries grouped by page (View, HUD, Body,
   Comfort, Input); each row a label, the value and −/+ buttons (`step`), booleans a toggle
   (`step 1`): this is the user's "toggle HUD elements with a click". Interaction through the laser
   pointer (`input::point_at`, #23): uv → row/button. Opened by a long press on the left menu button
   (short press stays Start for the game's pause menu); pauses the game's input while open.
   - Text rendering options: Dear ImGui (MIT, `imgui_impl_dx11` into our render target; heavy but
     complete), or a baked bitmap font + quads (tiny, enough for labels and numbers). Start with
     ImGui on a separate render target; never touch the game's device state without saving and
     restoring it (the theater already does this for its copies).
5. **Consumers** switch from `config::get` to registry reads, cached by `revision()`.

### 2. Keep per-command pipe values, add a save command

Smallest change, but every new tunable needs its own command, parser, storage and panel row. Only if
the registry wiring is blocked.

## Persistence rules

- Only values changed through the pipe or panel are written (`changed` marks); the file's comments,
  order and unknown keys stay. `merge_ini` is tested for this.
- `BFVR_<KEY>` environment variables still win at start-up (config semantics); `list` should show
  when an override is active so a saved value that "does nothing" is explainable.
- Writing happens on the pipe/panel thread, never on the render or game thread.

## Must not break

- Every current ini key keeps its name, default and meaning (`tools/bfvr.ini.example`).
- `research` stays out of the registry (release builds must not be able to switch it on live).
- The multiplayer guard: the pipe blocks everything except `status` when the mod is disabled; the
  `setting` command must sit behind that check too.

## First session (engine-free parts first, game later)

1. Registry instance in `core`; register `gun_scale`, `view_crop_h/v`, `hud_distance`, `hud_width`,
   `stereo_render`, `arm_ik*`; pipe `setting` command and `save`; aliases for the old commands.
   Testable on the monitor with `tools\pipe.ps1 setting list`.
2. `tools/settings.ps1` desktop front end.
3. New tunables `shoulder_offset`, `ipd_scale`, wired into arm IK and the eye pass; headset test.
4. In-VR panel (after the laser pointer, #23).

## Done when

Every tunable in the first table can be listed, changed and saved from the desktop and from the
headset, applies within a frame (or says "restart"), survives a restart with comments in `bfvr.ini`
intact, both presets build warning-free and the tests pass.
