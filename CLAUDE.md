# bf4-vr: notes for agents

VR mod for the Battlefield 4 single-player campaign (Quest 3 via Steam Link). Plans and open work
live in **GitHub issues** (DanInDev/bf4-vr); verified facts live in `docs/` and `re/notes/`.
Read those instead of reconstructing history.

## Start here

- `README.md` status and controls; `docs/ARCHITECTURE.md` modules, start-up and the frame flow;
  `docs/FROSTBITE.md` how the mod handles the engine (start there for any engine question);
  `docs/TOOLING.md` all tools.
- Battlefield 3 and multiplayer (Venice Unleashed) live in the sibling repository `..\bf3-vr`; BF4's own
  multiplayer research ended in `re/notes/server.md` (code on the branch `re/local-server`).
- `re/notes/camera.md` camera hook (BF4 build 179547 addresses), `stereo.md`, `gamepad.md`.
- `re/notes/typeinfo.md` engine type registry: `tools/typeinfo_dump.py` names classes, fields and
  vtables; `tools/object_probe.py` / `global_scan.py` name live objects (run the dumper once first).
- `re/MAPPING.md` what is left to reverse-engineer, by area, with method and session order;
  `re/symbols/bf4-179547.md` the registry of verified addresses/offsets (add every new finding there).
- `docs/tasks/` holds self-contained task briefs (e.g. `smooth-stereo.md`: a smoother 3D view);
  start from the brief when the user names one.
- **Overlord** ([github.com/Aeka0/Overlord](https://github.com/Aeka0/Overlord)) is the quality reference: its
  skeleton, weapon handling and utility interactions (#21). GPLv3: study the design, never copy its code.
  What was learned from it: `docs/ARCHITECTURE.md` (loader/infrastructure split, verified hooks) and
  `docs/tasks/per-eye-rendering.md` ("How Overlord does it": both eyes from one game frame).
- **Reverse engineering.** The decrypted image comes from `pipe dump re\dumps\bf4-179547-mem.exe` (git-ignored,
  never committed). Quick and enough for single functions: `python -E tools\disasm.py <dump> <address> --full`
  (capstone + `.pdata`), `tools\callers.py`, plus the pipe's `watch <address> 4 <ms>` (hardware write watch with
  backtraces) and `peek`. For cross-references and whole subsystems: **REA** (MCP tools `mcp__rea__*`, skill
  `reverse-engineer-anything`) driving Ghidra on the dump; setup in `docs/SETUP.md`. The full analysis takes about 10 min
  and runs again at every `open_binary`, so open once per session (`docs/TOOLING.md`, #20). Same rules as all tools: never while
  Battlefield 6 / EasyAntiCheat games run.
- `docs/SAFETY.md` is binding: campaign only, no tools while Battlefield 6 / EasyAntiCheat games run,
  never commit dumps, screenshots, game files or profile data; DRM-protected code is analysed in memory only.

## Loop

```powershell
tools\dev-run.ps1 -Resume          # build bf4-debug, deploy dinput8.dll, start BF4, resume campaign
tools\pipe.ps1 status              # fps, XR session, pad, camera, stereo counters
tools\pipe.ps1 shot <path.png>     # back-buffer screenshot (no focus needed)
python -E -P tools\camera_probe.py # live camera chain
```

Config: `E:\EA\Battlefield 4\bfvr.ini` (template `tools/bfvr.ini.example`). Input tools refuse to send
keys unless BF4 is the foreground window. A headset test needs the user; batch those requests.

## Done means verified

- Nothing is "done" without `tools\gate.ps1` green (both presets warning-free, all unit tests, no forbidden
  files) and, for behaviour in the game, the live check with its raw output. State what was not checked.
- Write the check before the change where possible: a unit test, or the pipe command and expected value.
- What only the headset can judge ends at "ready for your check", with a row in `docs/HEADSET-TESTS.md`.
- A dead end, a misleading tool or a new address is written down when it happens (`re/notes/`,
  "Tool pitfalls" below, `re/symbols/`), so the next session does not repeat it.

## Unattended runs

`docs/UNATTENDED.md` is the method; `/work-queue` runs it over the issues labelled `agent-ready`.
Between `tools\agent\run.ps1 start` and `stop` the hooks in `.claude/settings.json` refuse pushes, work on
`main`, closing issues and saving live settings, and a turn cannot end with the gate red. One issue per
branch; green branches are collected on `night/<date>`; three failed attempts park an issue as
`agent-blocked` with what was tried. State lives in issues and files, never only in the conversation.

## Conventions

- C++20, MSVC, tabs, `bfvr::` namespaces, comments only where the code cannot say it.
- Every engine address must be verified (bytes/prologue check) before hooking; record evidence in `re/notes/`.
- Build both presets (`bf4`, `bf3`) warning-free.

## Working with the user

- One branch per task, from `main`. Merge into `main` (fast-forward) only when the user asks, then delete
  the branch locally and on GitHub.
- Every new feature gets a live on/off setting (`src/common/settings_store.cpp`), a row on the in-VR panel
  (`src/ui/settings_panel.cpp`) and a line in `tools/bfvr.ini.example`.
- New engine-free logic gets a unit test under `tests/` (ctest runs them for both presets).
- Each feature that needs the headset gets a row in `docs/HEADSET-TESTS.md`; post monitor results on its issue.
- Report in plain language: what changed, what was verified on the monitor, what still needs the headset.
- The user's main checkout is `C:\Users\dnlni\Desktop\hobby\bf-vr`. After a merge, rebuild there so its
  `bfvr_launcher.exe` and the `dinput8.dll` beside it match `main` (its **Update mod** installs that DLL).

## Testing without a headset

- Set a simulated head first (`pipe fakehead 0.064`), then `fakehand <yaw> <pitch> [left xyz [right xyz]]`
  for controllers; without `fakehead` screenshots show a stale, tilted view. Clear both afterwards (`off`).
- `pipe pad <lx> <ly> <rx> <ry> <ms> [hex buttons] [lt rt]` drives the virtual pad. With `right_stick = turn`
  vertical stick flicks change stance (a look-down once left the soldier prone): switch to
  `setting set right_stick look` to pitch the view, then back.
- The user's own mouse and keyboard reach BF4 when they use the PC: check `GetLastInputInfo` or ask before
  sending input; stop if shots or weapon swaps happen that you did not cause.
- Checkpoint (campaign resume): the gadget stash (C4, M320) is the green crate stack at the start, opened
  with E when facing it; its menu takes `pipe pointer <u> <v> 0|1` clicks. Grenades: throw low along the
  corridor (`grenade throw 0 10 5` lands ~4 m away and wounds); a ceiling bounce has killed the player.
- With physical reload, test firing empties the magazine; `setting set reload button` plus pad B (`2000`)
  reloads, then switch back.
- BF4 only rumbles while its window has keyboard focus (re/notes/haptics.md); the mod reads rumble before
  that gate.
- Live `setting set` changes are not saved unless `setting save`: restore anything you changed.

## Tool pitfalls

- Do not write C++ through shell heredocs or Python string replacement: `\n` and `\0` escapes became real
  line breaks and NUL bytes. Use the file write/edit tools for source files.
- The command permission check sometimes returns no verdict; the same command usually passes on a retry.
- Weapon switches did not happen on a headset-less debug run (2026-11-12, #49; level at 4-16 fps, Ghidra busy
  beside it): `holster` showed `failed=1..6` and the log `holsters: secondary: no switch` (03:32-03:33), and a
  150 ms pad Y did not switch either; snap-turn flicks of 250 ms were also missed, while a held stick walked.
  Cause not established (short inputs at low fps, or no secondary in that checkpoint). Check weapon switches
  in the headset, not with `fakehand` on such a run.
- Repo Python tools run as `python -E -P tools\<tool>.py`; disassembly of the in-memory image:
  `python -E tools\disasm.py <dump.exe> <address> --full` (the dump stays outside git).
