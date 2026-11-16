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

## Headset check after steps 1-3 (VR View capture and a mark at each)

1. SCAR-H holo at the right eye, then the left: the reticle stays in the window on the target as the head turns.
   There is no second sight beside the gun.
2. AK-12 at the hip, sight to the eye: the lens is clear, with its own dot.
3. Magnifier flipped in: the world 1.7x inside the glass, in both eyes' positions, with no offset or white wedge.
4. Move the eye around behind each sight: the reticle stays on the target, and the scope darkens from the edge.
5. Shots through each sight land on the reticle.
