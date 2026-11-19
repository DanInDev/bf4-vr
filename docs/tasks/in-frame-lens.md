# Task brief: sights drawn in the game's frame (#39)

Written 2026-11-13. Revised the same day after step 0. Branch `fix/sights-in-frame`, from `fix/sights-headset`
(not `main`: the sights work is there). Read `CLAUDE.md`, `docs/SAFETY.md`, `docs/tasks/sights-ads.md`
("Direction from 2026-11-12") and `re/notes/weapon.md` ("The zoom mesh at the hip", "In-frame lens, step 0").

## The idea

The BF3 single-player VR mod (its code was shared on 2026-11-13; its maintainer allows copying it, owner 2026-11-16) makes sights
work by drawing them **in the game's frame, on the optic's own glass**, never as OpenXR quads:
- the lens keeps the game's vertex shader and depth test;
- per pixel, the ray's slope off the sight's axis, divided by the magnification, picks what the glass shows;
- reticle marks are placed by the same slope (collimated);
- there is a field stop past the eyepiece's view;
- a real second render feeds magnified scopes.

The headset failures of our quads (dot not seen, scope picture 11-20 degrees off in the right eye, white wedge, eye
gates) all come from the compositor placing layers apart from the game frame. In-frame drawing also shows up in
`eyeshot`, marks and VR View, so the monitor can test it.

## Where we already are (`fix/sights-headset`, built on the owner's "one solution for every optic" rule)

- **Clear lens and the optic's own reticle without the game's aim:** `weapon/zoom_mesh` (`sight_zoom_mesh`) sets
  `ZoomMeshTransitionFactor` to 0 while a sight is at an eye. That is data, so it covers every gun.
- **The reticle on the sight line, in-frame:** `sight_reticle` re-anchors the game's reticle quad (6 indices,
  stencil 2) through the per-view block's inverse screen size.
  - Step 0 confirmed it collimated on the SCAR-H: 1.4 cm off-axis the ring stays on the wall while the window slides.
  - Known flaws: the picture scales by about 13 % at 8 degrees (the uncommitted `sight_reticle_fit` in the `sights`
    worktree is about this), and the mask reaches a little past the glass.

So for red dots and holos the game's own optics, kept on the sight line, **are** the in-frame lens. No shader of
ours is needed for them.

## Plan from here

State 2026-11-13 evening: step 1 (second-eye lookups keyed by arena discards) built, the stray not reproduced
in this scene; step 2 done on the monitor (the AK-12's lens clear and collimated without the game's aim; the
magnifier is hidden by the zoom mesh and not magnified); step 3 built (`scope_in_frame`): the picture drawn
into each eye's finished image at the lens, verified in both eyes on the monitor. Details in
`re/notes/weapon.md` "In-frame lens, step 0" and "Steps 1 and 2".

1. **Fix the second eye's stray first-person draws** (step 0 finding, `re/notes/weapon.md`).
   - The right eye draws the gun and optic differently from frame to frame: a second housing about 165 px to the right,
     an unlit gun body. The zoom mesh and reticle are not involved.
   - Lead: constant arenas are discarded about twice during the first eye's render, but the first eye's blocks are read
     back from a copy queued before it.
   - Prove it: count first-eye bindings made after their arena's discard.
   - Fix: copy those blocks on the GPU at bind time, or key `patched_blocks` by arena generation.
   - Check: 6 `eyeshot2` captures as steady as `eyeshot`, mean difference <= 0.2 (method in the note).
   - This comes first. Every right-eye sight result is unreliable until it's fixed.
2. **AK-12 and the magnifier.** This needs a checkpoint that carries the AK-12 (the saved one has only the SCAR-H).
   - Does `sight_zoom_mesh` clear the black lens?
   - Is its reticle the same 6-index quad?
   - What is the magnifier's lens draw and stencil?
3. **The magnifier picture in-frame (the BF3 idea):**
   - Draw the scope render (`stereo/scope`, previous frame's picture) inside the zoom mesh's lens mask (stencil 2),
     sampled by slope: the ray from the rendered eye, slope off the sight axis divided by the magnification,
     plus a field stop.
   - Then the scope quad goes, and the right eye cannot be 11-20 degrees off any more.
   - Check: debug rings by slope at 1.73x their hip size in `eyeshot` and `eyeshot2`, lined up with the world at 1x,
     eye 0-3 cm off and head pitch 0-10 degrees.
4. **Reticle placement by slope** instead of the inverse-size anchor, if the 13 % scale or the mask overreach
   matters in the headset.
5. **Retire** `sight_dot` quads, the scope quad and the eye gates once the headset confirms 1-3.
   `hip_accuracy` stays.

Every new behaviour gets a live setting, a panel row and an ini line. Engine-free math (slope, field stop, picture
mapping) gets a unit test.

## Great Destroyer's scopes, read in full (2026-11-19)

Read from `criminalvictory/Battlefield-3-VR-The-Great-Destroyer` at `85d59d2` (2026-11-19): `src/scopezoom.cpp`,
`src/scopeview.cpp`, `src/scopemarks.h`, `src/skin.cpp` (`ScopeLens`, `DrawArms`, the held sights around line 10424).
Their scopes work in the headset ("That looks amazing"); after the owner's 2026-11-19 marks (black scopes, a broken
magnifier) ours do not. What they do, and where we differ:

| | Great Destroyer | Ours (`main` 93cf9d9) |
| --- | --- | --- |
| Where the picture goes | **On the game's own glass draw**, in the lit HDR scene pass (`InScene`: target R16G16B16A16_FLOAT, before the HUD). Only the pixel shader is swapped (`scopezoom::Begin` / `End`); the glass's vertex shader, depth test and stencil stay, so the rim covers it and each eye gets its own | `scope_in_frame`: a flat textured quad at the lens pose, drawn into each eye's finished image before the reticle quad, with its stencil. No depth: "the lens blocks the view" (marks 42, 48, 69) |
| What a glass pixel shows | Per pixel: the ray from the draw's **centre of projection** (third column of the inverse view-projection, not the tracked eye: "a few millimetres at the lens is a good part of what a pixel looks at"), its slope off the scope's axis divided by the power, then that direction looked up in the source | A picture composed once (`scope_compose`: round, dot baked in) and stretched over the quad |
| Source picture | Two, picked per frame: **(a)** a copy of the eye's own scene target at the glass draw, the region round where the scope's line lands (no extra render, so never black, only soft); **(b)** their second world render (720x720), when it was drawn this frame | Only the third render (`render_scope`), and it runs at the hip only for the magnifier, a name-table optic or a zoom learned in the game's ADS; otherwise the hip lens stays opaque: "pure black" (mark 21, USAS-12) |
| Power | Per sight, from their table | Magnifier constant, name table or learned. **Generic source found:** `WeaponZoomLevelData` +0x10 `RenderFov`, and the optic's `WeaponZoomModifier` +0x18 `ZoomRenderFov` (typeinfo); power = tan(hip FOV / 2) / tan(zoom FOV / 2) |
| Field stop | Black past slope 0.30-0.38 / power off the line (an eye off-axis sees the tube go dark, as a real scope); glass edge darkened to 25 % from 82 % of its radius. Not for reflex sights | None |
| Reticle | The sight's marks (extracted offline into `scopemarks.h` from the game's mark sheet), placed by the same slope, so collimated; the game's own reticle draw skipped while theirs shows | `sight_lens`: the game's reticle quad with our pixel shader, already by slope, but its span assumes a 40 degree ADS FOV (`sight_lens_fov`) for every optic |
| Sharpness | FSR 1 EASU (MIT, AMD) on the source, plus a light sharpen, both in "squeezed" HDR (c / (1 + max)) so the sun leaves no dark ring; "way too blurry" before | Bilinear |
| Second render | A second copy of the game's world renderer: own settings object (no first-person, motion blur, FXAA, screen effects, lens flares, planar reflections; 1 sample; spot-light shadows off), **terrain and particles borrowed from the first eye** (own terrain cost 4.5 ms for good; own particles came out as squares), FOV fitted to the lens plus room for one frame of recoil, its level of detail followed slowly (no pumping), near plane 0.1, left out when the frame's memory would pass 85 % | `eye_pass` `render_scope`: a third render of the world (owner: "lower third a texture bug" in the magnifier, marks 42, 48, 69) |
| Finding the glass | Per sight, by draw key (index count, start, base) from a table (`kHeldSights`), mined with `drawscan` / `glassdump`; their glass draws carry **stencil reference 32** | The zoom mesh's lens mask: the 6-index quad with stencil reference 2 (`reticle_draw`), generic for every optic seen so far |
| Near plane | 1 cm for world and gun ("no clipping with the eye pressed to the sight") | The game's |

Their per-sight tables break the owner's "one solution for every optic" rule. The rest carries over to BF4 without them,
because our zoom-mesh lens mask and the zoom level data already give the glass and the power for any gun.

### Plan from here (replaces steps 3-4 above)

"Scope" here means every optic (owner, 2026-11-19), each kind checked at every step: **1x reflex sights** (red dots,
holos), **magnified scopes** (real zoom, sniper scopes) and **flip magnifiers** behind a reflex sight (1x flipped out,
magnified flipped in, switching live). One glass shader for all: power 1 and no field stop for reflex sights. Issues
#137-#140 (S1-S4).

1. **The glass shader on the lens mask.** Swap `scope_in_frame`'s quad for a full-lens draw with GD's pixel maths:
   ray from the draw's centre of projection, slope / power, field stop, edge darkening. Source (b) when the scope
   render drew this frame, else source (a), a copy of the eye's HDR scene at that draw. So a scope is never black.
   Engine-free maths (slope, field stop, source uv) in `stereo/lens_math` with unit tests.
   **Built (#139, 2026-11-20)**, changed by what the monitor showed: source (a) is a dead end (the HDR scene always
   holds the first-person gun, lit with the world), so the picture is always the scope render (b); the lens quads only
   mark the glass (a mask, kept to the quads the sight line passes through) and the picture is composed into it after
   the eye render; scopes and magnifiers no longer start the game's aim (its flat sniper overlay). The SRS's quads
   lie off its line: it keeps the flat picture (open). `re/notes/weapon.md` "The picture in the optic's own glass".
2. **Power from data**, for every optic: read `RenderFov` / `ZoomRenderFov` for the selected zoom level and drop the
   learning and the name table to a fallback. The scope render and the glass then run for any optic above 1.25x.
3. **The reticle at the optic's own FOV**: `sight_lens` spans by that zoom level's FOV instead of the 40 degree constant.
4. **Probe the BF4 scopes first** (none is checked yet): with a 4x and a sniper scope at the eye, `reticle dump` the
   stencil-tested draws. Is there a stencil-2 lens quad, and is the black disc the hip mesh?
5. **Quality, after it works in the headset:** EASU on source (a), the render's FOV fitted with recoil room, its
   settings stripped as GD's.

Copying is allowed (maintainer's OK, CLAUDE.md): credit `scopezoom.cpp` / `scopeview.cpp` in the code comment, and
AMD's MIT notice with any EASU code.

## Headset check after steps 1-3 (VR View capture and a mark at each)

1. SCAR-H holo at the right eye, then the left: the reticle stays in the window on the target as the head turns.
   There is no second sight beside the gun.
2. AK-12 at the hip, sight to the eye: the lens is clear, with its own dot.
3. Magnifier flipped in: the world 1.7x inside the glass, in both eyes' positions, with no offset or white wedge.
4. Move the eye around behind each sight: the reticle stays on the target, and the scope darkens from the edge.
5. Shots through each sight land on the reticle.
