# Task brief: sights and aiming down sights that work in the headset

Written 2026-11-11 for a fresh agent session. Read `CLAUDE.md`, `docs/SAFETY.md` (binding) and this
file first; everything else is linked from here. Issue: **#39** (ADS and scopes, priority high); related
#12 (controller aim), #24 (gun size and depth), #29 (two-handed).

## Goal

In the Quest 3, holding a gun's optic up to one eye must work like a real optic:

1. **Red dot / holo:** a red dot in the lens, on what the barrel points at, seen by the eye behind the
   sight only, whatever the head does. AK-12 (`U_AK12_PK-AS` red dot) and SCAR-H (`U_SCAR-H_EOTech` holo)
   in the checkpoint.
2. **AK-12 lens:** clear when you look through it, not the solid black disc it is today.
3. **Flip magnifier (AK-12):** flipped in, the lens shows the world ~1.7x with a dot, only in the
   sighting eye, the view itself never zooms, no pumping.
4. **ADS behaviour:** raising the sight to the eye gives the game's ADS (accuracy, walk speed, lens
   state) without flicker, and never by accident with the gun near the face.

Done means the headset rows below are confirmed by the user (`docs/HEADSET-TESTS.md`), with
`tools\gate.ps1` green and the monitor checks recorded in `re/notes/weapon.md`.

## Where it stands (headset 2026-11-11, `main` 410ea2f)

| Element | Headset result | State |
| --- | --- | --- |
| Red dot, SCAR-H holo | **not seen** | The mod's dot layer was submitted (log: `sight dot swap chain 64x64`, `status` `sight_dot_frames=997`, about 10 s of a 4-minute run) but the user saw no dot |
| Red dot, AK-12 | **not seen**; lens **black as always** | Mark 2 had the game aiming (`aiming=1`), so `sight_ads` engaged at least once, yet the lens stayed black to the user |
| Hip-fire accuracy (`hip_accuracy`) | **works**: full auto pin-point on the dot | Keep at 1. Values for more spread later: `re/notes/weapon.md` "Hip-fire spread" |
| Magnifier (AIM-14) | not reached | Monitor only |
| Shots along the barrel, green aim dot (AIM-10, AIM-11) | work (2026-11-09) | Don't regress them |

History: 2026-11-03 "sights broken beyond belief"; 2026-11-04 MP2 (AK lens black, holo without cross:
`sight_ads` had been saved off, and eyes closer than 10 cm never counted); 2026-11-09 MP2b "ADS and
scopes still do not work at all"; 2026-11-10 nothing shown, ADS flickered 22 and 82 times (now latched);
2026-11-11 above. Details per check in the `AIM-*` rows and the "Main problems" table of
`docs/HEADSET-TESTS.md`. **Every fix so far looked right on the monitor and failed in the headset**: the
monitor checks use `fakehead`/`fakehand`, which hold the eye exactly on the line. Plan for that gap.

**2026-11-12 (branch `fix/sights-headset`):** the monitor gap was the hold, not only the eye: every
headset mark had the gun in both hands, and the two-handed hold zeroed the aim button, so the game never
aimed and the hip pose became the "sight line" (`re/notes/weapon.md`, "Why the headset never aimed").
Fixed and checked two-handed on the monitor. **Test sights with both fake hands and pad `300`.** Still
open: the SCAR-H's dot was submitted to the right eye (mark 1) and not seen; `sight_dot_test` and a VR
View capture decide between "layer not shown" and "too small to notice" (short list S1-S7 in
`docs/HEADSET-TESTS.md`).

## How it works today

The full measurements are in `re/notes/weapon.md`, sections "Sights and aiming down sights",
"Magnified sights", "The game's reticle follows the view, not the sight", "Hip-fire spread",
"The magnifier without the game's ADS", "A gun's attachments" and "Open". The short version:

- **Sight line.** `body/arm_pose` samples the drawn gun's sight (the game's ADS camera joint in `Wep_Root`
  axes, else `Wep_Scope1 + (0, 0.035, -0.15)`). `weapon/two_handed.cpp` (`update` around line 290) turns
  it into a start point and barrel direction in OpenXR LOCAL space each frame, and checks both eyes
  (`head.eyes[e]` from `stereo::latest_head_sample`) against it.
- **`sight_ads`** (`two_hand.cpp` `eye_behind_sight`, `sight_ads_latch`): an eye within 3 cm of the line,
  up to 30 cm behind the game's eye point and 2 cm short of the optic, holds the game's LT
  (`input::set_sight_aim`); it lets go 0.3 s after both eyes are beyond 5 cm, or 0.4 s after the last
  shot. Skipped for a gun whose only optic is a clear holo while `hip_accuracy` is 1
  (`weapon/optics::needs_game_aim`), so the SCAR never aims (no game reticle over ours, full speed).
- **`sight_dot`** (`two_hand.cpp` `sight_dot_for_eye`, `xr/theater.cpp` `add_sight_dot_layers`, texture
  `ui/reticle.cpp` `sight_dot_texture`): for an eye within **1.5 cm** of the line (the magnifier's rear
  lens radius), a 64x64 premultiplied red dot on an OpenXR quad 50 m down the sight line from that eye,
  `sight_dot_size` 0.3 degrees, `eyeVisibility` left or right only. Shown aiming or not; only for guns
  with a red dot or holo among their attachments (`weapon/optics`, `weapon/sight_state`). The green aim
  dot hides while it shows.
- **The game's own reticle** is drawn at the eye image's centre, not along the sight: 2 degrees of head
  pitch moves it, 5 degrees hides it. That's why the mod has its own dot. Hiding the game's reticle
  failed: it comes out of a full-screen or compositing pass, not one draw (`eyehide`).
- **The AK-12's black lens** is opaque outside the game's ADS and clear in it. It's drawn outside the gun
  pass, probably a separate small render (an 864x864 target among the frame's first draws) composited
  into the lens. Not solved: either find that view and compositing pass, or a game-side switch for the
  lens state.
- **Magnifier** (`stereo/scope*.cpp`, `eye_pass.cpp` `render_scope`): a third render of the world from the
  sight's eye point at the lens FOV divided by the magnification (learned 1.725x), cut to a circle with
  a red dot and shown on a quad at the lens for the sighting eye. It runs at the hip when the selected
  zoom level is a Magnifier (`weapon/sight_state`: weapon `+0x4988` `+0x1F8`/`+0x1E8`, `SightType` at
  level `+0x2C`) and an eye is within 3 cm of the line. Cost: 87-91 fps scoped vs 98-107 at the hip.
- **Gun size in ADS**: `gun_fov_life_size` keeps the gun at life size through the gun pass's own FOV (45
  degrees at the hip, 17 in the game's ADS); with it off the red dot never shows.
- **Shots**: `shots_from_gun` and `shot_line = gun` put bullets on the drawn barrel; `hip_accuracy` 1 makes
  hip fire as tight as ADS (GunSwayData patched live).

### Settings involved (`src/common/settings_store.cpp`, panel Weapon page)

`sight_ads` 1, `sight_dot` 1, `sight_dot_size` 0.3, `scope` 1, `scope_zoom` 1, `gun_fov_life_size` 1,
`hip_accuracy` 1, `ads_trigger` 0 (LT = the game's ADS by hand, for monitor tests), `shots_from_gun` 1,
`shot_line` gun, `reticle` 1, `reticle_depth` 1, `aim_convergence` 25. Every new behaviour gets a live
setting, a panel row and a line in `tools/bfvr.ini.example` (CLAUDE.md).

## Direction from 2026-11-12: the game's own optics, for every gun (owner's requirement)

Headset, third run (`gun_scale` and `ipd_scale` at 1): no large change. The only red dot seen is the mod's
own, also at the hip; the AK-12's ring still shows "in a weird way to the right of the gun". The owner
expects other scopes and holographic sights, each with its own reticle, to have the same problems, and
wants **one solution for every gun and optic, not work per sight**.

So the mod's own dot (`sight_dot`) is a stopgap at best: it cannot draw each optic's reticle. The plan is to
make the game's optics work as they are. What the type registry says (static, `tools/typeinfo_dump.py`, not
yet followed live):

| Type | Fields | Meaning |
| --- | --- | --- |
| `WeaponMesh1p` (0x10) | `+0x00 Mesh`, `+0x08 MeshZoom` (SkinnedMeshAsset) | Every first-person weapon has a second mesh for zoom |
| `WeaponSocketObjectData` | `+0x10 Used1p`, `+0x18 Used1pZoom`, `+0x20 Used3p`, `+0x28 Used3pZoom` | Every attachment (optic) too |
| `SocketData` | `+0xD4 HideByZoomTransition` | Attachments hidden across the switch |
| `WeaponStateData` (0x88) | `+0x48 ZoomMeshTransitionFactor`, `+0x4C ZoomScaleFactor`, `+0x50 ZoomInOutMeshTransitionFactors` | Where in the zoom animation the meshes switch |
| `WeaponZoomLevelData` | `+0x20 ZoomInOutMeshTransitionFactors`, `+0x2C SightType` | Per zoom level |

Reading (to verify): the black AK-12 lens is the hip mesh of the optic; the clear lens with its reticle is
the zoom mesh, shown only past the transition point of the game's ADS. The ring beside the gun in the
right eye's image while the game aims (monitor `eyeshot2`, and the owner's "ring to the right") would be
one of the two meshes not getting the second eye's shift.

State 2026-11-12 evening: step 1 is built (`weapon/zoom_mesh`, `sight_zoom_mesh`; the factor is plain data,
0 shows the zoom mesh at the hip; monitor-checked on the SCAR-H, the AK-12 not yet). Step 3's draw is
found only in part: an 18-index draw (the dots) and 6-index draws (the ring); moving them by the viewport was tried and does not work yet (`sight_reticle`, off; the table in the notes). Step 2 is intermittent and unexplained.
Evidence: `re/notes/weapon.md` "The zoom mesh at the hip".

Work order:

1. **Find the runtime switch.** Who reads `ZoomMeshTransitionFactor` and which field of the client weapon
   holds "zoom mesh shown" (REA: xrefs to the field offset in the `WeaponStateData` readers; live: diff the
   weapon object hip vs ADS, `pipe watch` on the candidate). Goal: show the zoom mesh of weapon and
   attachments while a sight is at the eye, without the game's aim (no speed change, no FOV pumping, no
   input held).
2. **Both meshes in both eyes.** Find why a piece is drawn at the first eye's place in the second eye
   during ADS (`eye_pass` `foreground_block` / `shift_per_view_block` against the zoom mesh's blocks).
3. **The reticle along the sight.** With the zoom mesh shown, find what places the reticle at the view's
   centre (`meshprobe` on the zoom mesh's lens draw: its pixel shader constants) and give it the sight
   line's direction per eye. This is the step that makes every reticle (dot, ring, cross, scope
   crosshairs) work without drawing our own.
4. **Scopes**: the lens picture (`stereo/scope`) keyed on the weapon's own zoom level data (`RenderFov`,
   `SightType`, learned magnification), lens size from the optic's mesh bounds instead of the magnifier's
   constants.

Rule for all of it: nothing keyed on a weapon or attachment name. `weapon/optics` (suffix table) becomes a
fallback only.

## Leads for the invisible dot (not verified)

Start here. Find out what the headset actually shows before changing anything:

1. **See it.** `tools\vrview-shot.ps1` captures SteamVR's VR View with every layer; marks and `eyeshot`
   never contain the dot. Ask the user to hold the sight to the eye, then call `mark` and
   `vrview-shot` together. VR View shows one eye (check which in its own menu), so ask for the sight
   at that eye.
2. **Placement.** The dot is placed from `head.eyes[e]` and the sight line taken in the game frame. The
   compositor shows it at the time the frame is displayed. If the line, eye positions or LOCAL space
   disagree with where the drawn optic actually appears (gun life size, `gun_fov_life_size`,
   per-eye offsets of the gun pass), the dot can sit outside the lens, or on the far side of the housing.
   Compare in a VR View capture: dot vs the drawn lens.
3. **The 1.5 cm gate.** In the headset an eye is rarely within 1.5 cm of a hand-held line for long
   (`sight_dot_frames` reached 997 in four minutes). Log `eye_off[e]` while the user aims (the `twohand`
   or a new `sight` pipe report), and consider a wider gate, with the dot still on the line, so it
   behaves like a real reflex sight whose dot stays on target off-axis.
4. **Size and brightness.** 0.3 degrees is about 7 px in a Quest 3 at 50 m. Check `sight_dot_size` 0.6-1.0
   in VR View. The texture is premultiplied BGRA on a B8G8R8A8 sRGB swap chain (format 91 in the log).
5. **Layer order.** Projection first, then vignette, HUD, rings, held grenade, scope, sight dot, aim
   reticle (`theater.cpp` around line 2350); quads draw over the projection whatever their depth, so the
   black lens can't hide it. If `scope_visible()` is true the dot is skipped on purpose.

## Tools

- Loop: `tools\dev-run.ps1 -Preset bf4-release -Resume` (Debug runs at ~14 fps and changes timing:
  judge sights in Release). The checkpoint has the SCAR-H in slot 0 and the AK-12 in slot 1; the room
  has walls to shoot at and your squad in it.
- Pipe (`tools\pipe.ps1`, `docs/TOOLING.md`): `status` (`sight_dot_frames`, fps), `twohand` (held, barrel,
  aim), `sightstate` (unlock, zoom levels, selected / in-use level, SightType, attachments), `scope` and
  `scopeshot`, `hipaccuracy`, `eyeshot` / `eyeshot2` (each eye's game image), `fakehead <ipd> [yaw]
  [height] [pitch]` and `fakehand <yaw> <pitch> [left xyz [right xyz]]` (clear both with `off`), `mark`,
  research: `meshprobe`, `eyehide`, `eyeskip`, `eyezoom`.
- `python -E -P tools\sight_probe.py [--ads 2]`: the game's own ADS sight line in `Wep_Root` axes.
- `tools\vrview-shot.ps1 <out.png>`: the composited headset view (above).
- Reverse engineering: `pipe dump re\dumps\bf4-179547-mem.exe`, `tools\disasm.py`, and REA/Ghidra for the
  lens render and compositing pass (open the binary once per session, ~10 min). Record every address in
  `re/symbols/bf4-179547.md` and the evidence in `re/notes/weapon.md`.

## Headset checks to close (`docs/HEADSET-TESTS.md`)

AIM-12 (red dot and holo, `problem`), AIM-14 (magnifier at the hip, open), AIM-1 / AIM-2 (ADS by raising
the gun, retest), AIM-3 (gun depth in ADS), AIM-5 / AIM-6 / AIM-7 (magnifier picture, hits, eye
distance), VIEW-4 (walking while aiming). AIM-13 (hip accuracy) is done: keep it. Rewrite these rows
as you change things; give the user one short list of 5-10 broad checks per session, not the whole
table (the user's preference), and ask for a mark plus VR View capture at each sight check.

## Rules that apply

- `tools\gate.ps1` green before calling anything done; state what was not checked.
- Engine-free logic (gates, placement maths, latches) gets a unit test under `tests/`
  (`two_hand`, `scope_math`, `hip_accuracy` and `optics` already have some).
- Every engine address verified (bytes or prologue) before hooking.
- No tools while Battlefield 6 / EasyAntiCheat games run; never commit dumps, screenshots or marks.
- One branch from `main` (e.g. `fix/sights-headset`); merge only when the user asks.
- Restore any live `setting set` you changed; `fakehead`/`fakehand off` when done.
