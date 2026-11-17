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

## Launcher: `bfvr_launcher.exe` (issue #16)

Built by the `bf4` preset only (`build\bf4\<Config>\bfvr_launcher.exe`, next to `dinput8.dll` and a copy
of `bfvr.ini.example`). One borderless dark window in the BF4 style (orange accent, Bahnschrift headings,
custom-drawn controls in `src/launcher/theme.cpp`; layout after Overlord's launcher) with three tabs: **Play**
(status and Start), **Setup** (folder, build, install, VR setup) and **Settings** (switches and runtime). It opens
on Play when the mod is installed, else on Setup. Its icon and the Play backdrop are embedded from
`assets/launcher/launcher.ico` and `wallpaper.jpg`; after changing `assets/launcher/source/logo.png` or
`wallpaper.png`, regenerate them with `python -E -P tools\launcher_assets.py` (Pillow), and the README banner
`docs/img/banner.jpg` with `python -E -P tools\readme_banner.py`. `docs/img/launcher.png` is a capture of the
launcher's Play page (our own window, never a game screenshot: those stay out of the repository):

| Part | Behaviour |
| --- | --- |
| Folder | `Install Dir` from `HKLM\SOFTWARE\WOW6432Node\EA Games\Battlefield 4`; **Change...** picks another; `--game-dir <folder>` on the command line overrides it |
| Build | Reads the PE header of `bf4.exe`: x64 and timestamp `0x58AB1CC0` = build 179547. Any other build: install is refused |
| VR setup | **Set up for VR** installs or updates the mod, writes the VR values into `bfvr.ini` (`stereo 1`, `stereo_render pair`, `render_size 2160x2160`, `view_crop_h/v 0.78/0.72`, `hud_layer 1`, `theater`/`gamepad 1`; other keys kept) and sets BF4's own profile (`Documents\Battlefield 4\settings\PROFSAVE_profile`: `MantleEnabled 0`, `WeaponDOF 0`, `MotionBlurEnabled 0`, `MotionBlur 0`, `VSyncEnabled 0`, `GstInput.Vibration 1`). When normal mode was live, its profile is kept first (as the VR mode switch does). Refused while BF4 runs (it writes the profile back on exit). Status lines show what is still to change (a missing `bfvr.ini` key counts as the mod's default) and the VR runtime the mod will pick: SteamVR when `vrserver.exe` runs, else the system's active OpenXR runtime (same rule as `xr/runtime_selection.cpp`) |
| VR mode switch (Play) | Normal and VR play each keep their own BF4 profile (`src/launcher/play_mode.cpp`). Flipping it saves the live `PROFSAVE_profile` as the current mode's copy (`PROFSAVE_profile.bfvr-normal` / `.bfvr-vr`, plain text, editable while BF4 is closed), makes the other mode's copy live, writes `PROFSAVE_profile.bfvr-mode` (`normal`/`vr`) and installs or removes the mod. The first switch to VR sets the VR values above on the normal profile; the first switch to normal takes `PROFSAVE_profile.bfvr-backup` (kept by launchers before this switch) when present, else keeps the profile and saves what you change from then on as normal. Campaign progress (`PROFSAVE`) is never touched. Refused while BF4 runs; to VR also refused on another build, a missing `dinput8.dll` or another mod's DLL, checked before any file changes. Without a marker the mode follows whether the mod is installed. `--profile <file>` points the switch at a test copy |
| Install / Update / Remove | The `dinput8.dll` beside the launcher, with the `bfvr.deployed.json` hash marker of `deploy.ps1` (each tool can update or remove the other's install). Refused while `bf4.exe` runs; a `dinput8.dll` without a matching marker belongs to another mod and is never replaced or deleted |
| Settings | `theater`, `stereo`, `stereo_first_person`, `gamepad`, `xr_runtime` in the game folder's `bfvr.ini`; edited in place (comments and other keys kept), a missing file starts from `bfvr.ini.example`. Warns when a `BFVR_<KEY>` environment variable overrides the file |
| Start Battlefield 4 | The same `origin2://` content-ID link as `game.ps1 start`; asks first when the mod does not match the mode (VR without the mod, normal with it) |
| Open bfvr.log | The mod's log in the game folder |

The launcher calls `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)` first, so the mod DLL
beside it can never load into the launcher itself.
`build\bf4\Debug\bfvr_launcher_test.exe` checks build detection, deploy/remove/foreign-DLL handling,
the `deploy.ps1` marker format, `bfvr.ini` editing and the VR setup (profile pending/apply/backup/restore,
refusal while the game runs, the ini preset with defaults, runtime manifest names) against a throwaway
folder in `%TEMP%`.

Setup page, **Updates?**: asks GitHub for the latest release (only on that click) and offers its page when it is newer than this build's version (CMake `project(... VERSION)`). Not yet: starting SteamVR from the launcher and live headset/session status (#16).

## Release packaging: `tools/package.ps1` (issue #17)

`tools\package.ps1 [-Version 0.1.0] [-NoBuild]` builds `bf4-release` and writes
`build\package\bf-vr-<version>.zip` (the version defaults to the CMake project version, with `-dev.<commit>` unless
HEAD carries the tag `v<version>`): `bfvr_launcher.exe`,
`dinput8.dll`, `bfvr.ini.example`, `README.txt` and `LICENSE.txt` (from `tools/package/`) and
`THIRD-PARTY-NOTICES.txt` (MinHook, OpenXR loader and JsonCpp licences from the fetched sources). The contents
are an allow-list, checked again inside the finished zip: game files, dumps, screenshots, PDBs, `bfvr.ini` and
logs can never end up in it. `LICENSE.txt` is the MIT licence (the same as `LICENSE` at the root). There is no installer
exe yet: the zip plus the launcher's install/remove is the release format.

**Publishing:** bump `project(bfvr VERSION x.y.z)` in `CMakeLists.txt`, then push the tag `vx.y.z`. The `release`
workflow checks the tag against the version, builds `bf4-release`, packages it and creates the GitHub release
with the zip; the launcher's **Updates?** then reports it. `bfvr_launcher_test.exe --online` asks GitHub the way the
button does.

## Driving the game: `tools/game.ps1`

| Command | Effect |
| --- | --- |
| `start` / `stop` | Launch BF4 through the EA app's link (`origin2://game/launch/?offerIds=<content IDs>`, as `bf4.exe` itself does) / close it, forced after 20 s, then wait 10 s so the EA app registers the exit |
| `status` | Process, window and the last `bfvr.log` lines |
| `shot` | PNG of the game window into `re/screenshots/` (git-ignored); works when covered |
| `focus` | Brings BF4 to the front, no keys (the game's empty reload, `handle_probe`, only animates in front); ask first |
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

## What the headset shows: `tools/vrview-shot.ps1`

`pipe shot`, `eyeshot` and the marks hold only the game's image. Everything the mod adds as an OpenXR
layer is missing there: grab rings, the green aim dot, the sight dot, the scope picture, the panel,
notices, the laser beam. SteamVR's mirror window (**VR View**: SteamVR Status window, pop-out icon on its
small headset picture) shows the composited result, and `tools\vrview-shot.ps1 <out.png>` captures it
(`PrintWindow` with full content, so it works behind other windows, not minimised; 1920x1080 for the
popped-out view). Without VR View it falls back to the small view inside the Status window;
`-List` prints the open windows. It is the compositor's mirror, so slightly cropped and warped
against the lenses. First used 2026-11-11 (the laser beam showed; marks never do).

## Recording: the stream view and OBS

VR View is one lens-shaped eye that shakes with every head movement. With `stream_view = 1` (default;
View page > Recording) the **game window** shows a recording picture instead, whenever the headset shows
the level head-tracked: the left eye's image re-aimed at a steadied camera (yaw and pitch follow the head
through a low-pass, `stream_smoothing` seconds to settle at rest, faster turns followed closer; the roll
taken out), 16:9 and `stream_fov` degrees across, the HUD layer's main piece on top (`stream_hud`: crosshair,
markers, objective text; the minimap and ammo, which the headset shows on the body, are left out), letterboxed into
the window so that the monitor-shaped window shows it in proportion. Menus, loading screens and flat-screen
cutscenes stay the game's own picture. The game renders a square image that DXGI stretches into the
monitor-sized window, so the 16:9 picture is drawn squeezed into the back buffer and comes out right on
the monitor. Cost: two full-screen passes inside the game's Present, **0.05 ms GPU** and 0.02-0.04 ms CPU per
frame (timestamp queries, `stream`; 2026-11-15, 1920x1080 picture into a 2160x2160 back buffer, RTX 5070). The
picture has as many pixels as the window shows of it.

The camera follows yaw and pitch only and is level; it cannot leave the rendered image, so with
`view_crop_h` the room to steady is (rendered width - `stream_fov`) / 2: with the owner's crop (0.78) the eye
image is ±47° wide and the default 80° leaves 7° each side.

Not in it: what the mod adds as OpenXR layers (the aim dot, grab rings, the separate sight lens quad,
the panel, notices, the laser). Use VR View for those. The scope picture is in it when `scope_in_frame`
draws it into the eye image (the default).

OBS:
1. Sources > **Window Capture**, window `[bf4.exe]: Battlefield 4`, capture method *Windows 10 (1903 and up)*;
   untick *Capture Cursor*. (*Game Capture* of bf4.exe works too.)
2. On a 16:9 monitor and a 16:9 canvas the picture fills it; on another monitor shape crop the black bars
   (Alt-drag in the preview) or use *Fit to screen*.
3. Sound: **Application Audio Capture** of bf4.exe, as before.
4. Steadiness: raise `stream_smoothing` (0.5) for a calmer picture, lower it (0.15) if it trails turns; `0`
   shows the head's motion as it is (still level and 16:9). A narrower `stream_fov` leaves more room to
   steady: the camera is pulled towards the eye's own view whenever the picture's corners would leave the
   rendered image (fast turns, a far-rolled head, a tight `view_crop_h`), so the bars never show the edge.

Pipe: `stream` (state, picture and window size, the eye's and the stream camera's yaw / pitch / roll,
`kept` = share of the steadying in use, CPU time), `stream trace` (the last 256 frames of those angles),
`streamshot <path.png>` (the 16:9 picture itself; `shot` still takes the back buffer before it is drawn
over, so screenshots and `dev-run` show the eye image as before).

## One-shot dev loop: `tools/dev-run.ps1`

`dev-run.ps1 [-Resume] [-NoBuild] [-UseRunning]` builds, deploys, starts BF4 and (with `-Resume`) clicks (through the mod's pointer, no focus needed; RESUME by pad A)
Campaign → BF4 Campaign → Resume, then waits for the level and saves `re/screenshots/dev-run-level.png`.
Menu clicks are scaled from 1920×1080 to the current back buffer. Total ≈ 3.5 minutes.
It refuses to start (exit 1, nothing stopped or deployed) while the launcher is in its Normal play mode
(`Documents\Battlefield 4\settings\PROFSAVE_profile.bfvr-mode` says `normal`): the flat 1920×1080 profile under the
mod's 2160×2160 window makes every menu click miss (#79). A missing marker file and `-UseRunning` skip the check.
With `-Resume` it ends with `loaded: <level>` from `bfvr.log` and exits 2 when that is not a `Levels/SP/` level
(still in the main menu).

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
| `pad <lx> <ly> <rx> <ry> [ms] [hex buttons] [lt rt]` | Holds the virtual pad's sticks (−1..1), XINPUT buttons and triggers (0..1) for up to 10 s; needs XInput polling and no running headset session (e.g. `pad 0 0 0 0 3000 0 0 1` fires 3 s) |
| `key <hex vk> [ms]` | Research: a key press through the game's Raw Input keyboard (works without focus), e.g. `key 31` = 1 |
| `holster [primary\|secondary\|knife\|gadget1\|gadget2\|order\|slingtest on\|slingtest off]` | Holsters, the squad signal, the use reach (with the off palm's `palm_forward`/`side`/`up` from the head) and the melee gesture (strikes, forward speed): state; research: switch as if drawn from that holster, `order` = the squad order key (Q), `slingtest on` = the primary's sling copy (#58) also while it is in hand (a monitor check without a weapon switch) |
| `grenade` / `grenade throw <yaw> <pitch> <m/s> [safe]` / `grenade capture` | Physical grenades: state, the grenade pouch point (`belt`); research: a throw from the off hand with that velocity (view-relative degrees), `safe` = no explosion; `capture` = select the game's grenade for a moment so the 3D held grenade is captured (#36). Test pitfall: an armed grenade let go falls live at your feet; cancel it with `setting set grenade button` while the grip is still held |
| `panel [open\|close\|hover <u> <v>\|click <u> <v>\|shot <path.bmp>]` | In-VR settings panel (#7): state; open/close it, hover or click at texture coordinates (0,0 top left) without a headset, or write the panel image as a BMP |
| `scan <hex bytes> [max]` | Research: addresses of a byte pattern in committed private read/write memory (heap), e.g. a vtable to find live objects |
| `entityevent <entity> <event id> [slot]` | Research: on the game thread, call the entity's event handler (vtable slot, default 5) with that event id (`re/notes/sound.md`) |
| `motion` | The game camera's own yaw rate and speed (no head pose), the comfort vignette's input (#10) |
| `sightstate` | The current weapon's sight (#39): unlock name, zoom levels, the level the next aim uses and the one in use, its SightType (2 = magnifier), and the attachments read from its unlocks (`optic=U_AK12_PK-AS reflex magnifier magnified (zoom)`) |
| `unlocks` | `unlock_all_weapons`: the setting, the code check, the active `SyncedBFSettings`, its `AllUnlocksUnlocked`, whether the 1 is the mod's, writes and restores |
| `hipaccuracy` | `hip_accuracy` (#39): strength, patched GunSwayData blocks, each one's standing hip min/max as written, the game's and the zoom values |
| `pickup` | `weapon_pickup` (#54): weapons on the ground found (census, usable), how long the census scan took, each palm's nearest pickup (m), R down, presses, takes started and done, the last event |
| `serversoldier` | The player's server soldier (`weapon/server_soldier`): its address, how it was found (kept, static table, heap scan), changes, misses and scans. Physical reload and grenades read ammo through it |
| `zoommesh [0..1\|off]` | `sight_zoom_mesh` (#39): whether a sight is at an eye, the weapon states whose `ZoomMeshTransitionFactor` is held at 0 and the game's values; research: hold the factor at a value whatever the eye does (0 = the zoom mesh and the game's reticle at the hip) |
| `reticle [<tan x> <tan y>\|off\|dump <file>]` | `sight_reticle` (#39): the sight line in the eye's view space (`tan`), reticle quads seen and re-anchored; research: two numbers hold the anchor at that offset (a negative first number with a leading space), `dump` writes every stencil-tested draw of one first-eye render with its depth-stencil, blend, scissor and viewport state |
| `twohand` | Two-handed weapon (#29): held, blend weight, grab count, barrel and converged shot directions (OpenXR LOCAL), aim mode |
| `extramesh` / `extramesh pin <hex mask>` | Extra meshes (#36): whether a second magazine is drawn now, its move, copies drawn, palettes read, meshes carrying the magazine tag; the held grenade: copy on/armed/where, captures (accepted, rejected), the captured draws (pass, owned blocks and views), the hold used and the game's own hold last seen (`re/notes/meshes.md`). `pin`: which palette entries of the grenade vanish once the pin is out (research; default 1 = entry 0) |
| `depthprobe` | The dot on target: dispatches and readbacks of the scene-depth reads, the last hit distance and its age, the depth texture's description and the world projection (`re/notes/weapon.md`) |
| `meshprobe <file.txt>` | Research: every indexed draw of the next first-eye render (buffers, shaders, target, VS constant bindings) and every 3072-byte VS slot-0 block as 64 3x4 matrices |
| `eyehide <ps hex>... \| plain \| null \| list \| off` | Research: skip draws in both eye renders by pixel shader (up to 64 pointers from `meshprobe`), every non-indexed draw (`plain`), every draw without a pixel shader (`null`); `list` gives the non-indexed draws of the last first-eye render (shader, vertices, count). Found the AK-12 lens and the game's reticle to be multi-pass effects (re/notes/weapon.md) |
| `meshcopy <index buffer hex> <dx dy dz> [entries] \| off` | Research: after each draw from that index buffer, the same draw again with palette entries 0..entries-1 moved (palette space, metres) |
| `reload [fire\|button\|catch\|grab\|take\|letgo\|stow\|insert\|rack]` | Physical reload (#14): state of the gun in hand, magwell/slide positions, the reloading hand (`hand=`, the right while the left carries the gun) and its palm, the held magazine's top and recent events; with an event (research) drives the state machine directly |
| `watch <hex address> [len] [ms] [rw]` | Hardware write-watch (debug registers + exception handler): every instruction that writes the address, with hit count and a 6-frame unwound backtrace; `rw` traps reads too ("what accesses this", e.g. the per-frame reader of a widget's `Visible`) |
| `eye <m>` / `eyealt <m>` | Research: shift the camera sideways / alternate ±m per frame (pass negatives with a leading space: `eye ' -1'`) |
| `fov <rad>` | Research: override the vertical FOV (`RenderView+0xB4`; 0 = off) |
| `stream [trace]` / `streamshot <path.png>` | Stream view for recording (section "Recording"): state, sizes, eye and stream camera angles, CPU ms; `trace` the last 256 frames; `streamshot` the 16:9 picture |
| `scope` / `scopeshot <path.png>` | Magnified sights: the game's FOV (hip and aiming), magnification, whether the scope render runs, its FOV, lens quad and which eye; the eye pass's scope timing is in `status` (`scope_ms`, prepare/render/compose). `scopeshot` writes the lens picture (512x512, round, premultiplied) |
| `lean` | #53: the head's offset from the session origin (OpenXR LOCAL, in the body's axes: forward, right, up) and how far the rendered view sits from the soldier's own camera, from the last head-tracked frame; notes when the head is over 0.5 m from the origin |
| `raycast <x y z> <x y z>\|down [m]\|ahead [m]\|stats` | #59: the game's own physics ray (level only) between two world points, or from the head straight down / along the view, cast on the game thread; hit point, distance, normal, microseconds. `stats`: rays cast, hits, faults, mean time, and the ground cache's columns (`re/notes/physics.md`) |
| `poses [save]` | Pose editor (panel page Skeleton): the grip each hand shows now with its eased tweak, the held preview, and every grip's live tweak in the `bfvr_poses.ini` format; `save` writes that file beside `bfvr.ini` (to read it back use `setting set pose_file load`, so the sliders follow) |
| `wallstop` | #63: the drawn gun stopping at walls: whether the barrel ray answered, how far the real barrel is inside the wall, the push back along it, let_go (more than 30 cm), frames pushed and skipped (with why), and the ray's world ends |
| `eyezoom <deg>` | Research: the second eye keeps the first eye's camera but renders with this vertical FOV (0 = off); `eyeshot2` to look at it. Showed that the constant patch follows a zoom (lighting correct) |
| `mark [note]` | Marks this moment for headset feedback, like a short tap of both stick clicks in the headset (`mark_tap`): `status`, `twohand`, `arms`, `scope`, `grenade`, `fingers`, `reload`, `cutscene`, `stance`, `haptics`, `hudwidgets` and `setting list`, plus a back-buffer screenshot, written to `bfvr_marks\mark-<date>-<time>-<n>.txt/.png` beside `bf4.exe` (never commit those). `bfvr.log` gets a `mark n` line |
| `hudwidgets [classes \| instances <vtable> \| hide <vtable>]` | HUD element toggles (#28): hook state, hidden-element mask, `isVisible` calls and hidden answers since the last report (about one hidden answer per frame per class of a hidden element). `classes`: every widget vtable that asked in one second; `instances <vtable>`: those widgets with their data (poke data `+0x60` to hide one); `hide <vtable>`: hide one more class (0 = none) |
| `foveation` | Fixed foveated rendering: level, radii, share of pixels at each rate, whether NVAPI variable rate shading is available, targets shaded / left alone (set the level with `setting set foveation off\|light\|medium\|strong`) |
| `scopescissor [0\|1]` | Research: the scope render's scissor to the lens circle off/on (default on) and its counters. `status` carries `gpu_ms eye1= eye2= scope=` (GPU timestamps around each render, pair rendering) |

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
| `tools/skeleton_probe.py [--match Hand]` (run from `tools\`) | Live: the first-person skeleton's bones (index, parent, name); with `--match`, the live local pose of the matching bones (fingers: re/notes/skeleton.md) |
| `tools/aim_probe.py [--axis yaw\|pitch] [--static]` | Live: turns with the `pad` stick and lists floats (soldier/camera manager, 2 hops deep) whose change equals the camera's yaw or pitch change |

REA/Ghidra on the 47 MB dump needs more than REA's default 5.5-minute analysis deadline, so the user
variable `REA_GHIDRA_STARTUP_TIMEOUT_MS = 10800000` (3 h) is set. Measured 2026-11-04 (REA 6.7,
Ghidra 12.1.4, #20): the full auto-analysis takes **about 10 minutes** and finds 142,493 functions
and 52,460 strings. REA keeps the Ghidra database only while the target is open, so every
`open_binary` runs the analysis again. The snapshot `close_binary` writes
(`re/dumps/bf4-179547-mem.rea-snapshot`, about 35 KB, git-ignored) holds REA's evidence records, not the
database. Workflow: `open_binary` on `re\dumps\bf4-179547-mem.exe` with `provider_id ghidra`. The first
query waits for the analysis; after that, `procedure_callers`, `xrefs`, `procedure_pseudo_code` and
`search_strings` answer in seconds. Keep the target open for the whole session and `close_binary` at
the end. A dump from another game session needs a new analysis (the image has to be the same bytes).
Names come from the in-memory typeinfo (`tools/typeinfo_dump.py` vtables), not from Ghidra, which has
no symbols for this image.

Per-eye research (2026-10-25, research builds): `eyepass <flags> [shift m]` (1 render twice, 2 shifted
eye last, 4 restore the slot snapshot), `eyeshot <path.png>` (first eye's image), `frametrace` now also
records constant-buffer bindings and uploads that contain the camera position (`cam@offset`). `eyeprobe <prefix>` writes one
first-eye render's constant bindings, every bound buffer (GPU readback) and view slot 0 to `re/dumps`;
`fakehead <ipd> [yaw] [height] [pitch]|off` publishes a fixed head sample so stereo paths run without a
headset (pitch in degrees, up positive: look down at the floor or the chest rig);
`fakefocus lost|back|off` simulates a system menu taking the controllers (#72: the pad goes neutral, held inputs stay
closed after `back` until released; publish a neutral `pad` while `lost`, as the XR loop does); always end with `off`;
`stereorender aer|pair` switches between alternate-eye and both-eyes-per-frame rendering. In pair
rendering `shot` is not the left eye's headset image (the second eye's gun ends up in it): judge per-eye
geometry with `eyeshot` / `eyeshot2`. `bodyturn 0|1` A/B-tests turning the first-person body back from the
gun (re/notes/skeleton.md). `twohand` prints each eye's distance from the sight line
(`eyes off_line_cm along_cm`, what `sight_ads` needs) and which eye sees the sight dot (`dot_seen=L/R`;
`status` counts the frames that showed it, `sight_dot_frames`, headset only), `holster` the free hands state (`dropped`, `floor_y`). Timing work on
the eye pass needs the Release build (`dev-run.ps1 -Preset bf4-release`, `research = 1` in `bfvr.ini`). Fatal
exceptions are logged to `bfvr.log` with registers and stack (`src/control/crash_log.cpp`).

| Test tool | Use |
| --- | --- |
| `tools/settings.ps1 [-List]` | Live settings (#25): up/down pick, left/right change, `r` reset, `s` save into `bfvr.ini`, `q` quit; pipe `setting list|get|set|step|reset|save` underneath |
| `ctest --preset bf4-debug` | Offline tests (`arm_ik`, `eye_math`, `settings`, `right_stick`, `gestures`, `handedness`, `reload`, `launcher`); see `docs/tasks/testing.md` |
| `tools/body_maps.py` | Offline: writes `docs/img/grab-zones.svg` and `docs/img/quest3-controls.svg` (README, release zip). Zone offsets and bindings are copied from `weapon/holsters.hpp`, `weapon/body_rig.hpp` and the input code; run again after changing them |
| `tools/heap.py <hex qword>` | Live, read-only: every address in bf4's private heap holding that value (a full walk; pipe `scan` stops at 64 per region) |
| `tools/unlock_census.py [--json f]` | Live: every `SoldierWeaponUnlockAsset` in memory (name, ids, flags) |
| `tools/weapon_catalog_probe.py [--held] [--json f]` | Live: the 51 crate guns' magazine size and spares; `--held`: the spawned guns' firing data (reload type, fire modes, rof, reload and bolt times, zoom levels); `docs/tasks/all-weapons.md` |
| `tools/catalog_sweep.py [--json f] [--only U_A,U_B] [--restore U_X]` | Live, own-crate build (#80): gives each of the 51 crate guns into slot 0 (`givegun`), reads its firing data and the reload kind the mod picked (waits up to 10 s for its `reload:` line); `--restore` gives a gun back at the end. Changes the player's primary gun. Result tables: `docs/weapons.md` (#81) |
| `tools/handle_probe.py record [--guns U_A,U_B] [--event empty\|shot] [--restore U_X]`, `analyse [--json f] [--table]`, `timeline --file U_A.empty` | Live, research build, BF4 in front: records into the slot in hand (the old gun given back; repeated takes kept beside each other and pooled) each gun's empty reload (own crate give, reload = button, arm_ik = 0, `reload setammo 0`) or one shot (bolt actions) frame by frame to `re/dumps/gun_points/` (Wep_Slide, Wep_Mag, both hands, Wep_Root axes); restores the settings. `analyse` gives the charging handle (hand moving with the bolt) and the magazine grab point and way out, offline. Results in `profiles.cpp` (`handle_point_for`, `magazine_grab_for`; re/notes/weapon.md "Charging handles, every crate gun", "Magazine grab points") |
| `tools/slot_probe.py [--press <hex>]` | Live: weapon slots and names, the current slot; times a pad press until the slot changes |
| `tools/sound_probe.py <reference\|buttons\|physical\|physical_ar\|grips\|stages>` | Live: records the playback device (`bfvr_audio_loopback`) while the pipe drives one step at a time; per step the sound onset and rise, clips in `re/dumps/sound/` (`re/notes/sound.md`) |
| `tools/reload_sound_probe.py [--event <name> --name <part>]` | Live: the reload SoundEntity of every gun (pipe `scan`), and sends one entity event to one (pipe `entityevent`) |
| `tools/weapon_sound_probe.py [--slot n]` | Live: the gun's sound asset, its sound patch graph events and parameters with hash-matched names |
| `tools/sight_probe.py [--ads s]` | Live: the camera joint in Wep_Root axes (the sight's eye point and line), optionally after holding the aim button; run with `armik 0`, `aim head`, `gun_pose animated` for the game's own pose (`re/notes/weapon.md` "Sights") |
| `tools/recoil_probe.py [--watch s]` | Live: every gun's GunSwayData recoil per stance and zoom, first-shot multiplier, fire logic, the stance; `--watch` samples the aiming sway/recoil offset (`re/notes/weapon.md` "Recoil data") |
| `tools/fire_effect_probe.py` | Live, read only: each gun's muzzle flash data (`FireEffects1p` / `3p`: offset, rotation, zoom offset, flags) and its `ShotConfigData` spawn flags (`re/notes/weapon.md` "The muzzle flash") |
| `tools/grenade_probe.py [--slot 6]` | Live: the grenade weapon's launch data (shot, hold-and-release, projectile) and its count (`re/notes/weapon.md` "Grenades") |
| `tools/ammo_probe.py` | Live: fires and reloads with the pad and lists int fields that follow the magazine and reserve (`re/notes/weapon.md`) |
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
