# Task brief: true per-eye rendering (both eyes from the same game frame)

Written 2026-10-25 after the first research session (`feature/render-quality`). Read `CLAUDE.md`,
`docs/SAFETY.md` (binding) and this file first. Raw evidence: `re/notes/stereo-rendering.md`;
addresses: `re/symbols/bf4-179547.md`. Related issues: #6 (alternate-eye pacing), #1 (sharpness/FOV).

## Goal

Render the world twice per game frame, once per eye, with each eye's own view and projection
(Overlord's approach), instead of alternate-eye rendering (AER: one eye per game frame).

The user decided on 2026-10-25 that this is the way to do VR properly. Their headset verdict on AER:
the view is "not that bad", but "the two eye rendering is not working properly so the gun, for
example, looks weirdly small while your tracked arms and similar look quite big".

## Why AER is not enough

| Symptom | Cause under AER | Fixed by per-eye rendering |
| --- | --- | --- |
| Gun and near objects have the wrong size and depth | The eyes are one game frame (~8 ms) apart, so anything that moves with the player (the gun, arms, the world while walking) sits in a different place in each eye. Stereo disparity on near objects is wrong, which the brain reads as wrong size | Yes: both eyes see the same moment, so the only difference between the eyes is the eye offset |
| Shimmer or double images in motion | Same cause | Yes |
| 60 Hz per eye at 120 fps | Each game frame serves one eye | Yes: each game frame serves both eyes |
| Rotation-only reprojection | No depth submitted | No: separate task (depth layer, `docs/tasks/smooth-stereo.md` direction C); the resolved scene depth is already located |

Cheaper partial fix: freeze the simulation on every second frame so both AER eyes come from the
same game state (`smooth-stereo.md` direction B). That removes most of the disparity error but keeps
60 Hz per eye and doubles input latency for one eye. Worth trying first if per-eye rendering stalls.

## What the renderer looks like (verified)

Full tables: `re/notes/stereo-rendering.md`. The parts that matter for this task:

- **One frame** (pipe `frametrace`, 2160×2160): UI atlases → env probe → G-buffer with 4× MSAA
  (4 × 2160² + D24S8) → shadow cascades → SSAO at half resolution → tiled compute lighting, HDR,
  transparents → exposure and bloom chains → tonemap into the back buffer → HUD (Twinkle, UI render
  module). All of it runs on the render thread from the immediate context.
- **Render frame** `0x1408463A0` (render thread, `this` = GameRenderer `[0x142672378]`): reads the
  view count `+0x58` and view array `+0x60` (stride `0x1020`), copies each RenderView into a
  per-frame array, hands it to the world system `[0x142738070]` (`0x140B8C660`, receives count,
  render views and view sizes), then runs up to six render modules (world, post, UI).
- **World module** `0x1408BEC40`: calls the world renderer (G-buffer, shadows, lighting through the
  dispatcher `0x140CE5EDE`), then loops over its own views (count `+0x548`, array `+0x550`) and
  binds/clears each view's target (`+0xEA8`).
- **View setup** `0x140848B00` (game thread, `(GameRenderer*, count, source views)`): reallocates
  the view arrays when the count changes, copies each view (through our camera hook `0x14083A710`),
  sets FOV/aspect from viewport *i*, fills the view's targets `+0xEA8/+0xEB0` from viewport `+0xEC0`
  when they are empty, and keeps the previous frame's views (`+0x68/+0x70/+0x78`). The game calls it
  from `0x14083F1CA` with count 1.
- **View block** (`0x1020` bytes): `+0x0` current RenderView, `+0x4E0` previous frame's RenderView
  (motion vectors / temporal effects), `+0xED0…+0xF5F` first-person (gun/arms) transforms,
  `+0xF60…` their previous copy, `+0xEA8/+0xEB0` targets, `+0xEC0` viewport index.
- **Viewports**: vtable slot 12 of `[0x142738080]` (`0x140BFF590`) returns viewport *i* =
  `[mgr+0x38] + 0x58 + i*0x138`. This is the same display-window array the swap-chain check
  `0x140BF4E60` walks, and its count `[mgr+0x30]` is **1**.

Conclusion: Frostbite's renderer is multi-view by design (view arrays, per-view targets and
clears), but this build is set up for exactly one display window and one view.

## What was tried and what happened

| Attempt | How (`src/stereo/world_pass.cpp`, research build) | Result | What it tells us |
| --- | --- | --- | --- |
| Run the world module twice per frame | pipe `worldpass 2 <shift>`: call the original world module render twice, moving the camera between calls | **Render thread hangs** on the first double call | The world renderer waits on per-frame state (job fences / command lists) that the first pass consumed. Doubling at module level needs that state reset or re-prepared |
| Two views, switched on in a running level | pipe `viewcount 2 <shift> <viewport>`: the view setup detour passes two copies of the view, the second shifted | Game thread stops after one frame | Changing the count reallocates the view arrays while the render thread uses them |
| Two views from game start | `research_views = 2` in `bfvr.ini`: same, hooked before the first frame | Freezes after ~154 frames in the front end | Starting with two views gets further, then memory corrupts |
| Stack sample of that freeze | pipe `threads` | Render thread idle. The game thread loops in EA's TLS certificate code (`0x1406CBFD0`), which has nothing to do with rendering | Unrelated code looping = **memory corruption**. View 1 asks for viewport/window 1, which does not exist, and other per-view arrays are sized for one view |

None of these are enabled in normal builds: `worldpass`/`viewcount` are research-only pipe commands
and `research_views` defaults to 1.

## Tools built for this (all in the branch)

| Tool | Use |
| --- | --- |
| pipe `frametrace <path>` | One frame of immediate-context calls, with caller and 12-frame call stacks for every non-draw event. Shows which engine function binds which target |
| pipe `threads` | CPU time and unwound stack of every game thread. First step on every hang/freeze |
| pipe `worldpass`, `viewcount` | The two experiments above, toggled at runtime |
| `tools/strxref.py` | String → RIP-relative code references in the decrypted dump |
| `tools/disasm.py`, `tools/callers.py`, REA + Ghidra | Static analysis of `re/dumps/bf4-179547-mem.exe` (decrypted dump, never committed) |
| HUD capture (`src/stereo/hud_capture.cpp`) | Already takes the HUD out of the eye image. Per-eye rendering needs the HUD once per frame, not per eye, which this already does |

## Approaches, most promising first

### 1. Two views through the engine's own multi-view path (recommended)

The engine already loops over views in the render frame and the world module. Make it work for
two:

1. **Valid targets for view 1.** Before calling the original view setup, fill `+0xEA8/+0xEB0` of
   view 1 with our own colour/depth target pair (2160² or smaller), so the setup skips the
   per-index viewport lookup. Also set `+0xEC0` = 0 and override the aspect/FOV it reads from the
   viewport (or hook `0x140BFF590` to return viewport 0 for index 1).
2. **Find every array sized for one view.** Start in the world system `0x140B8C660` (gets count,
   render views, view sizes) and the culling jobs it starts. Look for fixed-size members indexed by
   view, `count == 1` assumptions and allocations made once at level load. The 154-frame freeze is
   the reproducer: set a hardware write watch on the corrupted memory (the TLS loop's data) to find
   the writer, fix that array, repeat.
3. **Allocate before the first frame.** Keep `research_views = 2` at game start (switching the count
   in a running level crashes immediately).
4. **Per-eye camera.** Move the AER code in `camera_hook.cpp` `copy_detour` from "odd/even copy" to
   "view 0 = left, view 1 = right", both from the same head sample. The first-person transforms
   `+0xED0` need the per-eye composition per view.
5. **Collect the eye images.** View 0 goes to the back buffer as today. View 1 goes to our target,
   then the post module (tonemap) must run for it too: check whether the post module loops over views
   or only handles view 0. If only view 0, run the tonemap for view 1 ourselves or call the post module
   a second time with view 1 swapped in.
6. **Submit** both images from the same frame into the existing array swap chain (`theater.cpp`).

Risks: temporal effects keyed by view (the previous-frame view at `+0x4E0`, exposure/bloom history),
shadow cascades computed per view (double cost, or shared if the engine shares them), and GPU cost
(two 2160² passes with 4× MSAA: drop the per-eye size to ~1600² or MSAA to 2× if fps falls below
90).

### 2. Run the world renderer twice, re-preparing its per-frame state

Repeat the world module call with the second eye's camera, after resetting what the first call
consumed.

1. Reproduce the `worldpass 2` hang and run `threads`: the render thread's stack names the wait
   (fence, job, event) inside the dispatcher `0x140CE5EDE`.
2. Find who signals it and what per-frame preparation the render frame `0x1408463A0` does before the
   modules (render-view copy `0x140B79380`, world system `0x140B8C660`).
3. Re-run that preparation for eye 2, or prepare both eyes up front.

This avoids the viewport/window problem but fights the job system directly. Second choice.

### 3. Record and replay the world pass (fallback)

Record the world module's D3D11 calls for eye 1 (the trace already captures them) and replay them
for eye 2 with the view constant buffers swapped (view/projection and their inverses). Engine-
independent, but compute passes, culling (objects outside eye 1's frustum are missing from eye 2:
small with a 6.4 cm eye offset), and finding the camera constants in the right cbuffers make it
fragile. Only if 1 and 2 both stall.

Rejected: geometry duplication per draw (3D Vision style), see `re/notes/stereo.md` option B.

## Must not break

- Arm IK (`src/body/arm_pose.cpp`): controller poses through the first-person camera; with two
  views, the IK must run once per frame from the head (centre) sample, not per eye.
- HUD layer (`hud_layer = 1`), the theater screen for menus/loading, the multiplayer guard,
  virtual fullscreen at `render_size` (`src/hooks/swap_chain_size.cpp`), serialised XR calls.
- AER must stay available as the default until per-eye rendering is verified in the headset.

## Working rules

Same as `docs/tasks/smooth-stereo.md`: `tools\dev-run.ps1 -Resume`, only one session drives BF4,
headset tests batched with a checklist into `docs/HEADSET-TESTS.md`, no tools while kernel
anti-cheat games run, verify every address by prologue bytes before hooking and record it in
`re/symbols/bf4-179547.md`, build `bf4` and `bf3` warning-free, branch and push, no merge to main
without the user. Research hooks stay behind config switches that default off.

## First session

1. Approach 1, steps 1–3 on the monitor: start with `research_views = 2` and view 1 on our own
   targets, then find the first corrupted array with `threads` + a write watch. Record each array
   in `re/notes/stereo-rendering.md`.
2. Goal of the session: the game runs stable in a level with two views, view 1's image visible in a
   `shot`-style capture of our target (any camera offset).

## Done when

Both eyes render from the same game frame at ≥ 90 fps per eye, the user reports in the headset that
the gun and arms have a consistent size and no double images in motion, arm IK, the HUD layer and
menus still work, findings are in `re/notes/stereo-rendering.md` and `docs/HEADSET-TESTS.md`, and
both presets build warning-free.
