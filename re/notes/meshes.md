# Extra meshes: copies of the game's own draws (#36), BF4 build 179547

Status: **second magazine works on the monitor (2026-11-05, Release, AK-12).** A held magazine and the
one in the gun are both the game's own mesh, lit and depth-tested with the scene. The held grenade is
not done yet (see the end). Work order: `docs/tasks/extra-meshes.md`.

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

## The held grenade (not done)

The game draws no grenade while the rifle is out, so there is nothing to re-issue then. A copy needs the
grenade's draws captured whole while it is drawn (the throw is the first moment): index and vertex
buffers, input layout, VS/PS, every constant block (per-draw ones copied out of the arenas, which are
reused each frame; per-view ones taken from the current draw at replay), shader resources and samplers,
states, for each pass (pre-pass, G-buffer, the later pass), then replayed after a first-person draw of
the same pass with the palette moved to the hand.
