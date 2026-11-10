# Environment setup

Besides the Quest 3, you need a PC-VR connection, a C++ toolchain (x64 for BF4; x86 only
for the `bf3` preset), and a reverse-engineering kit. Run `tools/check-env.ps1` at any time to see the
current state.

## 1. Already on this PC

| Need | Status |
| --- | --- |
| Git, GitHub CLI | ✅ |
| VS Build Tools 2022 with MSVC 14.44 (**x86 + x64 compilers**) | ✅ |
| CMake 3.31 + Ninja 1.12 (bundled with Build Tools, **on user PATH** since 2026-10-22) | ✅ |
| Windows SDK 10.0.26100 | ✅ |
| Node.js 24.11.1 (REA needs ≥ 24.11) | ✅ |
| Python 3.14 | ✅ |
| Meta Horizon Link with **32-bit** OpenXR runtime registered | ✅ |
| SteamVR, EA app, Battlefield 4 (and Battlefield 3) | ✅ |
| GPU/CPU (RTX 5070, 9800X3D) — ample for a 2013 game at Quest 3 resolution | ✅ |

## 2. Reverse-engineering kit (installed 2026-10-22)

| Tool | Where | Why |
| --- | --- | --- |
| Temurin **JDK 21** | `C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot`; user `JAVA_HOME` points here | Ghidra 12.1.x and REA |
| **Ghidra 12.1.4** | `C:\Tools\ghidra_12.1.4_PUBLIC`; user `GHIDRA_INSTALL_DIR` | REA 6.7 is validated against 12.1.x. Do not take 12.2 until REA supports it |
| **x64dbg / x32dbg** | winget package; `x32dbg` command alias | Debugger for the running game (x64 for BF4, x32 for BF3); the pipe's `watch`/`peek` cover most needs |
| **RenderDoc 1.46** | `C:\Program Files\RenderDoc` | D3D11 frame captures |
| **REA 6.7.0** | `%APPDATA%\npm`; registered for Claude Code in `~/.claude.json` | Lets Claude Code drive Ghidra over MCP. `rea doctor`'s Hopper/IDA/JADX/Kuna and other-client warnings are expected and harmless |

| **FrostyEditor 1.0.6.3** | `C:\Tools\FrostyEditor` (official GitHub release, unsigned; zip SHA-256 `6ECA779A…9106F8`) | Browse BF4 assets, EBX level/camera/HUD data; `Profiles\BF4SDK.dll` carries BF4 type layouts |

Frosty notes: it reads the game's data files and builds a cache on first open;
point it at `E:\EA\Battlefield 4\bf4.exe`. Use it read-only for now. Applying
mods needs FrostyModManager and a separate launch profile, never multiplayer.

Optional extras: Visual Studio 2022 Community (debugger UI for your own DLL) and
System Informer (modules/threads of the game).

### REA and the Claude desktop app
The Claude desktop app is an MSIX package. A global `npm install` started from
inside it lands in the app's private storage, which other programs cannot see.
Install REA from a Windows Terminal that you open yourself:

```powershell
npm install --global rea-agents@6.7.0
rea setup --client claude_code --dry-run   # review
rea setup --client claude_code --yes       # apply: registers the MCP server in ~/.claude.json
rea doctor
```

Then restart Claude.

Added later by CMake, with nothing to install by hand: the OpenXR SDK loader and MinHook (function hooking).

## 3. Quest 3 connection

BF4 is 64-bit; `xr_runtime` in `bfvr.ini` picks the runtime for the game process only
(docs/TOOLING.md). A 32-bit game (BF3) needs a 32-bit runtime: SteamVR has one, Meta's crashes
(docs/FINDINGS.md).

| Option | Status | Recommendation |
| --- | --- | --- |
| **Steam Link** (Quest 3 over Wi-Fi through SteamVR) | ✅ used for all headset tests | **Default.** `xr_runtime = auto` picks SteamVR when it runs |
| Meta Horizon Link (USB-C cable or Air Link) | ✅ session created in the smoke test (x64) | Fallback: `auto` without SteamVR, or `system` |
| Virtual Desktop (VDXR) | not installed | Optional later |

Air Link needs a Wi-Fi 6/6E router near the play area, with the PC on Ethernet.

## 4. Before each session

1. Close Battlefield 6 and confirm `EAAntiCheatService` is stopped.
2. Connect the Quest through Steam Link (SteamVR running).
3. Start the BF4 campaign, **single-player only** (`tools\dev-run.ps1 -Resume` or the launcher).
