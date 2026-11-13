# Extra meshes: copies of the game's own draws (#36), BF4 build 179547

Status: **second magazine works on the monitor (2026-11-05, Release, AK-12).** A held magazine and the
one in the gun are both the game's own mesh, lit and depth-tested with the scene. **The held grenade works
on the monitor too (2026-11-09, M67, see the end).** Work order: `docs/tasks/extra-meshes.md`.

## The rig has one magazine

- First-person magazine bones: `Wep_Mag` 131 (child of `Wep_Root` 126), with `Wep_Mag_Ammo` 132 and
  `Wep_Mag_Extra1` 133 under it. Nothing is skinned to `Wep_Mag_Extra1` on the AK-12 or SCAR-H (moving it
  29 cm, pipe `bone 133 0 0.15 0.25`, changes nothing on screen; the same move on `Wep_Mag` lifts the
  magazine into view). The game's own AK-12 reload moves only `Wep_Mag` (down ~0.5 m and back: one mesh
  plays old and new).

## The magazine is its own small mesh (pipe meshprobe, 2026-11-05)

`meshprobe <file>` records every indexed draw of one first-eye render (928 in the checkpoint room): index
and vertex buffers, shaders, render target, VS constant bindings, and for VS slot 0 = 3072 bytes the 64
3x4 matrices. Two probes, the second with `bone 131 0.1 0 0`, diffed:

| Finding | Evidence |
| --- | --- |
| Most 3072-byte VS slot-0 blocks are not bone palettes but world-space instance matrices (translations ~870 m, the soldier's world position) | `meshlist`: tmax 870 |
| First-person palettes are in the **model-pose space** (Animatable +0x50: soldier space, root left out): translations up to ~2.5 m | entry 0 of the magazine = model pose of `Wep_Mag` times a constant bind offset |
| The **magazine is a separate mesh**: one index buffer, 1620 indices (540 triangles), a 2-entry palette: entry 0 `Wep_Mag`, entry 1 `Wep_Mag_Ammo` (both moved exactly 0.100 m with the bone) | draws 32 (pre-pass, stride 16), 49 + 50 (G-buffer, stride 40, indices 0-960 and 960-1620), 901/906 (a later pass, R16G16 target) |
| Palette entry = model-pose transform of the bone times its inverse bind: with `Wep_Mag` at (0.4468, 1.8589, 0.8771), identity rotation, entry 0 was identity with translation (0.4468, 1.9516, 0.7705): a bind offset of (0, +0.093, -0.107) | `magpose.py`, 3 reads |
| Re-issuing the game's draw right after it, with VS slot 0 bound to our copy of the palette (entries 0-1 moved 30 cm up), draws a second magazine, lit and shaded like the first, in both eyes | pipe `meshcopy <ib> 0 0.3 0 2`, eyeshot |

## How the second magazine is drawn

- `body/arm_pose` sets `Wep_Mag`'s scale to **1.002** every evaluation (`extra_mesh::magazine_tag`;
  invisible). Scale reaches the palette (rows are rotation * scale; hiding the legs with scale 0.001 works
  the same way), so the magazine's entries are the ones whose rows are 1.002 long, on any gun, with no
  table of meshes. Children inherit it (`Wep_Mag_Ammo`).
- `weapon/reload_hands` says when a second magazine is visible (`hand_result::extra`): the old one seated
  while a new one is held; the old one in the fist once the new one is seated (palming) or while it is
  caught (drawn untilted); one falling (AK flick) while the rig's mesh is in the hand.
- `arm_pose` publishes D = extra * drawn^-1 in palette space (both with scale 1), 3x4 rows.
- `stereo/extra_mesh` (render thread, eye_pass's draw hook): after a draw with a 3072-byte VS slot-0 block,
  reads the palette through the arena's CPU mapping, and if any entry carries the tag, draws it once more
  with our palette: tagged entries D * S, all others zero (collapsed: nothing else of that mesh is drawn).
  Meshes without the tag are remembered by (index buffer, start, count) and not read again for 4 s. The
  game's VS slot-0 binding is restored after the copy. It runs in every per-view render (both eyes, scope).
- Cost: ~3 tagged meshes x 3 KB read per render; fps 127.5 without vs 128 with the copy (Release,
  2160², fake head).

## The held grenade (2026-11-09, monitor, Release, M67)

Status: **works on the monitor**: the game's own M67 drawn in either fist, lit and depth-tested, its pin
ring gone once pulled; no measurable cost (124 fps with and without). Headset row GRN-4.

### When the game draws its grenade

| Situation | Slot | Grenade drawn |
| --- | --- | --- |
| Physical throw (pad LB quick throw, our launch rewrite) | 6 for ~0.3 s | **no**: the rifle stays drawn the whole time |
| Grenade key (5) | 6 | yes, in the right hand (the game's idle hold) |

So the capture selects the grenade itself: `weapon/weapon_swap` presses 5, waits for slot 6, waits for an
accepted capture, presses the gun's key (1 / 2). Round trip 0.6 s (pipe `grenade capture`; automatic on the
first grenade taken with `grenade_mesh` on, again for another kind of grenade).

### Its draws (pipe meshprobe with the grenade out, 2026-11-09)

- One mesh, one index buffer, 7065 indices, a two-entry palette: **entry 0 the pin and ring, entry 1 the
  body and spoon** (`extramesh pin 1` / `2` hid each). Moving `Wep_Root` (`bone 126`) moves both: they hang
  from it.
- Three palette draws per view, **the same shaders as the AK-12's magazine** in each: pre-pass (4 targets,
  stride 16, blend A), G-buffer (4 targets, stride 40, blend B), a later R16G16 pass (1 target). A fourth
  draw of the same indices in the R16G16 pass uses a 368-constant VS block (not a palette; not copied).
- Constant blocks: VS 0 palette; VS 1 (16 constants) per pass, shared by every draw; VS 2 (32) per object
  (the grenade's holds its world transform, like the gun's), **bound to PS 2 too**; PS 0 / PS 1 in the
  G-buffer: 16 constants each, per draw (material values and matrices: they differ even between parts of
  the same gun); in the other passes PS 0 is a big per-view block (800 constants, in another buffer).
- Textures: G-buffer PS slots 1-5 are the grenade's (the material); the rest equal the other draws'.

### How it is drawn

- Tag: while the capture has slot 6 selected, `arm_pose` puts `Wep_Root` at a fixed point of the palette
  space (0, 1, 0), unrotated, with scale **0.01**: the game's grenade is invisible and its entries are the
  ones whose rows are 0.01 long **and** that sit within 2 cm of that point (the scale alone also matched
  far-away instance matrices). Meanwhile `Wep_Mag` gets scale 1.5 instead of the magazine tag (0.015
  under the root): a capturing render that draws an entry with that mark still has the gun on screen
  and is thrown away (2 rejections before each good capture on the monitor). Headset 2026-11-09 showed
  why: the older check (meshes seen with the magazine tag) knew no magazine in a fresh session, and the
  capture kept 16 rifle and gear draws. Only meshes drawn in more than one pass are kept (the grenade
  is in three; the gear and a stray 36-index draw were in one).
- Capture (render thread, first eye): each tagged draw outside the shadow passes: index and vertex
  buffers, layout, topology, VS/PS, rasterizer state, VS/PS views, PS blocks 0-5 (contents read through
  the arena mapping), and the palette relative to the hidden root ([R / 0.01 | (t - root) / 0.01]). At
  the end of the render, a block or view the grenade shares with another skinned draw of the same pass,
  or (blocks) with its own VS bindings, is left to the draw it will follow; the rest is its own (G-buffer:
  PS 0 and 1, views 1-5).
- Replay: while `physical_grenade` holds one, `arm_pose` publishes `Wep_Root` = holding fist (fingers::grasp)
  times the game's hold measured in the right fist (pipe `extramesh` game_hold_in_fist: 1.5 cm from the
  fist centre, 44 degrees about its y axis), palette space. After each magazine draw (tagged, `magazine_tag`)
  the captured draw of the same pass (targets, first target's format, blend state) is issued once per
  view with its own state and the magazine draw's per-view and per-object state (the second eye's
  redirected blocks with it); the game's state is restored after.
- The primary on the sling (#58, 2026-11-13): the same capture as a third item (`captured::primary`), taken
  a second after the primary is first in hand (`weapon/sling_primary`), with `Wep_Mag` at scale 1 for that
  render (the magazine is part of the item, so no gun mark and no "seen with the gun" rejection). Up to 48
  draws per item (was 16; the replay mask is 64-bit). At most 3 capturing renders per gun (each hides it
  for one frame); a different gun in slot 0 drops the capture (`forget_item`). Drawn after the magazine
  draws of the gun in hand (the pistol's), at the let-go gun's sling pose; not while the knife is out
  (no magazine draw to follow).
- Needs a gun drawn (its magazine is the anchor); with none (knife) the theater's flat quad stands in, as
  it does until the first capture. Both eyes checked on the monitor (eye-2 screenshots are sometimes a
  frame behind: a hand or the grenade a few hundred pixels off in a single shot, not in the next).
