# First-person pass (weapon, arms) — BF4 build 179547

Status: **camera found and used (2026-10-25).** The weapon/arms pass is drawn with the transform at
view-params `+0xED0`; the stereo hook now composes the eye pose onto it (`stereo_first_person`,
default on). **Headset test passed (2026-10-25):** turning the head looks away from the gun, the gun
has depth, stick turns carry it, shots go where the gun points, and looking down shows the body and
arms. The pass's FOV is a fixed 45° vertical with the world's aspect (measured); its source is
ranked in "First-person FOV: static analysis" below.

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
- Already measured (stereo-rendering.md, session 4): the first-person per-view constant block has
  projection x/y scale 2.001/2.414 while the world's was 0.932/1.125. **2.414 = 1/tan(22.5°): the
  foreground is drawn at a fixed 45° vertical FOV with the world's aspect** (2.414/2.001 = 1.125/0.932).

## First-person FOV: static analysis (2026-11-02, dump only, game not running)

Result in one line: the foreground FOV is **45° vertical**, independent of the world FOV, with the
world's aspect; the engine value that is 45° and tied to first person is the per-player view's
**+0x13D0** (level default FOV in degrees). The code that turns it into the foreground projection
was not found statically, so candidate 1 needs the live write test below. Whatever the source, the
gun-size lever is exact: **`gun_scale = tan(fg/2) / tan(fovY/2)`** makes 1 game metre in the
foreground equal 1 m in the world (fg = 45°, fovY = the world's vertical FOV at RenderView +0xB4).

### Why gun_scale with that value is exact

The foreground uses the first-person rows (+0xED0) as its view transform (transposed, which is
why scaling the rows enlarges the gun) and its own projection: x_ndc = x·cot(fg/2)/aspect,
y_ndc = y·cot(fg/2). The world uses cot(fovY/2) with the same aspect. Scaling the right/up rows by
g scales view-space x and y by g and leaves depth alone, so the gun lands exactly where world
geometry at the same soldier-local position would when g·cot(fg/2) = cot(fovY/2). In stock BF4 the
campaign world FOV is also 45°, so g = 1 there. In VR the hook sets +0xB4 = 2·half_vertical, so
g = 0.41421 / tan(half_vertical) (e.g. half_vertical 48° → g = 0.373; `fov 1.6` → g = 0.4023).
If hands still miss the controllers at this g, the error is in the IK target or the pose
composition, not in the projection.

### Ranked candidates for the foreground FOV source

| # | Value | Address / how to reach it | Written by | Read by | Evidence |
| --- | --- | --- | --- | --- | --- |
| 1 | **Per-player view +0x13D0**, float degrees, ctor default 45.0 | manager `[0x142673D80]`, view i = `[[mgr+8]+i*8]` (count `[mgr+0x10]`), getter/creator `0x1408AAF90(mgr, i, create)` | `0x1408B08C7` (setter `0x1408B08B0`) from level setup `0x1408203DA`: `LevelData.DefaultFOV (+0xB0) × LevelData.InfantryFOVMultiplier (+0xB4)` | `0x1408A9385`: view params `+0x100C` = (k + fov)·π/180 when the active camera's vfunc +0x58 is true, else −1 (k = float `[0x142310EC8]`, 0.0 in the dump, older note says 1); `0x1408AB187`: ratio camera FOV / this → Ant update (`0x140823F5F`, `0x140825CF6`); `0x1408A69AD`: copied into the default camera +0x100 at construction; getters `0x1408AB100`, `0x1408AB0E0` | Only engine value of 45 tied to first person (it sizes the first-person shadow frustum, read as `V+0x1B04` at `0x140CE0D8C`); matches the measured 45.000°. Consumer of the foreground projection **not found** |
| 2 | `WeaponZoomLevelData.RenderFov` (+0x10), `WeaponZoomModifier.ZoomRenderFov` (+0x18) | weapon zoom data (TypeInfo) | data | not traced | Name; probably the foreground FOV while aiming down sights. Live check 5 |
| 3 | `FPSCameraData.FOV` (+0x108) | camera data | data | probably the active camera's +0x100 | Feeds the **world** FOV: `0x1408AE3BD` takes `[[view+0x1D0]+0x100]` (else `[[view+0xEE0]+0x6C]`) + offset, ×π/180 → `setFovY` `0x1408AE417` on the view's RenderView (+0xEF0); overrides from the list at view +0x13E0 (`0x1408AE458`) |
| 4 | `GameRenderSettings.ForceFov` (+0x48) / `FovMultiplier` (+0x4C) | `[GameRenderer+0x48]` | data | ForceFov: `0x140848CA4` → `setFovY` on view 0 (degrees) unless it is −1; FovMultiplier: no read found in the view setup | World only |
| – | RenderView +0xB8 (0.96 rad) | – | constructor `0x140B77FF0` writes 0x3F75BE0C (55°) | – | **Rejected**: a constructor default, not the menu setting; pinning it changed nothing (table above) |
| – | A separate foreground RenderView in the world renderer | – | – | – | **Rejected**, see below |

### What the renderer does with the foreground (verified by disassembly)

- Pass builder `0x140CEC650` (per world-renderer slot) names its passes; the foreground ones are
  `worldFgZPass` (`0x140CEE4AA`, gated by WorldRenderSettings +0x12E ForegroundEnable and +0x130
  ForegroundZPassEnable), `worldFgGeometryPass` (`0x140CEEBF5`), `worldFgTransparent`
  (`0x140CEF051`), `worldFgOpaqueEmissive` (`0x140CEF272`). Main passes take the view handle at
  `[handles+0x10]`, foreground passes `[handles+0x20]`.
- Handles come from `0x140CDEE20(worldRenderer, slot, handles)`: for each of 3 contexts it calls
  `0x140C265A0(ctx, slot+0x186A0, slot+0x19060, slot+0xFF0)` with the **same** RenderViews, then only
  a different depth-stencil state (`0x140C263F0`: main `FFF00201/02000006`, foreground
  `FFFF2001/02000007`). So the pass-level view and projection are the world's; the separate 45°
  projection exists only in the foreground per-view constant block, written elsewhere.
- Per-view descriptor (0x1560 bytes, built by `0x1408CAF70` on the render thread, copied by
  `0x140CD6110` into slot +0x5E0): +0x50 view 0, +0x530 previous view 0, +0xA10 params +0x4E0,
  +0xEF0 a fourth RenderView (only the shadow cascades `0x140CD81A0` use it, `0x140CE0E50`),
  +0x13E0..+0x14FF = params +0xED0..+0xFEF, +0x1510 = 1 − params +0xFF4, +0x1520/+0x1524/+0x1528 =
  params +0x1008/+0x100C/+0x1010. No code reads +0x13E0 (or slot +0x19C0) at a fixed offset except
  copies; the foreground block's builder reads it through some other base (not found).
- Slot views: +0x630 = view 0 and +0xB10 = previous view, copied to +0x186A0/+0x19060 with a
  per-frame sub-pixel offset (±0.25 px into RenderView +0x90, likely TAA) and rebuilt (`0x140CF1D3C`). The world per-view constants read view 0's FOV at
  `0x140CE9587` (`[slot+0x6E4]`) and its projection at `0x140CE971C`.
- The π/4 load at `0x140C7EC26` (in `0x140C7E980`, which reads `+0x19A0..+0x1A10` of its object) is a
  90° view of an unrelated object (vtable near `0x141DE87E0`, viewport-built projection at +0x1944,
  π/4 passed to vfunc +0xC8): not the first-person pass. The symbols table's "first-person slot
  transforms" readers were displacement-scan matches of that object.

RenderView methods found on the way: `setFovY` `0x140B89EE0` (stores +0xB4, horizontal FOV
+0x250 = 2·atan(tan(fov/2)·aspect), dirty |= 0xF), `setAspect` `0x140B89D60`, `setNear` (+0xBC)
`0x140B89F40`, `setFar` (+0xC0) `0x140B89EC0`, `setType` (+0xB0) `0x140B89FD0`, ortho size
`0x140B89FB0`/`0x140B89F90`, `setTransform` `0x140B8A0A0`; projection `0x140B7EAE0` (sin/cos of
fov/2) is called only from `0x140B7EA10` (RenderView update `0x140B8CFC0`) and `0x140B7FBF0`
(view-projection with another far plane, `0x140CAD383`). Callers of `setFovY`: world camera
`0x1408AE417`/`0x1408AE458`, ForceFov `0x140848CBF`, secondary streaming view `0x1408A92A8`,
`0x1408283D3`, camera entity `0x140FA8401`/`0x140FA841F` (degrees, clamped to [0.0002, π]), shadows
`0x140CD8303`, `0x140CD8EC9`, `0x140CD93FC`, `0x140CE1833`. None builds a foreground view.

### Live test recipe (for the session that owns the game)

Pointers (all reads first; `peek` prints little-endian hex):

```
params   = [[0x142672378] + 0x60]           world FOV: params+0xB4 (rad), fp shadow FOV: params+0x100C
mgr      = [0x142673D80]                    count [mgr+0x10]
view0    = [[mgr + 8]]                      fp FOV candidate: view0+0x13D0 (float degrees)
camera   = [view0 + 0x1D0]                  camera FOV: camera+0x100 (degrees)
level    = [[0x142670D80 + 0x28] + 0x38]    DefaultFOV level+0xB0, InfantryFOVMultiplier level+0xB4
```

1. **Read.** `peek view0+0x13D0 4` → expect `00003442` (45.0). `peek params+0x100C 4` → 0.7854
   (k = 0) or 0.8029 (k = 1), −1 (`000080bf`) when the camera is not first person. Check
   level+0xB0 × level+0xB4 = view0+0x13D0, and camera+0x100 = world FOV in degrees.
2. **Writer.** `watch <view0+0x13D0> 4 5000` while playing: expect no hits (written only at level
   setup by `0x1408B08C7`). A checkpoint reload during the watch should show `0x1408B08C7` ←
   `0x1408203DA`.
3. **Decisive write (mono, `fakehead off`).** `poke <view0+0x13D0> 0000b442` (90.0), then
   `shot`. If candidate 1 drives the foreground: gun and arms shrink toward the screen centre to
   tan(22.5°)/tan(45°) = **0.414** of their size, world unchanged (world FOV comes from the camera).
   Restore with `poke <view0+0x13D0> 00003442`. No change → the value is not read per frame; next
   time reload the checkpoint with the poke in place (it may be latched at spawn), else candidate 1
   is out. Side effects while poked: the first-person shadow frustum and Ant's FOV ratio change.
4. **Formula check (independent of the source, mono).** Shots A: `fov 0`, `gunscale 1`; B:
   `fov 1.6`; C: `fov 1.6`, `gunscale 0.4023`. Expect B's gun = A's gun (known), and C = A shrunk
   uniformly by 0.402 about the centre: the gun's bounding box and a world corner's distance from the
   centre shrink by the same factor. Then `fov 0`, `gunscale 1`.
5. **Aiming (candidate 2).** With `fov 1.6` holding the world FOV, compare the foreground block's
   projection (`eyeprobe2` dump, block +0x70: y scale 2.414 at the hip) while aiming down sights. A
   different y scale = cot(RenderFov/2) means `WeaponZoomLevelData.RenderFov` replaces the 45° in ADS,
   and auto gun_scale must use that value while zoomed.

If 3 or 4 confirms, gun_scale can be automatic: g = tan(fg/2)/tan(+0xB4/2) per frame in the camera
hook, with fg = view0+0x13D0 (or the RenderFov while zoomed).

## Gun life size (2026-11-02)

Static analysis (section above): the foreground is drawn at a **fixed 45 deg
vertical FOV** with the world's aspect (projection y scale 2.414 = cot 22.5 deg, measured in
stereo-rendering.md). In the headset the world uses the rendered FOV (~96 deg vertical), so the gun and
arms were magnified about the view centre by cot(22.5)/cot(half fovY), ~2.7x, and sat off the
controllers. `gun_life_size` (default on) scales the first-person right/up rows by
tan(22.5 deg)/tan(fovY/2) every head-tracked frame, on top of `gun_scale`; pipe `gunscale` prints the
applied value.

## The gun pass FOV in ADS (2026-11-04, live)

The per-view constant block of the first-person camera (`constant_patch::shift_per_view_block`) holds
the gun pass's projection: y scale cot(fov/2). Hip 2.414 (45 deg), **ADS 6.69 (17 deg)** on the AK-12
red dot (`WeaponZoomLevelData.RenderFov`, candidate 2 above, by its effect). Other per-view blocks of
the same layout show 45 (the first-person shadow view, also in ADS) and the world's FOV, so the gun's
is the narrowest of a frame. `stereo/eye_pass` publishes it per second-eye pass; `camera_hook` scales
the gun by tan(that/2) / tan(world/2), so the gun stays life size while the game aims down the sights.

## Who sets a RenderView's FOV (2026-11-04, REA/Ghidra, dump only)

Every direct caller of setFovY `0x140B89EE0` in the full analysis: `0x140828090`, View setup
`0x140848B00`, `0x1408A9160`, World FOV from camera `0x1408AE170`, the renderer's `0x140CD81A0`,
`0x140CD8BF0`, `0x140CD9060`, `0x140CE0010`, and `0x140FA8270` (indirect calls are not covered).

`0x140FA8270` is the soldier-side one: slot 9 of an unnamed vtable `0x141E61980` (no RTTI; built at
`0x140F86F41` / `0x140F8854B`), which tail-calls the base `0x140848A00` (next to View setup). Per call
`(…, view, camera, …, active, worldView, fgView, …, dirty*)`:

- If the camera's flag `+0xF0` bit 8 is set and its `+0x11C` (degrees) is not −1, both RenderViews get
  setFovY(`+0x11C`·π/180, clamped 0.0002..π), and both take the override lists at player view `+0x13E0`
  / `+0x1470` (`0x140B8A0A0`).
- When the game's own stereo 3D is on (`[[0x142738080]+0x60](0)+0x28`) it sets RenderView `+0xD4`
  (convergence) to Twinkle video `+0x50` × 0.05, dirty |= 0xB.

Candidate for the ADS world zoom (`ZoomLevelData.FieldOfViewSP` reaching the camera's `+0x11C`):
**ruled out live** (2026-11-04): the active camera (`VeniceFPSCamera`, vtable `0x141BC94F0`) keeps `+0x11C`
0, `+0xF0` 0 and `+0x100` -1 at the hip, in red-dot and magnifier ADS. The zoom itself is readable where
the mod overrides it: view params `+0xB4` before the camera hook (45 / 40 / 27 deg; re/notes/weapon.md
"Magnified sights"). The mod's `copy_detour` sets the headset FOV there, which is why the world never zooms.

## Live checks for the structural life size (2026-11-09, monitor, #39)

- **Candidate 1 confirmed: per-player view +0x13D0 drives the gun pass at the hip.** View 0 =
  `[[[0x142673D80]+8]]` (+0x13D0 = 45.0). Poking 60 gave a gun pass of 60 deg (projection y scale in the
  foreground per-view block, pipe `gunscale` gun_fov_deg); 70 gave 70; from 80 the frame's narrowest
  projection is the world's (78.35 here), so the gun pass is wider still. The gun and arms are drawn
  smaller with an orthonormal first-person camera (with `gun_life_size 0`).
- **ADS overrides it:** while the game aims, the gun pass is 17 deg on the AK-12 whatever +0x13D0 holds.
  Not from the weapon data: SoldierWeaponData +0x138 ZoomLevels (2: RenderFov 55, 18) poked to 70 changed
  nothing; none of the 42 WeaponModifierData (+0x140, 24 bytes: Guid, Modifiers array) modifiers holds 17
  at +0x18 (WeaponZoomModifier.ZoomRenderFov). Memory diff scans for 17.0 and 0.2967 (radians) while
  aiming vs at the hip found no runtime field (only world positions and the scan's own buffer). The ADS
  value is likely built into the projection from a transition (FOVTransitionData) on the fly.
- **Why it matters:** `gun_life_size` scales the first-person rows instead; the lens and reticle shader
  uses those rows, so the red dot vanishes and the lens samples the wrong place (#39). With
  `gun_life_size 0` the game's ADS draws both correctly.
- **Next:** REA on the foreground per-view block writer (the code that fills the gun pass's projection,
  y scale cot(fov/2)): find where it reads the zoom FOV in ADS, then either set that input or override the
  projection there, so the gun pass gets the life-size FOV in both hip and ADS with an orthonormal camera.

## The gun pass FOV, found (2026-11-10, monitor, #39)

- **Readers of view 0 +0x13D0** (pipe `watch <addr> 4 1500 rw`): `0x1408A938D` (view params +0x100C, the
  shadow minimum FOV), `0x1408AB18F` (the FOV ratio to Ant, ~once per frame) and the getter `0x1408AB0E0`
  called ~3 times a frame from **`0x140FB8290`**, the gun pass FOV:
  - `this` = the client soldier weapon; its runtime zoom levels are an array of `WeaponZoomLevelData*` at
    **weapon +0x47D0 .. +0x47D8** (not SoldierWeaponData +0x138: the optic's copy, 55 / 17 / 18 on the AK-12
    red dot).
  - Fewer than two levels: the hip value (+0x13D0). Else the zoom state `[weapon+0x4988]` gives the current
    and previous level (+0x1E8 / +0x1F0) and a transition factor (`0x140F94280`, shaped by the level's
    `RenderFovTransition` at +0x18 via `0x14093CA00`); level 0 means the hip value, others their
    **RenderFov (+0x10)**; result = lerp(previous, current, t).
- **Proof:** +0x13D0 = 70 and the runtime levels 1.. = 70: the gun pass stayed 70 deg at the hip and in ADS,
  the red dot and its ring drew in a clear lens (the game's own), with an orthonormal first-person camera.
- **Implementation** (`stereo/gun_pass_fov`, setting `gun_fov_life_size`, default on): the camera hook writes
  the life-size FOV, 2 atan(tan(world half) / gun_scale), into both every frame (the game's values kept and
  put back when it is turned off) and no longer scales the first-person rows. `eye_pass::foreground_block`
  also recognises the gun pass by its soldier-space camera (|position| < 4 m), since at gun_scale 1 its
  projection now equals the world's. Monitor: hip 73.06 deg (gun_scale 1.1), applied row scale 1.000; gun
  and hands at the same size and place as with the squeeze; ADS red dot visible (with the squeeze: none);
  off restores 45 / 17.
