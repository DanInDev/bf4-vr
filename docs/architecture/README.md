# Technical notes

The developer side of BF4-VR: how the mod is built, how it works with the Frostbite engine, and where the
research lives. The player-facing overview is the [root README](../../README.md).

## Documents

| Document | What it covers |
| --- | --- |
| [overview.md](overview.md) | Modules, start-up order, what happens in one frame, build targets |
| [engine.md](engine.md) | How the mod reads and changes the engine: camera, both eyes, HUD, skeleton, weapons, why multiplayer is out |
| [../SAFETY.md](../SAFETY.md) | Rules that keep your EA account and other games safe. **Read before running anything** |
| [../SETUP.md](../SETUP.md) | What is installed on the development PC, connecting the Quest 3 |
| [../TOOLING.md](../TOOLING.md) | Launcher, development loop, control pipe commands, probes, headset emulation |
| [../HEADSET-TESTS.md](../HEADSET-TESTS.md) | What was tested in the headset and what is waiting |
| [../UNATTENDED.md](../UNATTENDED.md) | How agents work through issues without the owner, with enforced verification |
| [../FINDINGS.md](../FINDINGS.md) | First verified facts about `bf4.exe`, `bf3.exe` and the PC (2026-10-22) |
| [../tasks/](../tasks) | Self-contained task briefs per feature area |
| [../venice-unleashed.md](../venice-unleashed.md) | Venice Unleashed (BF3 community servers), reference |
| [../../re/README.md](../../re/README.md) | Reverse-engineering workspace and method |
| [../../re/MAPPING.md](../../re/MAPPING.md) | Engine elements still to map, and how each was or can be found |
| [../../re/symbols/bf4-179547.md](../../re/symbols/bf4-179547.md) | Verified addresses and offsets for BF4 build 179547 |
| [../../re/notes/](../../re/notes) | Evidence per topic: camera, stereo, skeleton, weapon, HUD, sound, haptics, server |

New notes that explain *how something works* belong in this folder; evidence for a single engine finding
belongs in `re/notes/`, and every verified address in `re/symbols/`.

## Repository layout

```
bf-vr/
├─ assets/          launcher icon and wallpaper (and their sources)
├─ docs/            safety, setup, tooling, headset tests, task briefs; architecture/ (this folder)
├─ re/              reverse-engineering notes and symbols (dumps and projects are git-ignored)
├─ src/
│  ├─ loader/       dinput8.dll proxy: the game loads it, it starts core
│  ├─ core/         start-up order, settings fan-out, per-frame tick
│  ├─ common/       log, bfvr.ini, live settings registry and catalogue
│  ├─ safety/       multiplayer guard
│  ├─ hooks/        Present, swap chain, game window, XInput, rumble (MinHook)
│  ├─ xr/           OpenXR session, layers, controller actions, runtime selection
│  ├─ stereo/       camera hook, both eyes per frame, HUD layer, scope, foveation, extra meshes
│  ├─ body/         arm IK, fingers, torso on the first-person rig
│  ├─ input/        virtual pad, button map, turning, stance, gestures, haptics
│  ├─ weapon/       reload, grenades, holsters, two hands, shot origin, sounds
│  ├─ ui/           in-VR settings panel, overlays, HUD element toggles
│  ├─ control/      \\.\pipe\bfvr: status, screenshots, research commands
│  └─ launcher/     bfvr_launcher.exe: set BF4 up for VR, install or remove the mod, settings, start
├─ tests/           unit tests of the engine-free code (ctest), launcher_test, smoke_theater
├─ tools/           dev-run, pipe, deploy, package, gate, probes (Python), bfvr.ini.example
└─ CMakeLists.txt   presets bf4 (x64, the product) and bf3 (Win32, engine-free part only)
```

## Build and run

```powershell
# 1. Check the environment
powershell -ExecutionPolicy Bypass -File tools\check-env.ps1

# 2. Configure and build (fetches the OpenXR SDK and MinHook)
cmake --preset bf4
cmake --build --preset bf4-debug

# 3. Smoke test without the game (headset on and Link active to see the screen)
build\bf4\Debug\bfvr_smoke_theater.exe 30

# 4. Install into the game (BF4 closed), check, and remove again
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Action deploy
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Action status
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Action remove
```

The development loop (build, deploy, start BF4, resume the campaign) is `tools\dev-run.ps1 -Resume`;
`tools\gate.ps1` must be green before anything is called done (both presets warning-free, all unit tests,
no forbidden files). Details in [TOOLING.md](../TOOLING.md).

### The launcher

`bfvr_launcher.exe` finds BF4 through the registry, checks build 179547, installs or removes the
`dinput8.dll` beside the game (the same ownership marker as `deploy.ps1`, so both tools work together), edits
every setting in `bfvr.ini` (the same pages as the in-VR panel, plus the start-up settings), starts BF4
through the EA app and opens `bfvr.log`. **Remove mod** takes the DLL out again and is required before any
multiplayer session. `tools\package.ps1` builds the release zip for players.

## Settings

Settings live in `bfvr.ini` beside the game (template: [`tools/bfvr.ini.example`](../../tools/bfvr.ini.example)).
`theater = 0` loads the DLL without VR; `xr_runtime` picks Steam Link, Meta Link or a simulator. Most settings
change live from the in-VR panel; `tools/game.ps1` starts, stops, screenshots and sends input to BF4.

## Controller mapping in menus (theater mode)

In menus, loading screens and cutscenes the game is shown on a flat virtual screen and the Quest controllers
act as an Xbox pad:

| Quest controller | Xbox pad | BF4 default (controller) |
| --- | --- | --- |
| Left stick / click | Left stick / LS | Move / sprint |
| Right stick / click | Right stick / RS | Look / melee |
| Right trigger · left trigger | RT · LT | Fire · aim |
| Right grip · left grip | RB · LB | As bound in BF4 |
| A · B (right) | A · B | Jump · crouch |
| X · Y (left) | X · D-pad down | The game's menu · fire mode (weapons change at the holsters, not by a button) |
| Left menu (≡) | Start | Pause menu (short press). **Hold** for the BF4-VR settings panel |
| Both stick clicks, held ~0.5 s | (none) | Opens or closes the settings panel too (SteamVR can take the ≡ button for its dashboard) |

D-pad and Back have no button of their own: map one on the Controls page. BF4 reads the pad through its own
XInput calls, merged with any real pad on slot 1. Keep the BF4 window focused on the desktop: unfocused, the
game drops to 30 fps.

The controller and grab-zone pictures in the root README come from `tools/body_maps.py`, which copies its zone
offsets and bindings from the code; run `python -E -P tools\body_maps.py` after changing either.
