# bf4-vr: notes for agents

VR mod for the Battlefield 4 single-player campaign (Quest 3 via Steam Link). Plans and open work
live in **GitHub issues** (DanInDev/bf4-vr); verified facts live in `docs/` and `re/notes/`.
Read those instead of reconstructing history.

## Start here

- `README.md` status and controls; `docs/architecture/` the technical notes: `overview.md` modules, start-up and the frame flow;
  `engine.md` how the mod handles the engine (start there for any engine question);
  `docs/TOOLING.md` all tools.
- Battlefield 3 and multiplayer (Venice Unleashed) live in the sibling repository `..\bf3-vr`; BF4's own
  multiplayer research ended in `re/notes/server.md` (code on the branch `re/local-server`).
- `re/notes/camera.md` camera hook (BF4 build 179547 addresses), `stereo.md`, `gamepad.md`.
- `re/notes/typeinfo.md` engine type registry: `tools/typeinfo_dump.py` names classes, fields and
  vtables; `tools/object_probe.py` / `global_scan.py` name live objects (run the dumper once first).
- `re/MAPPING.md` what is left to reverse-engineer, by area, with method and session order;
  `re/symbols/bf4-179547.md` the registry of verified addresses/offsets (add every new finding there).
- `docs/tasks/` holds self-contained task briefs (e.g. `smooth-stereo.md`: a smoother 3D view);
  start from the brief when the user names one. `all-weapons.md`: the 51 crate guns, their real mechanics and
  the work order for making each one handle like the real gun.
- **Overlord** ([github.com/Aeka0/Overlord](https://github.com/Aeka0/Overlord)) is the quality reference: its
  skeleton, weapon handling and utility interactions (#21). GPLv3: study the design, never copy its code.
  What was learned from it: `docs/architecture/overview.md` (loader/infrastructure split, verified hooks) and
  `docs/tasks/per-eye-rendering.md` ("How Overlord does it": both eyes from one game frame).
- **BF3 VR: The Great Destroyer** ([github.com/criminalvictory/Battlefield-3-VR-The-Great-Destroyer](https://github.com/criminalvictory/Battlefield-3-VR-The-Great-Destroyer)):
  a BF3 campaign VR mod on the same Frostbite generation (OpenXR on the game's D3D11 device, free hands,
  per-joint finger poses in `src/handpose.cpp`, hand reloads incl. bolt and charging handle in `src/reload.cpp`,
  scopes, stereo via `SplitScreenTestViewCount`). **Before starting anything new, check whether it already
  solved it** and note what was learned. No licence file, but its maintainer allows copying its code and setup
  (owner, 2026-11-16; the hope is that the projects merge or serve as the reference for future Frostbite mods):
  reuse is fine, credit it in the code comment and the note (file and function it came from).
- **Reverse engineering.** The decrypted image comes from `pipe dump re\dumps\bf4-179547-mem.exe` (git-ignored,
  never committed). Quick and enough for single functions: `python -E tools\disasm.py <dump> <address> --full`
  (capstone + `.pdata`), `tools\callers.py`, plus the pipe's `watch <address> 4 <ms>` (hardware write watch with
  backtraces) and `peek`. For cross-references and whole subsystems: **REA** (MCP tools `mcp__rea__*`, skill
  `reverse-engineer-anything`) driving Ghidra on the dump; setup in `docs/SETUP.md`. The full analysis takes about 10 min
  and runs again at every `open_binary`, so open once per session (`docs/TOOLING.md`, #20). Same rules as all tools: never while
  Battlefield 6 / EasyAntiCheat games run.
- `docs/SAFETY.md` is binding: campaign only, no tools while Battlefield 6 / EasyAntiCheat games run,
  never commit dumps, screenshots, game files or profile data (sole exception: the owner-chosen README pictures
  in `docs/img/showcase/`); DRM-protected code is analysed in memory only.

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

- One branch per task, from `main`. **Claude is the main worker on this repository** (owner, 2026-11-16): it
  decides when to merge into `main` and push, as it sees fit: the gate green, the monitor check done, and what
  only the headset can judge named in `docs/HEADSET-TESTS.md`. After a merge, delete the branch locally and on
  GitHub, and tell the owner what was merged in the report. Unattended runs keep their own rules
  (`docs/UNATTENDED.md`: no pushes or merges into `main` during a run; the report lists what to merge).
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
- From 2026-11-19 the resume checkpoint is **Shanghai, in front of a weapon cache** (level `SP_Shanghai`): the Baku notes
  below (crate beside you, gadget stash, red room) describe the old one. Crate guns for tests: `givegun` / the own crate
  (#80), e.g. `U_USAS-12_SP` (scope), `U_SteyrAug` (magnifier), `U_SRS` (sniper scope). Untested there yet: say so if
  `givegun` fails in Shanghai.
- Esc skips a cutscene during its loading screen (owner, 2026-11-15); the resume checkpoint starts with the
  Shanghai briefing cutscene, which at the headless ~3 fps otherwise takes minutes. The level no longer spawns
  you with the SCAR and AK: they are in the weapon crate beside you (owner, 2026-11-15).
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
- Unverified (2026-11-12, #49): on a headset-less debug run (level at 4-16 fps, Ghidra busy beside it) the
  hip holster's switch never happened (`holster`: `failed=1..6`, log `holsters: secondary: no switch`).
  Cause not established; do not judge weapon switches from such a run.
- 2026-11-13: `dev-run.ps1 -Resume` stopped at the main menu twice in a row (2160x2160 window, also with
  `-MenuWaitSeconds 90`), though `menu:`/`level:` lines were printed. Clicking by hand works: pipe `pointer`
  at (0.254, 0.2556) Campaign tab, (0.306, 0.402) the BF4 Campaign tile, then `pad 0 0 0 0 200 1000` on Resume.
  2026-11-15 and -16: the cause was the launcher's **Normal** play mode (flat 1920x1080 profile live, mod removed):
  deployed anyway, the mod draws 1920x1080 under a 2160x2160 window and every click misses. `dev-run` now refuses while
  `PROFSAVE_profile.bfvr-mode` says `normal` and ends with `loaded: <level>` (exit 2 when still in the menu).
  Check `safety: level` in `bfvr.log` (not FrontEnd) before trusting a dev-run. One cause seen: a
  "connection with EA Online was lost" dialog over the menu eats the first click (its OK: pointer 0.5 0.583).
- 2026-11-13: weapon switches (holsters, captures, `holster <name>`) only happen while BF4 is the foreground
  window; with it behind other windows the game ignores the keys ("no switch"). Ask before taking focus.
  Right after a level start the knife's weapon name is still empty when slot 7 comes up.
- In pair rendering the first `shot` after a pipe command often shows black arm/gun silhouettes (the other
  eye's draws): judge from the second and third shot, or `eyeshot`.
- 2026-11-13 (#57): on the ~10 fps headless debug run a grip at the sling zone took the slung rifle once
  ("taken from its holster"), then not again in three tries with `zone=primary` reported. A held pad
  grip is `pad 0 0 0 0 <ms> 0200` (returns at once, the mod holds it); without it free hands lets the
  gun go to the sling and the reload zones switch off (`dropped=1`).
- 2026-11-15: pipe `scan` returns at most 64 matches per memory region, so a vtable census misses objects in busy
  regions (found 114 of 259 weapon unlock assets). Walk the regions fully for a census (`re/notes/weapon.md` crate section).
- 2026-11-15: the weapon crate opens with `game.ps1 keys -Keys E` (focus), not `pipe key 69`; in its menu
  keyboard Down scrolls the rows. With BF4 in front, `pipe pad` stick turns stopped turning the soldier and
  `fakeyaw` turns only the camera: face the crate before taking focus (`re/notes/weapon.md` "Fist rack").
- 2026-11-16: `fakehand <yaw> <pitch> ...` turns only the right controller; the left one points straight ahead
  unless its own yaw, pitch, roll follow as arguments 9-11 (`fakehand '0 0 lx ly lz rx ry rz -60 0 0'`). A C4
  "along the off hand" test without them aims along the body.
- 2026-11-16: the gadget stash ignores the game's pad (stick, D-pad, RB) and the arrow keys, also with BF4 in front;
  only mouse clicks work (pipe `pointer`, also behind other windows). Open it with the use reach (off hand ~45 cm
  ahead and low toward it, `pad 0 0 0 0 800 0100`) with the body facing it; E alone did not open it.
- 2026-11-16 (#41): positions in palette space (`extramesh` `at`, copy matrices) are relative to the root bone, which
  sits on a whole-metre grid point, not at the feet: y ~1.8 is chest height there. Compare with `arms` `palette camera`.
  An item's Wep_Root can be far from its mesh (the knife: 0.3-0.6 m, `extramesh` `from_root`).
- 2026-11-16: a headset row added on a branch needs an ID that is free on `main` (`git show main:docs/HEADSET-TESTS.md`):
  a GUN-29 written on a branch overwrote #64's GUN-29 row.
- 2026-11-16: during a run the stop hook reported "Sources changed since the last gate run" after turns where
  `gate.ps1 -Status` matched in every worktree (one of its git calls exits 128). It objects once per stop.
- 2026-11-17: after a `dev-run -Resume` load the mod's arm rig can come up empty (`arms` skeleton=-, `reload`
  points_valid=0, fakehand changes nothing); check `arms` before any hand or zone test and load again if so
  (re/notes/weapon.md "Charging handles, every crate gun"). For `pipe.ps1`, quote a command with arguments as one
  string (`tools\pipe.ps1 'givegun U_M416 1'`): bare extra arguments are refused.
- 2026-11-17: to find what a gun's animation moves, compare every rig bone's local rotation, not positions: the REX's
  frame turns in place (Wep_Slide, 85 degrees, zero travel) and was first reported "not in the rig".
- 2026-11-17: pipe `reload setammo 0` only starts the game's empty reload on a freshly given gun (`givegun` first),
  and only with BF4 in front (`tools\game.ps1 focus`, ask first). `handle_probe` gives into the slot in hand; giving
  into another slot recorded the gun in hand for every gun.
- 2026-11-19 (driving): `[0x14230F260]` is the **controlled entity**, a `ClientVehicleEntity` while driving: read the
  soldier through `common/controlled` `game::local_soldier()`, never the raw global. Pad X is Start under `x_menu`
  (a test pressing X pauses); the pause menu ignores `pipe pointer` clicks, pad A on the highlighted RESUME closes it.
  The campaign menu's objective text can lag (it showed "Help Dunn" but resumed in the car). `re/notes/vehicle.md`.
- 2026-11-19 (sights probe, #137): `givegun` gives a gun **without attachments** unless you name them
  (`givegun U_CZ805 0 U_CZ805_EOTech U_CZ805_Magnifier`; `unlockfind <text>` lists them, not every optic is loaded in
  every level). For fake-hand sight tests set `free_hands 0` and `use_reach 0` live: else the gun drops to the sling
  when a grip hold ends (a re-give does not bring it back, `holster` says `dropped=1`) and the off hand ahead and low
  presses "use". `pad` holds at most 10 s. `zoommesh 1` forces the **hip** model, `0` the zoom model. Pipe `scan` is a
  full heap walk: 91 s, and the pipe answers nothing meanwhile; never put it in a polling loop.
- Windows PowerShell 5.1: `[string](Get-Content <empty file> -Raw)` stays `$null`; use `"$(...)"` before `.Trim()`.
- Repo Python tools run as `python -E -P tools\<tool>.py`; disassembly of the in-memory image:
  `python -E tools\disasm.py <dump.exe> <address> --full` (the dump stays outside git).
