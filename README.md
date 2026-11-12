# bf-vr

Hobby research project: VR support for the **Battlefield 4 single-player campaign**
on a Meta Quest 3, in the spirit
of [Overlord](https://github.com/Aeka0/Overlord) (MW2CR) and the Titanfall 2 VR mod.

Status (2026-11-10): **the campaign is playable in VR with physical gun handling.**
- Both eyes are rendered from one game frame (pair rendering) with head tracking through SteamVR /
  Steam Link; menus, loading screens and cutscenes show on a flat virtual screen with a laser pointer.
- Arms follow the controllers (IK on the game's first-person rig) and hold the gun; shots start at
  the drawn gun and fly along its barrel; two-handed grip, free hands, holsters on the body.
- Physical reload, grenades (pin, throw), melee and use by reach; haptics; HUD on its own layer with
  per-element toggles; in-VR settings panel and a desktop launcher.
- Open: magnified scopes and sights (#39), laser sights, some gadgets, the grab-zone models (#41).
  Headset results are in `docs/HEADSET-TESTS.md`; the roadmap is issue #21.
- Multiplayer: not in BF4 (see `docs/FROSTBITE.md`, last section). That research continues with
  Battlefield 3 and Venice Unleashed in the separate repository `bf3-vr`.

| Doc | What it covers |
| --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Modules, start-up, what happens in one frame, build targets |
| [docs/FROSTBITE.md](docs/FROSTBITE.md) | How the mod reads and changes the engine: camera, two eyes, HUD, skeleton, weapons |
| [docs/SAFETY.md](docs/SAFETY.md) | Rules that keep your EA account and other games safe: read first |
| [docs/SETUP.md](docs/SETUP.md) | What is installed, Quest 3 connection |
| [docs/TOOLING.md](docs/TOOLING.md) | Launcher, dev loop, control pipe commands, probes, headset emulation |
| [docs/HEADSET-TESTS.md](docs/HEADSET-TESTS.md) | What was tested in the headset and what is waiting |
| [docs/FINDINGS.md](docs/FINDINGS.md) | First verified facts about `bf4.exe`, `bf3.exe` and this PC (2026-10-22) |
| [docs/tasks/](docs/tasks) | Task briefs per feature area |
| [docs/venice-unleashed.md](docs/venice-unleashed.md) | Venice Unleashed (BF3 community servers): reference |
| [re/README.md](re/README.md) | Reverse-engineering workspace and method |
| [re/MAPPING.md](re/MAPPING.md) | Engine elements, how each was or can be found |
| [re/symbols/bf4-179547.md](re/symbols/bf4-179547.md) | Verified addresses and offsets for BF4 build 179547 |
| [re/notes/](re/notes) | Evidence per topic: camera, stereo, skeleton, weapon, HUD, sound, haptics, server |

## Layout

```
bf-vr/
├─ docs/            architecture, engine handling, safety, setup, tooling, headset tests, task briefs
├─ re/              reverse-engineering notes and symbols (dumps/projects are git-ignored)
├─ src/
│  ├─ loader/       dinput8.dll proxy: the game loads this, it starts core
│  ├─ core/         start-up order, settings fan-out, per-frame tick
│  ├─ common/       log, bfvr.ini, live settings registry and catalogue
│  ├─ safety/       multiplayer guard
│  ├─ hooks/        Present, swap chain, game window, XInput, rumble (MinHook)
│  ├─ xr/           OpenXR session, layers, controller actions, runtime selection
│  ├─ stereo/       camera hook, both eyes per frame, HUD layer, scope, foveation, extra meshes
│  ├─ body/         arm IK, fingers, torso on the first-person rig
│  ├─ input/        virtual pad, button map, turn, stance, gestures, haptics
│  ├─ weapon/       reload, grenades, holsters, two hands, shot origin, sounds
│  ├─ ui/           in-VR settings panel, overlays, HUD element toggles
│  ├─ control/      \\.\pipe\bfvr: status, screenshots, research commands
│  └─ launcher/     bfvr_launcher.exe: set BF4 up for VR, install/remove the mod, settings, start (bf4 preset only)
├─ tests/           unit tests of the engine-free code (ctest), launcher_test, smoke_theater
├─ tools/           dev-run, pipe, deploy, package, probes (python), bfvr.ini.example
└─ CMakeLists.txt   presets bf4 (x64, the product) and bf3 (Win32, engine-free part only); fetches OpenXR SDK and MinHook
```

## Quick start

```powershell
# 1. Check the environment
powershell -ExecutionPolicy Bypass -File tools\check-env.ps1

# 2. Configure + build (cmake is on the user PATH)
cmake --preset bf4
cmake --build --preset bf4-debug

# 3. Smoke test without the game (headset on and Link active to see the screen)
build\bf4\Debug\bfvr_smoke_theater.exe 30

# 4. Into the game (BF4 closed first) and back out
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Action deploy
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Action status
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Action remove
```

Or use the launcher, `build\bf4\Debug\bfvr_launcher.exe` (or `Release`): it finds BF4 through the
registry, verifies build 179547, installs or removes the `dinput8.dll` beside it (same ownership marker
as `deploy.ps1`, so both tools work together), edits every setting in `bfvr.ini` on its Settings page (the same pages as the in-VR panel, plus the ones read at start-up), starts BF4 through the
EA app and opens `bfvr.log`. **Remove mod (required before multiplayer)** takes the DLL out again.
`tools\package.ps1` builds the release zip for players (see docs/TOOLING.md).

## Controls in a level

![Quest 3 controllers in bf4-vr: what each button and gesture does](docs/img/quest3-controls.svg)

Grab by putting a hand in a zone and squeezing its grip. Circles show the real reach; `grab_debug = 1`
draws them in the headset too.

![Grab zones around the body: shoulders, chest, hips](docs/img/grab-zones.svg)

Every button on the map can be remapped: Controls page of the panel (or the launcher), with presets such
as "Jump on B, reload on A". Grips, triggers and stick directions keep their roles.

Both maps come from `tools/body_maps.py`, which copies its zone offsets and bindings from the code.
Run it again (`python -E -P tools\body_maps.py`) after changing either.

## Controls in theater mode (menus, Quest Touch → Xbox pad)

| Quest controller | Xbox pad | BF4 default (controller) |
| --- | --- | --- |
| Left stick / click | Left stick / LS | Move / sprint |
| Right stick / click | Right stick / RS | Look / melee |
| Right trigger · left trigger | RT · LT | Fire · aim |
| Right grip · left grip | RB · LB | as bound in BF4 |
| A · B (right) | A · B | Jump · crouch |
| X · Y (left) | X · D-pad down | The game's menu · fire mode (weapons change at the holsters, not by a button) |
| Left menu (≡) | Start | Pause menu (short press). **Hold it** for the bf-vr settings panel: point with either controller, trigger clicks, Save writes `bfvr.ini` |
| Both stick clicks, held ~0.5 s | (none) | Opens or closes the bf-vr settings panel too (SteamVR can take the ≡ button for its dashboard). A single stick click reaches the game 0.1 s late |

D-pad and Back have no button of their own: map one to them on the Controls page. BF4 reads the pad through its own XInput calls, merged with any real pad on slot 1.
Keep the BF4 window focused on the desktop: unfocused, the game drops to 30 fps.

Settings live in `bfvr.ini` beside the game exe (template: `tools/bfvr.ini.example`):
`theater = 0` loads the DLL without VR; `xr_runtime` picks Steam Link / Meta Link / a simulator.
`tools/game.ps1` starts, stops, screenshots and sends input to BF4 (see docs/TOOLING.md).
