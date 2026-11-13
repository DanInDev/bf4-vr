# Architecture

How the mod is built, as of 2026-11-10 (`main`). How it reads and changes the game engine is in
[engine.md](engine.md); addresses are in [`re/symbols/bf4-179547.md`](../../re/symbols/bf4-179547.md).

## In one picture

```
bf4.exe ──loads──► dinput8.dll (src/loader)        proxy: forwards DirectInput8Create, starts core
                        │
                        ▼
                 core (src/core)                    start-up order, settings fan-out, per-Present tick
                        │
   ┌──────────────┬─────┴────────┬──────────────────┬───────────────┐
   ▼              ▼              ▼                  ▼               ▼
 safety         hooks          stereo             xr              control
 MP guard       Present,       camera, both       OpenXR          pipe: status, shots,
                XInput,        eyes per frame,    session,        research commands
                window,        HUD layer, scope   layers, hands
                swap chain
                                 │                  ▲
          ┌──────────────┬───────┴──────┐           │ latest head / hand / rendered-view samples
          ▼              ▼              ▼           │ (src/stereo/tracking.hpp)
        body           input          weapon ───────┘
        arm IK,        pad, turn,     reload, grenades, holsters,
        fingers        stance, aim    two hands, shot origin        ui: settings panel, overlays
```

The design follows Overlord's split: a thin loader, then infrastructure (hooks, VR runtime,
rendering), then gameplay on top. Gameplay code never calls OpenXR: it reads the latest published
head and hand samples and writes a virtual gamepad.

Two rules shape most files:

- **Engine-free logic is separate from the game adapter.** A state machine or a piece of maths sits
  in its own file with a unit test (`weapon/reload`, `weapon/two_hand`, `stereo/eye_math`,
  `body/arm_ik`); a second file connects it to BF4 (`weapon/physical_reload`, `weapon/two_handed`,
  `stereo/camera_hook`, `body/arm_pose`). The similar names are the two halves of one feature.
- **Every feature has a live setting** (`common/settings_store.cpp`), a row on the in-VR panel and
  the launcher (`common/setting_catalog.cpp`) and a line in `tools/bfvr.ini.example`.

## Start-up

`DllMain` only logs and starts one thread (nothing else under the loader lock). That thread
(`core.cpp`) runs in this order:

1. `safety`: stop here on a multiplayer command line; start the level guard thread.
2. Load `bfvr.ini` into the settings registry.
3. Swap chain hooks (before the game creates its swap chain).
4. Engine hooks: camera, HUD capture, arm pose, shot origin. Each waits on its own short-lived
   thread until the DRM has decrypted the code and the expected bytes are there.
5. Control pipe (`\\.\pipe\bfvr`).
6. The OpenXR "theater" object (never destroyed: tearing the runtime down at process exit can deadlock).
7. Present hook; then HUD widgets, XInput hooks, rumble hook and VR controls.

Installed later, when first needed: the per-view render hook (first stereo frame), the D3D11
context hooks (first eye pass), the HUD hooks (first matching Present).

## One frame

Four threads take part. They exchange data only through the lock-free "latest sample" slots in
`stereo/tracking.hpp`.

| Thread | Entry point | What happens |
| --- | --- | --- |
| XR waiter (ours) | `xr/theater.cpp` | `xrWaitFrame`, locate both eyes, publish the head sample. Hidden frames are begun and ended here, so a headset that is taken off never slows the game |
| Game thread | `stereo/camera_hook.cpp`, on the engine's per-frame view copy | Stick turn; menu / cutscene / level check (flat screen or head-tracked); left eye pose and a symmetric cropped FOV written into the camera; controller aim (camera at the body yaw, aimer along the gun); physical stance; gun size; scope planning; publishes what was rendered |
| Animation jobs | `body/arm_pose.cpp`, on the pose conversion | First-person rig after animation, before skinning: arm IK to the controllers, gun on the hand, fingers, torso; publishes the gun's points (magazine, slide, sight line) |
| Render thread | `stereo/eye_pass.cpp`, on the per-view world render | Renders the view **twice in one game frame**: left eye, then every constant buffer the left eye used is copied, patched for the right eye and redirected, then the right eye. An optional third render fills a magnified scope. Foveation (NVIDIA VRS) and extra meshes (second magazine, held grenade) attach here |
| Render thread | `stereo/hud_capture.cpp`, on the UI render | The HUD is drawn into its own transparent texture instead of the eye image; the scene depth along the shot line gives the aim dot's distance |
| Render thread | `core.cpp` `on_present` → `xr/theater.cpp` | Settings fan-out, weapon and UI updates, controller sync, then one OpenXR frame: a projection layer with exactly the pose and FOV each eye was rendered with, plus quad layers (vignette, HUD pieces, held grenade, scope lens, reticle, settings panel). Menus, loading screens and cutscenes go out as a flat screen with a laser pointer |

`stereo_render = pair` (both eyes per game frame) is the default; `aer` (one eye per frame,
alternating) is the older path and still works.

## Modules

| Folder | Responsibility | Talks to the engine? |
| --- | --- | --- |
| `loader` | `dinput8.dll` proxy; wraps the game's DirectInput so it sees a virtual pad | no |
| `core` | Start-up order, settings fan-out, per-Present tick, status text | no |
| `common` | Log, `bfvr.ini`, the settings registry, the setting catalogue (names and pages for the panel and launcher) | no |
| `safety` | Multiplayer guard: command-line check and a level check four times a second; a multiplayer level disables every hook for the rest of the process | reads the level |
| `hooks` | Present, swap chain and output (virtual render size), game window (focus, cursor, raw input keys), XInput, rumble; two opt-in probes (`dxgi_probe`, `nvapi_hook`) left from the 3D Vision investigation | mostly Windows APIs |
| `xr` | OpenXR instance and session on the game's own D3D11 device, waiter thread, composition layers, controller actions, runtime selection | no |
| `stereo` | Camera hook, pair rendering and constant patching, HUD capture and layout, scope, foveation, extra meshes, aim depth, gun pass FOV, cutscene and walk-sway filters | yes: the centre of it |
| `body` | Arm IK, fingers, grips, torso, recoil on the first-person rig | `arm_pose` only |
| `input` | Virtual pad, button map, trigger latch, right stick (turn and stance), gestures, pointer, haptics mixer, handedness; `vr_controls` reads and writes the game's aim, stance and health | `vr_controls`, half of `virtual_pad` |
| `weapon` | Physical reload, grenades, holsters and weapon swap, two-handed grip, free hands, melee, use reach, shot origin, hit feedback, reload sounds | the adapter half of each feature |
| `ui` | In-VR settings panel, vignette, reticle, held grenade overlay, HUD element toggles | `hud_widgets` only |
| `control` | The pipe server and its commands; research tools: crash log, frame trace, thread stacks, hardware watchpoint | generic memory access |
| `launcher` | `bfvr_launcher.exe` (built with the bf4 preset): finds the game, checks the build, installs or removes the DLL, sets BF4 up for VR, edits `bfvr.ini`, starts the game. Never loads the mod. Two games, switched in the header or with `--game bf4\|bf3`; what differs is one `game_profile` each in `launcher/install.hpp`. Battlefield 3 gets the 32-bit research build from the bf3 preset (`build\bf3\<config>\dinput8.dll`, or the folder `bf3` beside the launcher) and no change to the game's own settings | no |

`stereo/world_pass.cpp` is research only (rendering the whole world module twice crashes); it is
kept as a record and is reachable only through research pipe commands.

## Settings

- **Start-up keys** (`common/config`): read once from `bfvr.ini` beside the game exe, with a
  `BFVR_<KEY>` environment override (`theater`, `stereo`, `xr_runtime`, `render_size`, `research`, …).
- **Live settings** (`common/settings`, `settings_store`): one typed registry. A change bumps its
  revision; on the next Present `core::apply_settings()` pushes the values into the modules. Changes
  come from the pipe (`setting list|get|set|step|reset|save`), the in-VR panel or the launcher, and
  reach the file only on `save` (comments and other keys are kept).

## Control pipe and research mode

`\\.\pipe\bfvr`, one text command per connection, local clients only (`tools\pipe.ps1`). `status`,
screenshots, settings and each feature's state report are always available; memory and experiment
commands (`dump`, `peek`/`poke`, `watch`, `scan`, `fakehead`, `pad`, `eyepass`, …) need
`research = 1` (the default in Debug builds). The full list is in [TOOLING.md](../TOOLING.md).

## Build

| Target | Content |
| --- | --- |
| `bfvr_common` | Engine-free base: log, config, settings, button map |
| `bfvr_runtime` | Everything else that runs in the game |
| `bfvr_loader` | `dinput8.dll` |
| `bfvr_launcher` | Launcher (bf4 preset only) |
| tests | 31 ctest unit tests of the engine-free halves, `bfvr_launcher_test`, and `bfvr_smoke_theater` (hooks and OpenXR without the game) |

MinHook and the OpenXR loader are fetched by CMake. Presets: `bf4` (x64, the product) and `bf3`
(Win32).

### What the `bf3` preset is

Both presets compile the same sources. All BF4 knowledge sits behind `#if defined(_M_X64)`, so the
32-bit build is the engine-free part: loader, settings, Present and XInput hooks, the flat theater
screen and the control pipe. It has no camera, arms, HUD layer or weapon features. Its one piece of
Battlefield 3 knowledge is the multiplayer guard (`safety/`): the command line must ask for single
player, the exe must be build 1147186, and the loaded level is read every 250 ms. The research for
Battlefield 3 lives in its own repository, `bf3-vr`.

## Known debts

- **Engine constants are spread over the files that use them.** The local soldier pointer is
  defined in 8 files, the soldier-to-weapon chain in 5, the renderer pointer in 3, and 10 files have
  their own memory-read helper. The registry is the document `re/symbols/bf4-179547.md`, not a
  header. A port to another build or game should start by collecting them in one place.
- `OMSetRenderTargets` is hooked by two modules (`hud_capture`, `scope_scissor`) that hand calls to
  each other by convention.
- Seven settings are read both as start-up keys and as live settings (two sources of defaults).

## History

Built in stages, each proven before the next: 0 the DLL loads and logs; 1 theater mode (flat screen
in the headset); 2 head-tracked stereo, first alternate-eye, then both eyes per frame; 3 arms and
weapon on the controllers; 4 weapon handling (reload, grenades, holsters, two hands, scopes). Open
work is tracked in GitHub issues; the roadmap is issue #21.
