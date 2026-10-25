# bf4-vr: notes for agents

VR mod for the Battlefield 4 single-player campaign (Quest 3 via Steam Link). Plans and open work
live in **GitHub issues** (DanInDev/bf4-vr); verified facts live in `docs/` and `re/notes/`.
Read those instead of reconstructing history.

## Start here

- `README.md` status and controls; `docs/ARCHITECTURE.md` modules and stages; `docs/TOOLING.md` all tools.
- `re/notes/camera.md` camera hook (BF4 build 179547 addresses), `stereo.md`, `gamepad.md`.
- `re/notes/typeinfo.md` engine type registry: `tools/typeinfo_dump.py` names classes, fields and
  vtables; `tools/object_probe.py` / `global_scan.py` name live objects (run the dumper once first).
- `re/MAPPING.md` what is left to reverse-engineer, by area, with method and session order;
  `re/symbols/bf4-179547.md` the registry of verified addresses/offsets (add every new finding there).
- `docs/tasks/` holds self-contained task briefs (e.g. `smooth-stereo.md`: a smoother 3D view);
  start from the brief when the user names one.
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

## Conventions

- C++20, MSVC, tabs, `bfvr::` namespaces, comments only where the code cannot say it.
- Every engine address must be verified (bytes/prologue check) before hooking; record evidence in `re/notes/`.
- Build both presets (`bf4`, `bf3`) warning-free.
