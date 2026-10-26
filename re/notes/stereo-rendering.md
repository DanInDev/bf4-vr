# True per-eye rendering and the frame structure (#5/#6 groundwork)

Session 2026-10-25 (`feature/render-quality`). Goal: render both eyes from the same game frame
(Overlord's approach) instead of alternate-eye rendering, and take the HUD out of the eye images.
Tools: pipe `frametrace <path>` (one frame of D3D11 calls with callers and call stacks), pipe
`threads` (stack sample of every thread), REA + Ghidra on the decrypted dump.

## One frame at 2160x2160 (frametrace, level, 1790 calls, all on the render thread)

| Order | Pass | Targets | Issued from |
| --- | --- | --- | --- |
| 1 | UI preparation (glyph/minimap atlases) | 256², 64² → 4096² atlas | renderer start |
| 2 | 864² pass (env/sky probe) | 864² f26 + depth | world module |
| 3 | **G-buffer**, 4× MSAA | 4 × 2160² (f28, f28, f28, f26) + D24S8 | world module |
| 4 | Shadow cascades | 1600², 800² (×4) | world module |
| 5 | SSAO at half resolution | 1080² f41/f34 | world module |
| 6 | Tiled lighting (compute 135×135 = 16-px tiles), HDR lighting, transparents | 2160² f10 (RGBA16F) + resolved depth `D24S8 2160²` | world module |
| 7 | Exposure chain (256×128 … 1×1), bloom chain (1080 … 33) | f10 / f26 | post module |
| 8 | Tonemap → 2160² f28, final pass into the back buffer | back buffer | post module, fullscreen helper `0x140BF3A29` |
| 9 | **HUD** (Twinkle): ~75 indexed draws from `0x140C81BD2` straight into the back buffer, with a stencil clear | back buffer + resolved depth | after the render modules |

The resolved scene depth (D24S8, 2160², bound with the back buffer) is the candidate for
`XR_KHR_composition_layer_depth` (smooth-stereo direction C).

## Engine structure (verified by decompilation and call stacks)

| Address | What | Notes |
| --- | --- | --- |
| `0x1408463A0` | **Render frame** (render thread), `this` = GameRenderer (`[0x142672378]`) | View count `+0x58`, view array `+0x60` (stride `0x1020`); copies each RenderView (`0x140B79380`) into a frame-allocated `renderViews` array, hands it to the world system `[0x142738070]` (`0x140B8C660`), runs up to 6 **render modules** (`this+0x100…`, vtable slot 3; call returns to `0x1408466D3`), then the 2D/HUD overlay (`0x14063E210`, returns `0x1408467A1`), the debug overlay (`0x140846800`) and `TwinkleOverlayManager::postRender` |
| `0x1408BEC40` | **World module** render (vtable `0x141D24258` slot 3) | Calls the world renderer (`module+0x30`, slot 3 → dispatcher `0x140CE5EDE`: G-buffer `0x140CE810D`, shadows `0x140CE89F9`, lighting `0x140CE8FA6`); then per view (count `+0x548`, views `+0x550`, stride `0x1020`) binds and clears the view's target (`+0xEA8`) |
| post module | same class as module 0 (both return through `0x14016920D`) | tonemap and final pass |
| `0x140848B00` | **View setup** (game thread): `(GameRenderer*, count, source views)` | Reallocates the view arrays when the count changes, copies each view (our hooked `0x14083A710`), sets FOV/aspect per view from viewport *i*, fills `+0xEA8/+0xEB0` targets from viewport `+0xEC0` when empty, keeps the previous frame's views (`+0x68/+0x70/+0x78`). Called from `0x14083F1CA` with count 1 |
| view block layout | `0x1020` bytes | `+0x0` RenderView (current), **`+0x4E0` RenderView of the previous frame** (camera.md said "first-person view": corrected), `+0xED0…+0xF5F` first-person transforms, `+0xF60…+0xFEF` their previous-frame copy, `+0xEA8/+0xEB0` targets, `+0xEC0` viewport index, `+0xFF4/+0xFF8` floats |
| `[0x142738080]` vtable slot 12 (`0x140BFF590`) | viewport *i* = `[mgr+0x38] + 0x58 + i*0x138` | The same 0x138-stride **display window** array the swap-chain check `0x140BF4E60` walks (count `[mgr+0x30]` = 1) |

## Experiments

| Experiment | Result |
| --- | --- |
| Render the world module twice per frame (`worldpass 2`) | **Render thread hangs** on the first double call: the world renderer waits on per-frame state (jobs/fences) that the first pass consumed. Rendering twice at module level does not work without driving that state |
| Two views via the view setup, switched on in a running level (`viewcount 2 1.0 0`) | Game thread stops after one frame |
| Two views from game start (`research_views = 2`) | Freezes after ~154 frames in the front end |
| Stack sample of the freeze (`threads`) | Render thread idle (waiting for work); the **game thread spins in EA's TLS certificate code** (`0x1406CBFD0`: DER subjectAltName, tag 0x82, wildcard host matching) in an endless loop. Unrelated code looping = **memory corruption**: view 1 asks for viewport/window 1, which does not exist, and other per-view arrays are sized for one view |

**Correction 2026-10-25 (second session):** the "TLS loop = memory corruption" reading was wrong.
`worldpass 2` reproduced the same stack on the **render thread** (pipe `present_thread`), and the
bottom frames resolve to `kernelbase!UnhandledExceptionFilter` → ntdll exception dispatch → the
game's unhandled-exception filter `0x140007639` → crash reporter → HTTPS upload (ProtoSSL stream
write `0x140AC4140` → handshake `0x1406CD3F0` → certificate hostname match `0x1406CBFD0`, which loops
forever on a null cursor with a non-zero length). So **both experiments crash the render thread**,
and the crash reporter then spins instead of exiting. Thread CPU grows ~1 s/s in that loop. The
real fault is logged by the research crash logger (`src/control/crash_log.cpp`, `research = 1`).

| Address | What |
| --- | --- |
| `0x140007639` | Game's unhandled-exception filter (called from `UnhandledExceptionFilter+0x1F3`) |
| `0x1406CBFD0` | ProtoSSL: DER subjectAltName (tag `0x82`) wildcard hostname match; infinite loop on bad input |
| `0x1406CD3F0` / `0x140AC4140` | ProtoSSL connection update / blocking stream write |

World renderer frame `0x140CE56F0` (the "dispatcher"; world module → `module+0x30` vtable slot 3):
loops over its **own view slots**, count `+0x214`, slot stride `0x34000` inline in the object, view
data at `+0x280 + i*0x34000`, per-slot context at `+0x34040 + i*0x34000`, frame argument stored at
`+0x34258 + i*0x34000`, and calls the per-view render `0x140CE7EA0` (14 KB: G-buffer, shadows,
lighting; only caller) once per slot. Before the loop: per-frame callbacks (`+0xB0..+0xB8` list,
vtable `+0x18`; after the loop `+0x20`), sync points `+0x34378/+0x34390/+0x343A8` (each checked
with `0x140D1E570`/`0x140D1CF50`, waited with `0x14063CA00`/`0x14063C940` inside a renderer lock
`[0x142738080]` slots `+0x48`/`+0x40`).

Conclusion: the engine's renderer is multi-view by design (view arrays, per-view targets, per-view
clears), but this build is configured for exactly one display window and one view. True per-eye
rendering through it needs (1) a second valid viewport/window entry or views pointing at window 0 with
their own targets, and (2) every per-view array the world system allocates for one view found and
sized for two. Each freeze can be localised with `threads` plus a write-watch on the corrupted memory.

## Next steps for per-eye rendering

The task brief with the ranked approaches and a first-session plan is `docs/tasks/per-eye-rendering.md`.

1. Give view 1 valid targets: set `+0xEA8/+0xEB0` of view 1 to our own render target pair before the
   setup call (so the per-index viewport lookup is skipped), and override the per-index aspect read.
2. Find the world system's per-view allocations: `0x140B8C660` (receives count, renderViews,
   viewSizes) and the culling jobs; look for fixed `count == 1` assumptions and arrays indexed by view.
3. Alternative: drive the second eye at render-module level by re-running the world renderer's
   per-frame preparation (find what the dispatcher waits on, via `threads` during the `worldpass 2` hang).

## HUD layer (#3/#5): built, monitor-verified 2026-10-25

Correction to the frame table: the HUD is not drawn by the overlay call `0x14063E210`. Call stacks of
the HUD draws show the **UI render module** (third module slot): `0x1401691C9` (module render,
dispatches on the slot) → **UI render `0x1408863E0`** (binds the back buffer at `0x14088658E`, HUD via
`0x140886640` → `0x140897E82` → draws from `0x140C81BD2`). The world module ends with the tonemap
into the back buffer (`0x140CEAC6B`).

`src/stereo/hud_capture.cpp` (`hud_layer = 1`): wraps the UI render; while it runs on a head-tracked
frame, `OMSetRenderTargets(back buffer)` binds our transparent 2160² texture instead (cleared once per
frame). The theater shows it as a head-locked quad (`hud_distance` 1.5 m, `hud_width` 1.3 m ≈ 47°),
premultiplied alpha. Menus and loading screens are untouched (theater screen).

| Check (monitor, `hud_layer = 2` = every frame) | Result |
| --- | --- |
| Eye image | HUD-free: world, hands, gun only |
| HUD texture (`hudshot`) | minimap, ammo, objective, squad markers, crosshair; alpha 0–255 |
| Pitfall | The detour must pass all six argument slots through bit for bit (64-bit): declaring them `int`/`float` changed the callee's register/stack view and leaked a Twinkle lock (`0x141110102`), freezing the game thread |
| Known limitation | 3D markers (squad tags, objectives) are screen-space HUD too: on a head-locked layer they no longer sit on their targets |

## Per-eye pass on the per-view render (session 2, 2026-10-25)

`src/stereo/eye_pass.cpp`, pipe `eyepass <flags> [shift m]`, `eyeshot <path.png>`; off by default.
Hooks the world per-view render `0x140CE7EA0` and calls it twice per frame for slot 0, the camera
RenderViews in the slot (`+0x630`, `+0xB10`, sub-pass copies `+0x186A0`/`+0x19060`) moved sideways
for one of the calls (`RenderView::update 0x140B8C180`, flags `0xF`), the first eye's back buffer
copied to its own texture.

| Check (monitor, 1280x720, saved campaign checkpoint) | Result |
| --- | --- |
| Stability, `eyepass 1 0.5`, `tools/soak.ps1 -Minutes 5` | **PASS**: 297 samples, no crash/stall, 10781 complete pairs in the first minute, 0 rejected; lowest fps 146.7 (cap ~200) |
| GPU work | frametrace: **1925 draws per frame with the pass vs 1000 without** (all on the render thread) |
| CPU cost of the second call | `eye_ms` 0.29 ms: the call only submits draw lists that job threads built earlier |
| Camera shift visible? | **No**: `eyeshot` (shifted eye) and `shot` (normal eye) show the same viewpoint; both are complete frames (the eye image has no HUD, as expected) |

Why the shift has no effect: the camera is baked into **dynamic constant arenas** before the
per-view render runs. frametrace with constant-buffer tracking (`Map`/`Unmap`/`UpdateSubresource`
and `VS/PS/CSSetConstantBuffers`, camera position search) shows two 1 MB dynamic buffers mapped
`NO_OVERWRITE` and unmapped from `0x140C12490` (`+0x63`, through the device wrapper vtable `+0x78`) on
the render thread **and on job threads**, each holding the camera position at many offsets (per-draw
or per-pass constant blocks). The second render replays draw lists that point at the same arena data.

Consequence: per-eye rendering needs per-eye **constant data**, not only a per-eye RenderView:

1. Find the writer of the view constants into the arena (the job that fills per-view/per-draw
   constant blocks from the RenderView; breakpoint the writes near a `cam@` offset with `watch`),
   and make it produce an eye-2 copy (Overlord: `engine_stereo_dynamic_arena`, "dynamic uploads"
   in the per-eye transaction).
2. Cheaper probe first: before the second render, rewrite the centre camera's view, projection and
   view-projection matrices (and inverses, camera position) inside the mapped arena ranges with the
   eye-2 values, render, restore. Works only if per-object constants keep world and view separate.

## Eye-2 constants and pair rendering (session 3, 2026-10-25/26)

Constant data, found with `eyeprobe` (bindings of one first-eye render + every bound buffer read back):

| Finding | Evidence |
| --- | --- |
| Draws bind 256-byte (and 512/3072/5888/12800-byte) blocks of 3 dynamic 1 MB constant arenas through `VS/PS/CSSetConstantBuffers1` with offsets (~2100 bindings per frame) | `eyeprobe` bindings |
| Per-draw blocks hold the object's world matrix (3×4); per-view blocks hold copies of the RenderView's view-projection (`+0x420`, transposed `+0x460`), projection (`+0x320`), camera position | float dumps vs `probe-slot.bin` |
| RenderView layout additions: transform `+0x40`, view `+0x260`, projection `+0x320` (x/y scale 1.358/2.414 = 72.7° h at 16:9), view-projection `+0x420`, transposed `+0x460` | slot dump |
| Blocks bound by eye 1 were written by job threads before the render; eye 2 also binds ~180 blocks that the **render thread writes during the render**, still with the centre camera | counters `eye_late` |
| Reading the arenas through their mapping is slow (GPU memory over the bus); GPU copy to staging after eye 1 costs ~4 ms incl. waiting for eye 1 | `eye_readback_ms` |
| Debug-build timings are meaningless for this path (37 ms → 2 ms in Release) | |

Eye-2 constants (`eyepass` flag 8, `src/stereo/constant_patch.*`, tested): every 16-byte aligned window of
the slot's RenderViews that changes when the camera moves is a replacement pair (25 per frame); blocks
bound by eye 1 are patched in one upload, late blocks are read live and patched one by one; bindings of
eye 2 are redirected to our own 4 MB constant buffer. Result: world disparity correct in sign and order
(fake 64 mm head at 1280×720: walls −8 px, far window −5 px). Release, monitor, both eyes every frame:
130–140 fps; eye 2 ~2 ms CPU, preparation ~4 ms.

Pair rendering (`stereo_render = pair`, pipe `stereorender`): the camera hook gives every copy the left eye
pose and the eye pass the IPD; the theater takes the eye pass's first image as the left eye and the
Present image as the right eye (`store_pair`, only for a newly completed pair). 10-minute soak: see
`docs/HEADSET-TESTS.md`.

**The gun and arms are not offset in eye 2.** Their screen position does not depend on any constant this
path changes:

| Experiment | Gun | World |
| --- | --- | --- |
| Shift the slot's first-person transforms `+0xFF0/+0x19C0/+0x1A50` before eye 2 | no change | – |
| Probe diff with `tshift 0 ed0 0.1` (moves the gun a lot) | changed windows: 512-byte slot-2 blocks (+0x30 view, +0xB0 view-projection; a mirrored world camera, likely reflection/probe) and the first-person view-projection at `+0x80` of first-person draw blocks (part matrix at `+0x40`, soldier-local) | – |
| Move every view-projection built with the game projection (flag 32, `shift_view_projections`, unit-tested) | no change at 0.1 m or 1 m | correct |
| Move the first-person part matrix too | no change | – |
| Also shift pure projections | −6 px only | broken (far wall −15 px) |

Next suspect: skinning. The first-person meshes are skinned; bone matrices probably live in a
non-constant buffer (structured buffer / SRV) that jobs fill. Find the gun draws' shader resources
(`VSSetShaderResources` around the draws using the `+0x80` first-person view-projection) and the
buffer they read.

## The gun and arms get their eye offset (session 4, 2026-10-26 night)

How they were found (all research pipe commands, pair mode with `fakehead 0.064`):

| Step | Result |
| --- | --- |
| `eyepoison <stage> <slot> <bytes>` (zero one constant category in eye 2; `tools/poison_sweep.ps1`) | VS slot 0 256-byte = static world (vanishes), VS slot 0 3072-byte = skinned arms (bone palettes), VS slot 2 512-byte = per-view constants (all white); the gun survived every category it was not drawn with |
| `eyeskip <from> <to>` (skip eye-2 draws; `tools/skip_sweep.py`) | Draws 40–80 of eye 2 draw the gun (black silhouette when skipped); the first-person pass comes first in the frame |
| `eyeprobe2` (record eye 2's bindings, draw markers `D`, redirects `R`) | The first-person draws bind VS slot-2 512-byte blocks that were **never redirected** |

The first-person per-view block has the world block's layout but its own camera and projection:

| Offset | Content (first-person, pair mode, 1280×720) |
| --- | --- |
| `+0x0` | time, time |
| `+0x10` | viewport w, h, 1/w, 1/h |
| `+0x30` | view matrix rows `R | t` (soldier-local camera, e.g. at (0.66, 2.09, 0.39)) |
| `+0x70` | projection (**own FOV**: x/y scale 2.001/2.414 while the world's was 0.932/1.125) |
| `+0xB0` | view-projection |
| `+0xF0` | rotation-only view-projection |
| `+0x130` | (sx, sy, 1.000003, near) |
| `+0x140` | camera position (= −Rᵀt) |

No RenderView value appears in it (own projection), so the table never matched it and the "world
projection" rule (flag 32) never did either. `constant_patch::shift_per_view_block` recognises the
layout by its own consistency and moves the camera by d along its right axis (view t.x −= d,
view-projection row-0 translation −= d·sx, camera position += d·right); applied to 512-byte blocks the
table did not change. Unit-tested on random cameras; pitfall: `+0x138` is 1.000003, not 1.

Monitor result (64 mm): world −5/−8 px, gun scope **−297 px** (expected ≈ 0.064·2.0/0.3·640 ≈ 273 px for
a scope ~0.3 m away). With a 1 mm shift the eyes differ only along the gun's edges (no inconsistency).
In eye 2 the gun is seen from a visibly different side (12° at 0.3 m) and can look darker; whether that
matches reality is a headset question.
