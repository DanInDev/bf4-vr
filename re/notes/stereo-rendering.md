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

Conclusion: the engine's renderer is multi-view by design (view arrays, per-view targets, per-view
clears), but this build is configured for exactly one display window and one view. True per-eye
rendering through it needs (1) a second valid viewport/window entry or views pointing at window 0 with
their own targets, and (2) every per-view array the world system allocates for one view found and
sized for two. Each freeze can be localised with `threads` plus a write-watch on the corrupted memory.

## Next steps for per-eye rendering

1. Give view 1 valid targets: set `+0xEA8/+0xEB0` of view 1 to our own render target pair before the
   setup call (so the per-index viewport lookup is skipped), and override the per-index aspect read.
2. Find the world system's per-view allocations: `0x140B8C660` (receives count, renderViews,
   viewSizes) and the culling jobs; look for fixed `count == 1` assumptions and arrays indexed by view.
3. Alternative: drive the second eye at render-module level by re-running the world renderer's
   per-frame preparation (find what the dispatcher waits on, via `threads` during the `worldpass 2` hang).

## HUD (#5): plan

The HUD is drawn last, after the render modules, by `0x14063E210` into the back buffer. Wrapping that
call and swapping the back-buffer render target for our own transparent texture gives a HUD-free eye
image plus a HUD texture that can be shown as its own OpenXR quad layer (sharp, placed in the visible
area, not bleeding into the lens corners).
