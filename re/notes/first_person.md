# First-person pass (weapon, arms) — BF4 build 179547

Status: **camera found and used (2026-10-25).** The weapon/arms pass is drawn with the transform at
view-params `+0xED0`; the stereo hook now composes the eye pose onto it (`stereo_first_person`,
default on). **Headset test passed (2026-10-25):** turning the head looks away from the gun, the gun
has depth, stick turns carry it, shots go where the gun points, and looking down shows the body and
arms. The pass's FOV is still unknown.

## Layout after the three RenderViews (`[[0x142672378]+0x60]`)

| Offset | Content |
| --- | --- |
| `+0xED0` | **First-person camera**, soldier-local: rows right, up, forward, position (position = eye above the feet, e.g. (0.054, 1.56, 0.077)) |
| `+0xF10` | the same camera in world space |
| `+0xF50` | soldier (feet) position, world |
| `+0xF60…+0xFEF` | history copy: rewritten from +0xED0 or the previous frame by `0x140848B79` after our hook, so edits there are discarded |
| `+0xFF4`, `+0xFF8` | world fade, blur (GameRenderSettings) |
| `+0x100C` | **shadow-map minimum FOV** `(1 + [GameView+0x13D0])·π/180` or −1, read at `0x140CE0D8C` (not the gun FOV) |
| `+0x1008`, `+0x1010` | shadow inputs from the camera |
| `+0x1020…` | the block is an array with stride 0x1020: +0x1030 is element 1 (second split-screen view, unused) |

RenderView `+0x00` (in each of the three views) holds the same local camera; only `+0xED0` moves the gun.

## Evidence (pipe research commands, screenshots compared by mean pixel change)

| Test | Gun area | World area |
| --- | --- | --- |
| idle (two baselines) | 2–3.4 | 0 |
| `tshift` +0xF10 / +0xFA0 / +0xF60 by 0.3 m | 2.5–3.5 | 0 |
| `tshift` +0xED0 by 0.3 m | **15.4** (gun jumps to the left edge; flashlight beam moves) | 0 |
| `fakeyaw 20` (eye pose composed onto +0x40 and +0xED0) | gun and world move together (~240 px), flashlight spot stays on the same wall patch | |
| `pin` +0xB8, +0x10E4, +0x10E8, +0x100C, +0x1010 (FOV candidates) | 1.9–3.2 (no change) | 0 |

How the candidates were found: `tools/matrix_search.py` listed 71 copies of the camera rotation
reachable from the renderer, camera manager and soldier; with the world view shifted by 0.3 m
(`view 0 0 0.3`) 23 copies kept the original camera, including the +0xED0 set.

## How +0xED0 is built (static analysis, 2026-10-25)

`0x140828090` (game side) fills each view, then copies view 0's +0x00..+0x8F (local transform,
world transform, +0x80) into +0xED0..+0xF5F (`0x1408281CB`). An optional camera override at
`0x140828249` (`setTransform` `0x140B8A0A0`) runs after that copy and moves only the world view.
On the render thread `0x1408CAF70` builds a 0x1560-byte descriptor per view (weapon camera at
descriptor +0x13E0); no code reads it at a fixed offset except copies.

## The gun is the foreground pass (verified live)

| Test | Result |
| --- | --- |
| `WorldRenderSettings.ForegroundEnable` (+0x12E) = 0 | **gun and arms disappear** (not redrawn in the world pass) |
| `ForegroundDepthClearEnable` (+0x12F) = 0 | no visible change (no wall close to the gun) |
| view 0 eye separation (+0xD0) = 0.3 | world and gun shift together: the foreground projection shares view 0's inputs |
| view 0 near plane (+0xBC) = 0.6 | gun not clipped |

The live WorldRenderSettings is found through the settings manager `[0x14261E930]` (lookup
`0x140688C50(manager, ClassInfo)`), 3 pointer hops deep, identified by its getType().
All projections go through `0x140B7EA10` → `0x140B7EAE0` (inputs: RenderView +0xB0 type,
+0xB4 fov, +0xBC near, +0xC0 far, +0xC4 aspect, +0xD0 eye separation, +0xD4 convergence).

## Gun size lever (verified live)

Multiplying the first-person camera's right and up rows (+0xED0, +0xEE0) by g scales the drawn gun
and arms by g around the view centre, world unchanged, no visible artefacts (rows x1.43 → clearly
larger gun). `gun_scale` (config, pipe `gunscale <g>`) applies this every frame; the right value
for VR still has to be tuned in the headset (the gun's own FOV is unknown).

## Open: the first-person FOV

- The gun keeps its size when the world FOV changes (camera.md), so the pass has its own projection.
- No 16:9 projection matrix other than the world's (45°, 34 copies) is reachable from the renderer,
  camera manager or soldier within 3 pointer hops: the first-person projection is probably built
  on the render thread directly into a constant buffer.
- Next: RenderDoc capture of a weapon draw → its projection constant → the CPU code that fills it
  (or the FOV value it is built from). Candidates from TypeInfo: `FPSCameraData.FOV` (+0x108),
  `GameRenderSettings.FovMultiplier` (+0x4C), `WeaponZoomLevelData.RenderFov`.

## Gun life size (2026-11-02)

Static analysis (branch `fp-fov-research`, notes there): the foreground is drawn at a **fixed 45 deg
vertical FOV** with the world's aspect (projection y scale 2.414 = cot 22.5 deg, measured in
stereo-rendering.md). In the headset the world uses the rendered FOV (~96 deg vertical), so the gun and
arms were magnified about the view centre by cot(22.5)/cot(half fovY), ~2.7x, and sat off the
controllers. `gun_life_size` (default on) scales the first-person right/up rows by
tan(22.5 deg)/tan(fovY/2) every head-tracked frame, on top of `gun_scale`; pipe `gunscale` prints the
applied value.
