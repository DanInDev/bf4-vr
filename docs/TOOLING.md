# Test tooling

How runs are driven without a person in the headset, and what still needs one.

## Headset connection and runtime choice

`xr_runtime` in `bfvr.ini` (see `tools/bfvr.ini.example`) selects the OpenXR runtime
for the game process only, through `XR_RUNTIME_JSON`; the Windows registration is
never touched.

| Connection | Setting | Verified |
| --- | --- | --- |
| **Steam Link** (Quest 3 over Wi-Fi via SteamVR, `vrlink` driver) | `auto` (SteamVR running) or `steamvr` | Inside BF4 2026-10-22: session reached `VISIBLE`, SteamVR lists the app as `system.generated.openxr.bf-vr.bf4.exe` |
| Meta Horizon Link (cable / Air Link) | `auto` (SteamVR not running) or `system` | Session created in smoke test |
| Simulator | path to the simulator's runtime manifest | not yet installed |

## Driving the game: `tools/game.ps1`

| Command | Effect |
| --- | --- |
| `start` / `stop` | Launch BF4 through the EA app's link (`origin2://game/launch/?offerIds=<content IDs>`, as `bf4.exe` itself does) / close it, forced after 20 s, then wait 10 s so the EA app registers the exit |
| `status` | Process, window and the last `bfvr.log` lines |
| `shot` | PNG of the game window into `re/screenshots/` (git-ignored); works when covered |
| `keys -Keys "Esc,Down,Enter"` | Scancode key presses (DirectInput-compatible); takes focus |
| `click -X 640 -Y 360` | Left click at window-relative pixels; takes focus |

Input commands steal keyboard focus, so do not type elsewhere while a scripted run is going.

EA app launch pitfalls (from `%LOCALAPPDATA%\Electronic Arts\EA Desktop\Logs\EADesktop.log`):
- A relaunch right after BF4 exits is refused with `Game launch error: [4] (AlreadyRunning)`;
  `start` detects that line and fails fast. If it persists, the EA app needs a restart.
- The Play button's offer key `OFB-EAST:109548960` is rejected over the link (`EntitlementNotFound`);
  the content-ID list works.
- `dev-run.ps1` stops BF4 before deploying, so it also closes a game started from the EA app; use
  `-UseRunning` to drive a game that is already at the main menu.
Screenshots taken while BF4 is resizing its window (first ~20 s) can come out black.

## SteamVR behaviour to know

Quitting SteamVR ("quit all" from its monitor window) sends a Quit event to every
VR app **and to Steam itself**; BF4 is closed with it. Theater mode therefore
treats the OpenXR `EXITING` state as final: VR stays off until the game restarts,
instead of reconnecting (which relaunched SteamVR or kept BF4 registered during shutdown).

## One-shot dev loop: `tools/dev-run.ps1`

`dev-run.ps1 [-Resume] [-NoBuild] [-UseRunning]` builds, deploys, starts BF4 and (with `-Resume`) clicks
Campaign → BF4 Campaign → Resume, then waits for the level and saves `re/screenshots/dev-run-level.png`.
Menu clicks are scaled from 1920×1080 to the current back buffer. Total ≈ 3.5 minutes.

Startup settings: `E:\EA\Battlefield 4\user.cfg` runs console commands at launch (verified:
`GameTime.MaxVariableFps 77` capped the game at 77 fps). Command-line `-Setting value` arguments are ignored.

An offline or cracked executable is not used: it would mean bypassing the DRM. The EA app's own
offline mode is the legitimate way to play without a connection.

## In-game control pipe: `tools/pipe.ps1`

| Command | Effect |
| --- | --- |
| `status` | fps, back buffer, theater phase and OpenXR session state, worst `xrSyncActions` / frame-submit time per second, pad state |
| `shot [path]` | PNG of the next frame straight from the back buffer; no focus needed, works with the window covered |
| `dump <path>` | The game's decrypted in-memory image as a PE file (for Ghidra/REA). Local analysis only, into git-ignored `re/dumps/` |
| `peek <hex address> <length>` | Raw memory read, fails cleanly on bad addresses; used by `tools/camera_probe.py` |
| `peekmany <length> <hex address>...` | Batched reads (≤ 4096 bytes each), one line per address, `-` if unreadable; each pipe connection costs ~20 ms, so scans use this (`camera_probe.peek_many`) |
| `poke <hex address> <hex bytes>` | Research write of ≤ 64 bytes, only into pages that are already writable (never code) |
| `tshift <slot> <hex offset> <m>` | Research: every copy, shift the transform at a view-params offset along its right row (0 m = off) |
| `pin <slot> <hex offset> <float>` | Research: every copy, write a float at a view-params offset and mark view 0 dirty (offset 0 = off) |
| `fakeyaw <deg>` | Research: synthetic head yaw applied like a headset eye pose (world + first-person camera); 0 = off |
| `pad <lx> <ly> <rx> <ry> [ms]` | Holds the virtual pad's sticks (−1..1) for up to 10 s; needs XInput polling (a physical pad) and no running headset session |
| `watch <hex address> [len] [ms]` | Hardware write-watch (debug registers + exception handler): every instruction that writes the address, with hit count and a 6-frame unwound backtrace |
| `eye <m>` / `eyealt <m>` | Research: shift the camera sideways / alternate ±m per frame (pass negatives with a leading space: `eye ' -1'`) |
| `fov <rad>` | Research: override the vertical FOV (`RenderView+0xB4`; 0 = off) |

Static helpers (local analysis of the dump, `python -E -P`, needs `pip install --user capstone`):
`tools/disasm.py <dump> <address> [--full]` disassembles the containing function (bounds from
`.pdata`, leaf gaps handled); `tools/callers.py <dump> <address>` lists direct call/jmp sites;
`tools/camera_probe.py` and `tools/projection_probe.py` read live camera and projection data.

Type registry ([re/notes/typeinfo.md](../re/notes/typeinfo.md)):

| Tool | Use |
| --- | --- |
| `tools/typeinfo_dump.py <dump>` | Writes `re/dumps/typeinfo-<pe timestamp>.json`: every reflected type, field, enum value and vtable → class (under 1 s) |
| `… --class <Name>` / `--find <text>` / `--vtable <addr>` | One layout with inherited fields / name search / class of a vtable |
| `tools/object_probe.py <addr> [--deref] [--depth 1]` | Live: class of an object and of every typed object it points to |
| `tools/global_scan.py [--filter Soldier]` | Live: typed objects reachable from `.data` globals, one level through untyped managers; saves `re/dumps/globals-live.json` |
| `tools/matrix_search.py [--save f] [--compare a b]` | Live: every copy of the camera rotation (rows or transposed) reachable from renderer, camera manager and soldier; compare two runs (e.g. with a `view`/`tshift` change) |
| `tools/aim_probe.py [--axis yaw\|pitch] [--static]` | Live: turns with the `pad` stick and lists floats (soldier/camera manager, 2 hops deep) whose change equals the camera's yaw or pitch change |

REA/Ghidra on the 47 MB dump needs more than REA's default 5.5-minute analysis deadline. The user
variable `REA_GHIDRA_STARTUP_TIMEOUT_MS = 10800000` (3 h) is set and takes effect after Claude restarts.

Per-eye research (2026-10-25, research builds): `eyepass <flags> [shift m]` (1 render twice, 2 shifted
eye last, 4 restore the slot snapshot), `eyeshot <path.png>` (first eye's image), `frametrace` now also
records constant-buffer bindings and uploads that contain the camera position (`cam@offset`). Fatal
exceptions are logged to `bfvr.log` with registers and stack (`src/control/crash_log.cpp`).

| Test tool | Use |
| --- | --- |
| `ctest --preset bf4-debug` | Offline tests (`arm_ik`, `eye_math`); see `docs/tasks/testing.md` |
| `tools/soak.ps1 [-Minutes 10] [-MinFps 60] [-Command 'eyepass 1']` | Samples `status` once a second while the game sits in a level; fails on a stall, low fps or rejected eye pairs and saves a `threads` sample (`re/dumps/soak-*`) |

Disable with `control_pipe = 0` in `bfvr.ini`. Local clients only (`PIPE_REJECT_REMOTE_CLIENTS`).

## Headset emulation options

| Option | How it is selected | Pros | Cons |
| --- | --- | --- | --- |
| **Meta XR Simulator** (Windows, v205) | `xr_runtime = <simulator json>`, per process | Simulates a Quest with keyboard/mouse in a desktop window; D3D11 supported; no system change | Download behind Meta's developer license page; D3D11 goes through its Vulkan compositor |
| SteamVR **null driver** | SteamVR settings `forcedDriver: null`, `activateMultipleDrivers: true`, `driver_null.enable: true` | Already installed; real SteamVR compositor; mirror window can be captured | Global SteamVR setting, so it **must be switched off** to use Steam Link again; reported as choppy |
| Real headset, not worn | as is | No setup | Sessions stay `SYNCHRONIZED`; the runtime asks for no frames, so nothing is shown or measured |

## Next tooling steps

- In-process control pipe (`\\.\pipe\bfvr`): status, back-buffer screenshot and later console commands, without stealing focus.
- OpenXR validation layer from the pinned SDK for API-misuse checks in debug runs.
