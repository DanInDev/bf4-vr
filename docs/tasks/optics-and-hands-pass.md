# Task brief: every optic, then every gun's hands (a once-over)

Written 2026-11-25 at the end of a long session (owner's headset test the same evening). Branch per fix from `dev`. Read
`CLAUDE.md` first (binding: safety, branches, "done means verified", tool pitfalls), then this brief, then the issues named.

## Goal

1. **Optics:** take every magnifying optic in the campaign one by one and make each look and work like the real one in
   VR: the world magnified inside the optic's own glass, the optic's own reticle in it, on the sight line, level with the
   gun, sharp, no stray layers or black areas, no flicker. Fix what fails, optic by optic.
2. **Hands, every gun:** a once-over of all 51 crate guns (`docs/tasks/all-weapons.md` lists them with their mechanics)
   for the hands: the firing hand's grip and trigger finger, the off hand on the handguard or foregrip, the hand holding
   a magazine (fingers must not cover the magazine through its body; held near its bottom third), the rack / bolt fist on
   the charging handle (overhand and underhand), the hand on an LMG lid and belt, a pistol's two-handed hold. Fix per gun
   in the per-gun data (`src/weapon/profiles.cpp`, `gun_traits`, zone defaults), not in one-off hacks.

Owner's bar (CLAUDE.md "Working with the user"): life-like over easy; each gun as the real one; every fix gets the gate,
a monitor check with raw output, a headset row in `docs/HEADSET-TESTS.md`, and results on its issue.

## Where things stand (2026-11-25, `dev` ae0e99c)

Optics, verified on the monitor (Shanghai, fake head and hands; `re/notes/weapon.md` sections "The picture in the optic's
own glass", "Every optic's own reticle in its glass"):

- The scope picture is rendered along the gun's sight line (the scope camera), and the glass now reads it on that
  camera's axes (#158). Magnifiers measure 1.70-1.88x for 1.73 (CZ-805, AUG), the ACOG 2.2-3.0 for 2.74, with the head
  straight, turned 25 and rolled 20 degrees; on the line where the match is clean; about one picture pixel per eye pixel.
- Each optic's own reticle is drawn in its glass from the game's art (ACOG bars, 4/6 ladder and red chevron; PSO-1
  rangefinder; SRS 8x mil-dot, PKS-07 chevrons, ballistic crosshair). ACOG/PSO art comes with a material (size, aspect,
  anchor); the SRS scopes' art is a model quad read back once (`lens_math::scope_art_from_quad`), its width taken as
  0.8 of the screen height at the zoom level (an estimate from BF4 pictures: judge it in the headset).
- The SRS's closed hip eyepiece gets a virtual eyepiece (a disc on the line); no other optic should (#145: only while
  the eye is near the line and the optic has not shown an open glass for 15 renders).
- Owner's headset 2026-11-25: magnifier and 4x "work great". The theater's extra scope quad (the disc in front of
  the optic) and the menu detector's flat-screen switches while scoping are fixed (#145).

Open optic issues (read each first): **#163** flat-screen flashes still (first raise, gun at the face: lead in the
issue, the theater falls to the flat screen when an eye image is missing), **#164** LMG + PKA lost its picture after a
weapon switch, **#165** SV-98 + PKS-07 black, **#162** a dark square over the optic (the second-eye stray), **#161**
the AK-12's front vanishes with the game's aim on, **#159** sharpness (frame cost of the larger render, #144), **#143**
the UMP-45's RX01 red dot not recognised, **#93** red square on iron sights, **#140**, **#139**, **#137**, **#39**.

Hands, open: **#129** MK11 and **#131** SV-98 (owner 2026-11-25: realign the held magazine, tweak the chambering hand,
fingers cover the magazine), **#87** rack fist on the action, **#88** handles not measured, **#90** magazines in the
hand, **#91** foregrips, **#141** magazine lengths and grab points, **#67** palming, **#62** hand collision, **#95** the
LMG rack hand's small offset, **#160** forearm twist (default 0.8, owner to judge), and the per-gun issues #100-#131.

## Optic list to go through

From `unlockfind` in Shanghai (attachments named `U_<gun>_<optic>`): ACOG, PSO-1, PKA (3.4x), PKA-S, M145, Compact4X,
CP1_Prismatic, ATN_Thor, IRNV, Magnifier (flip), Rifle_Scope (8x), PKS-07 (7x), Ballistic_Scope (40x) and the reflex
sights (EOTech, PK-AS, RX01, Kobra, HOLO...). Per optic, on at least one gun that carries it:

1. `tools\optic_shots.ps1 -OutDir <dir> -Give 'givegun <gun> 0 <optic>' -Name <n>`: `scope` (power, `glass_arts`,
   `open_glass`, `virtual`, `field`) and eye shots facing the lit towers (turn the soldier left with `pad 0 0 -30000 0 250`
   until the line points at buildings, not the curtain).
2. `tools\optic_capture.ps1` with `-Head '0.064'`, `'0.064 25'`, `'0.064 0 0 0 20'` (and `-Flip` for magnifiers), then
   `tools\glass_magnification.py`: the measured power within 10 % of the data's, offset 0 where the match is clean.
3. The reticle: the game's own art in the glass, its size against a real picture of the sight; `tools\optic_grab.ps1`
   if an optic shows a dot or nothing.
4. A headset row per optic family; the owner checks in batches.

Optic pitfalls learnt this session (also in CLAUDE.md and re/notes/weapon.md): no eye shot shows OpenXR layers (the
theater's quads only show in the headset / SteamVR view; the theater does not run without a headset session);
`scope`'s lens-report `power=` is the last optic's (use `magnification=`); D-pad up toggles a magnifier; `givegun` gives
no attachments unless named; one straight edge or a window grid fools the magnification match.

## Hands: method

- Tools: pipe `fingers` (holds, palm readings, rack fist offset `rack_fist_grip_minus_knob`), `reload` (zones, `palm=`,
  `zone=`), `arms`, `twohand`, `handle_probe.py` (the game's own hand on the knob), `grab_debug`, the in-VR zone editor
  (#152: tuned zones become defaults via `tools/zones_to_defaults.py`), `pose_*` settings for the finger editor.
- Fake controllers: `fakehand 0 0 lx ly lz rx ry rz [lyaw lpitch lroll]`; the reload zones are measured from the
  controller itself (`palm=`), other grab zones 6 cm behind it (CLAUDE.md pitfalls). Pull a grabbed magazine in 2 cm
  steps, never teleport.
- Reference for fingers and reloads: The Great Destroyer (`src/handpose.cpp` per-joint finger poses, `src/reload.cpp`
  bolt and charging handle; reuse allowed, credit it), Overlord for design only (GPLv3, never copy).
- Work gun by gun in `docs/tasks/all-weapons.md` order; per gun: screenshots of each hold from two angles (`eyeshot`,
  `eyeshot2`, fake head turned), the fix in per-gun data, a unit test where the logic is engine-free, a headset row.

## Done means

Every optic in the list checked and either right (monitor numbers on its issue) or fixed; every crate gun's hands looked
at, fixed where wrong, with headset rows the owner can tick in batches; nothing merged without the gate green.
