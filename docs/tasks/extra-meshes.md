# Extra meshes: a second magazine and a held grenade

Work order: GitHub issue #36 (progress and headset results go there).

**Status (2026-11-05):** the second magazine is done on the monitor (approach A, simpler than planned: the
magazine is its own small mesh, found by a scale tag on `Wep_Mag`; `re/notes/meshes.md`). Open: the held
grenade (needs a full draw capture at the throw, see the end of meshes.md) and the headset check (GUN-13).

Goal: draw game-quality copies of the gun's magazine (a fresh one in the off hand while the old one is still
in the gun, the caught one while palming) and of the grenade (held before the throw, while the rifle is out),
lit and depth-tested like the rest of the scene. A flat overlay quad (the held grenade today) is not enough.

## What is known (2026-11-05)

- The first-person rig draws **one** magazine: `Wep_Mag` 131. Its children `Wep_Mag_Ammo` 132 and
  `Wep_Mag_Extra1` 133 exist, but nothing is skinned to `Wep_Mag_Extra1` on the AK-12 or SCAR-H, and the
  game's own reload reuses the one `Wep_Mag` for old and new (re/notes/skeleton.md "One magazine mesh").
  So no second magazine can come from the rig; physical_reload moves the one mesh to the hand.
- The held grenade: the game draws nothing until the throw (ui/held_grenade: a compositor quad).
- **Overlord** (MW2 Campaign Remastered, not Frostbite; GPLv3: design only) spawns no entities for these.
  It copies face subsets of the native magazine/receiver and grenade viewmodel meshes into private
  immutable buffers once, then submits them through the engine's own scene path each frame, placed from the
  solved hand pose; the native inserted magazine is hidden only while the copy is available. Grenades are
  vertex subsets in model bind space with the same model-to-body transform; the thrown grenade is the game's
  own projectile. Docs: `docs/vr-ammunition-presentation.md`, `docs/vr-grenades.md`,
  `docs/vr-falling-reload-items.md` in its repo.
- Frame structure (re/notes/stereo-rendering.md): one render thread; G-buffer (4x MSAA, 4 targets + D24S8),
  shadows, tiled lighting, post, HUD. A copy drawn **into the G-buffer pass** gets the game's lighting for
  free; skipping the shadow pass is acceptable.
- pipe `frametrace` (2026-11-05) shows no indexed draws because **`stereo/eye_pass` owns those hooks**
  (log: `frametrace: slot 12 / 20 already hooked; not traced`), not because the game hides them. eye_pass
  already sees every `DrawIndexed`/`DrawIndexedInstanced` on the immediate context and every constant
  binding: each draw binds 256-byte blocks of two 1 MB dynamic arenas (`VSSetConstantBuffers1` /
  `PSSetConstantBuffers1` with offsets) that job threads filled through a persistent NO_OVERWRITE map, and
  for the second eye it already redirects blocks to patched copies in its own buffer (`stereo/constant_patch`).
  Its `eyeprobe` records bindings and draws. That is the machinery approach A needs: the weapon's bone
  palette is most likely in such a block.

## Approaches

| | How | For | Against |
| --- | --- | --- | --- |
| **A. Copy the game's draw** (Overlord's idea at the D3D level) | Find the first-person weapon's G-buffer draw (its index/vertex buffers, VS/PS, input layout, bone palette); once, copy the index subset of triangles skinned to `Wep_Mag` (vertex blend indices read back from a staging copy) into our own index buffer; each frame issue one more draw right after the game's, with the bone palette entry for `Wep_Mag` replaced by the second magazine's pose (an arena-block copy, as eye_pass does for eye 2). Grenade: capture the grenade viewmodel's draw while it is out, copy its buffers, redraw later with our transform | Real mesh, real shading and depth, engine-agnostic D3D work; the copy reuses the game's shaders and constants; eye_pass already hooks the draws and redirects constant blocks | Needs the skinning data layout (constant block or structured buffer of 3x4 matrices); per-frame constants must be the current frame's; the grenade's buffers must stay resident after it is put away |
| B. Engine-side submission | Hand Frostbite a second mesh instance (a mesh render entity / skinned mesh with the weapon's MeshAsset) through its render scene | Engine does everything, incl. shadows | Deep RE of entity creation and the render scene; asset lifetime |
| C. Own renderer | Draw a magazine model of our own after the G-buffer resolve with the scene depth | Fully ours | Needs a model and our own lighting; never matches the game |

Recommended: **A**, falling back to B if the draw cannot be found or copied.

## First session (monitor, campaign checkpoint)

1. Read `stereo/eye_pass.cpp` and `stereo/constant_patch.cpp` (arenas, block redirection, `eyeprobe`), and
   let frametrace record through eye_pass's detours (or extend `eyeprobe` with IA buffers, shaders and
   render targets per draw).
2. Find the weapon draw: offset `Wep_Root` far away (pipe `bone 126 …`) and diff which G-buffer draws
   change, or match the draw whose VS constant block contains the `Wep_Mag` matrix (we know its pose).
3. Record its state: IA buffers/format/layout, VS/PS, VS constant buffers and SRVs (find the bone palette),
   blend state, depth state, render targets. Write it into re/notes/ (new `meshes.md`).
4. Proof: re-issue that one draw a second time with the palette's `Wep_Mag` entry moved 10 cm: two
   magazines on screen. Then cut the index subset to the magazine only.
5. Grenade: same capture with the grenade drawn (pad LB), then redraw it while the rifle is out.

Every engine address found goes into `re/symbols/bf4-179547.md` with its evidence.

## Then (implementation)

- `render/extra_mesh` (engine-free placement + D3D draw copy), fed by physical_reload (second magazine pose:
  the hand's magazine while the gun holds one; the caught one while palming) and physical_grenade (held
  grenade pose); the native `Wep_Mag` stays with the gun.
- Setting `extra_meshes` (on/off) with a panel row and an ini line; fall back to today's one-mesh drawing
  when the copy is not available (another gun, a failed capture).
- Headset rows: magazine in hand with one in the gun (AK flick, SCAR palming), held grenade.
