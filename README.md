# bf-vr

Hobby research project: VR support for the **Battlefield 4 single-player campaign**
(Battlefield 3 remains a supported build target) on a Meta Quest 3, in the spirit
of [Overlord](https://github.com/Aeka0/Overlord) (MW2CR) and the Titanfall 2 VR mod.

Status: **stage 1 (theater mode) works in BF4 on a Quest 3 via Steam Link** (confirmed in
the headset 2026-10-22). VR controllers act as an Xbox pad (built, awaiting an in-headset test).
A control pipe gives status and back-buffer screenshots without touching the game window.

| Doc | What it covers |
| --- | --- |
| [docs/SETUP.md](docs/SETUP.md) | What is installed, Quest 3 connection |
| [docs/SAFETY.md](docs/SAFETY.md) | Rules that keep your EA account and other games safe — read first |
| [docs/FINDINGS.md](docs/FINDINGS.md) | Verified facts about `bf4.exe`, `bf3.exe` and this PC |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Modules, stages, BF4 vs BF3 |
| [docs/TOOLING.md](docs/TOOLING.md) | Runtime selection (Steam Link / Meta Link / simulator), driving BF4, emulation options |
| [re/README.md](re/README.md) | Reverse-engineering workspace and REA workflow |
| [re/MAPPING.md](re/MAPPING.md) | Engine elements still to map, with method and session order |
| [re/symbols/bf4-179547.md](re/symbols/bf4-179547.md) | Verified addresses and offsets for BF4 build 179547 |
| [re/notes/stereo.md](re/notes/stereo.md) | BF4's built-in stereo settings and the next experiment |

## Layout

```
bf-vr/
├─ docs/            design notes, findings, setup
├─ re/              reverse-engineering workspace (dumps/projects are git-ignored)
├─ src/
│  ├─ common/       logging
│  ├─ loader/       dinput8.dll proxy — the game loads this, it starts core
│  ├─ control/      \\.\pipe\bfvr: status, back-buffer screenshots
│  ├─ core/         runtime start-up on its own thread
│  ├─ hooks/        Present and XInput detours (MinHook)
│  ├─ input/        shared gamepad state
│  └─ xr/           theater mode, controller actions, runtime selection
├─ tests/           smoke_theater: hook + OpenXR without the game
├─ tools/           check-env.ps1, deploy.ps1, game.ps1, pipe.ps1, console-probe.ps1, bfvr.ini.example
└─ CMakeLists.txt   presets bf4 (x64, default) and bf3 (Win32); fetches OpenXR SDK and MinHook
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

## Controls in theater mode (Quest Touch → Xbox pad)

| Quest controller | Xbox pad | BF4 default (controller) |
| --- | --- | --- |
| Left stick / click | Left stick / LS | Move / sprint |
| Right stick / click | Right stick / RS | Look / melee |
| Right trigger · left trigger | RT · LT | Fire · aim |
| Right grip · left grip | RB · LB | as bound in BF4 |
| A · B (right) | A · B | Jump · crouch |
| X · Y (left) | X · Y | Reload · switch weapon |
| Left menu (≡) | Start | Pause menu |

No D-pad or Back yet. BF4 reads the pad through its own XInput calls, merged with any real pad on slot 1.
Keep the BF4 window focused on the desktop: unfocused, the game drops to 30 fps.

Settings live in `bfvr.ini` beside the game exe (template: `tools/bfvr.ini.example`):
`theater = 0` loads the DLL without VR; `xr_runtime` picks Steam Link / Meta Link / a simulator.
`tools/game.ps1` starts, stops, screenshots and sends input to BF4 (see docs/TOOLING.md).
