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
| `+0xF60…+0xFEF` | a second copy of that set (no visible effect when shifted) |
| `+0x1030…` | a fourth view description (local at +0x1030, world at +0x1070, FOV 0.7854 at +0x10E4, 0.96 at +0x10E8, near 0.06, far 20000, aspect 1.778) |
| `+0x1008…+0x1014` | 1.0, 0.8029, 0.99, 100.0 (no visible effect on the gun) |

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

## Open: the first-person FOV

- The gun keeps its size when the world FOV changes (camera.md), so the pass has its own projection.
- No 16:9 projection matrix other than the world's (45°, 34 copies) is reachable from the renderer,
  camera manager or soldier within 3 pointer hops: the first-person projection is probably built
  on the render thread directly into a constant buffer.
- Next: RenderDoc capture of a weapon draw → its projection constant → the CPU code that fills it
  (or the FOV value it is built from). Candidates from TypeInfo: `FPSCameraData.FOV` (+0x108),
  `GameRenderSettings.FovMultiplier` (+0x4C), `WeaponZoomLevelData.RenderFov`.
