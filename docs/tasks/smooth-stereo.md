# Task brief: a smoother, more natural 3D view

Written 2026-10-25 for a fresh agent session. Read `CLAUDE.md`, `docs/SAFETY.md` (binding) and this
file first; everything else is linked from here.

## Goal

The user's verdict on the current headset view: it "looks weird and janky". Make the stereo view
feel smooth and solid in normal play (looking around, walking, turning with the stick, firefights)
on a Quest 3 via Steam Link. Related issues: #6 (alternate-eye pacing and judder, the core of this),
#1 (sharpness and FOV), #8 (render scale).

Start by asking the user to describe the jank in one headset session, using the checklist under
"First session" below, because several different causes look alike.

## How the 3D view works today

BF4 has no stereo path of its own (`re/notes/stereo.md`), so the mod uses **alternate-eye rendering
(AER)**: each game frame renders one eye.

| Step | Where | What it does |
| --- | --- | --- |
| Head sample | `src/xr/theater.cpp` `waiter_loop` / `publish_head_sample` | A waiter thread runs `xrWaitFrame`, then `xrLocateViews` (LOCAL space) for that predicted display time and publishes both eye poses and FOVs (`src/stereo/tracking.*`) |
| Per-eye camera | `src/stereo/camera_hook.cpp` `copy_detour` | Hook on the per-frame view-params copy (`0x14083A710`, game thread). Odd copies = left eye, even = right. It composes the eye pose onto the world camera (`+0x40`) and the first-person (gun/arms) camera (`+0xED0`), sets the vertical FOV (`+0xB4`) to the headset's (times `view_crop_h/v`), and the aspect via the set-aspect hook (`0x140B89D60`). It publishes {eye, pose, FOV} as `rendered_view` |
| Store | `theater.cpp` `store_eye` | On each Present, the back buffer is copied into that eye's texture with the pose and FOV it was rendered with |
| Submit | `theater.cpp` `submit_frame` | When the waiter has a headset frame, both latest eye textures go into one array swap chain (slice 0/1) as a projection layer with each eye's own render pose. The compositor reprojects rotation only (no depth is submitted) |
| Menus/loading | `camera_hook.cpp` | Mono, shown on a virtual screen (quad layer); stereo only inside a loaded campaign level |

Facts measured so far (`docs/HEADSET-TESTS.md`, `re/notes/camera.md`, `re/notes/first_person.md`):

- RTX 5070; BF4 runs 330–400 fps uncapped. The current target is 120 fps, i.e. **60 Hz per eye**.
  The engine cap is `GameTime.MaxVariableFps` (default 200).
- One 1920×1080 back buffer per frame, used whole for one eye: rendered anamorphic at the headset's
  per-eye aspect (`stereo_aspect = headset`). The window falls back to 1280×720 when BF4 loses focus.
- The headset reports about 54° outer / 40° inner / 44° up / 55° down per eye; only about two thirds
  per axis is visible in the Quest 3 → `view_crop_h 0.78`, `view_crop_v 0.72` in the game's `bfvr.ini`
  (tuned by the user).
- The copy hook runs once per Present, and the presented image always belongs to the latest copy.
- BF4 stops presenting by itself when it is not the foreground window (not a mod bug; check
  `tools\game.ps1 status` and the pipe `status` field `stage=idle` before suspecting a deadlock).

## Likely causes of the jank (verify; do not assume)

1. **The eyes come from different moments.** Each headset frame pairs the latest left and right
   images, which are one game frame apart (about 8 ms at 120 fps). Anything that moves (the world
   while walking or stick-turning, animations, the gun) is in a different place in each eye, which
   reads as shimmer or double images. The eyes are not paired by frame either, so the same eye can
   be shown twice while the other one is stale (#6).
2. **60 Hz per eye against a 72/90/120 Hz headset**: frame pacing is not tied to the headset's
   `xrWaitFrame` cadence; the game renders freely and the waiter picks whatever is newest.
3. **Rotation-only reprojection**: no depth layer, so SteamVR cannot correct head translation or
   use motion smoothing properly.
4. **Stick turning and game pitch** move the whole world in steps that the head does not make
   (comfort, #5).
5. **Low resolution / softness** (#1, #8): 1920×1080 per eye, stretched over a wide FOV.

## Candidate directions, cheapest first

| Direction | Idea | First step |
| --- | --- | --- |
| A. Pair and pace the eyes | Render L then R for each headset frame, submit only complete pairs from consecutive copies, never repeat an eye; cap the game at 2× the headset refresh | Add per-eye age and pairing counters to pipe `status`; log copy/Present/waiter timestamps; try 90 Hz headset with a 180 fps cap and 120/240 |
| B. Freeze the world for the second eye | Render both eyes of a pair from the same game state, so only the camera differs: hold simulation time (or skip the game update) on every second frame. This removes the double images that AER causes in motion | Find the game-time step (`GameTime.*` settings, the main loop around the copy caller chain `0x140848C03 → 0x14083F1CF → 0x14082891E`, `re/notes/camera.md`) and test pausing it on alternate frames on the monitor |
| C. Depth submission | Find the scene depth buffer, submit `XR_KHR_composition_layer_depth` so SteamVR can reproject positionally and use motion smoothing | Locate the depth texture (RenderDoc capture; `tools` has none yet) and check that SteamVR's runtime accepts the extension |
| D. More pixels per eye | Render above 1920×1080 (`Render.ResolutionScale`, a larger render target, or a taller window kept independent of focus) | Measure sharpness and fps with `ResolutionScale` 1.25/1.5 |
| E. True per-eye rendering | Run the scene render twice per frame (Overlord's approach) | Research only; much later (`re/notes/stereo.md` option C) |

A and B together are the most likely to fix "janky"; C and D make it feel solid and sharp. Pick
based on what the user reports in the first session.

## Must not break

- **Arm IK** (`src/body/arm_pose.cpp`, `re/notes/skeleton.md`): the arms map the controllers through
  the first-person camera = CameraJoint + aim pitch, relative to the OpenXR LOCAL origin, and the
  synthetic `hand` targets use the head sample. Freezing game time or changing the per-eye camera
  must keep the controller poses and the `+0xED0` composition consistent.
- The gun on the aim (`stereo_first_person`), the theater screen for menus and loading, the
  multiplayer guard (`src/safety/mp_guard.cpp`), and the serialised XR calls (every runtime call from
  Present is under `xr_calls`; the waiter only try-locks, else `stop_waiter` can deadlock).

## Working rules

- Dev loop: `tools\dev-run.ps1 -Resume` (build bf4-debug, deploy, start BF4, resume the campaign),
  `tools\pipe.ps1 status | shot <png> | help`, `tools\vr_watch.py`, `tools\vr_sweep.py`
  (`docs/TOOLING.md`). If the EA app refuses launches with "AlreadyRunning", restart the EA app.
- Only one session may drive BF4 at a time; ask the user before starting the game.
- Headset tests need the user: batch them into one request with a concrete checklist, and record the
  results in `docs/HEADSET-TESTS.md`.
- No memory tools while a game with kernel anti-cheat runs (Battlefield 6, EasyAntiCheat games,
  Helldivers 2/GameGuard). An NVIDIA driver reset on 2026-10-25 froze BF4 while Helldivers 2 ran.
- Build both presets (`bf4`, `bf3`) warning-free; record verified facts in `re/notes/` and addresses in
  `re/symbols/bf4-179547.md`; work on a branch and push it; no merge to main without the user.

## First session

1. Ask the user to play about two minutes in the headset and say which of these they see:
   (a) double images or shimmer on moving things, (b) the world stutters while turning with the
   stick, (c) stutter when moving the head, (d) blur or softness, (e) the scale or depth feels wrong,
   (f) something else.
2. Instrument (direction A, first step) and measure without the headset: per-eye age at submission,
   pairs, repeats, fps against the headset refresh.
3. Pick the direction from the answer and the numbers; prototype behind a config switch; verify on
   the monitor (screenshots via the pipe), then one batched headset test.

## Done when

The user reports no judder or double images in normal movement at the chosen refresh rate, the
view feels solid, arm IK and menus still work, the findings are in `re/notes/stereo.md` and
`docs/HEADSET-TESTS.md`, and both presets build warning-free.
