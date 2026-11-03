# Task: believable hands (fingers, magazines, charging handle)

Status: **built and partly monitor-verified (2026-11-03), branch `feature/hand-animation`** (on top of
`test/vr-check-2026-11-03`, not on `main`). Live testing is still open: the game checks below, then the
headset rows H1-H5 in `docs/HEADSET-TESTS.md`. Off switch: `finger_poses = 0` (panel, Body page).

Goal: the hands look like they do what you do. Fingers follow the controller. A magazine sits in the
fist. The charging handle is held and pulled, and the bolt comes back with it. The trigger finger
lifts off.

## What it does

| Hand / situation | Drawn fingers | Where it is decided |
| --- | --- | --- |
| Off hand, empty | squeeze curls middle/ring/pinky, trigger (or a touch) the index, a thumb on a button/stick/rest the thumb | `fingers::shape_for(task::free)` |
| Off hand inside a grab zone (magazine, handle, pouch) or at the grenade belt | open, ready to grab | `physical_reload` → `set_off_hand_task(reach)` |
| Off hand holding a magazine (also while it slides home) | wrapped round its body; the magazine is placed **in the drawn fist** and published back, so the reload inserts what you see | `arm_pose::attach_held_magazine`, `reload_hands` (`has_held`) |
| Off hand racking | hooked; the handle stays inside the index's curl (palm if the fingers are off) and follows the pull; `Wep_Slide` moves back by the pull and springs forward on release. At the rear stop (once the pull counts) a tick and `bolt_back`; the release plays the bolt home and chambers, even after riding the handle forward | `reload_hands` (`racking`, `rack_pull`, `rack_at_rear`), `arm_pose` (`handle_request`) |
| Gun hand firing | the drawn gun kicks up, sideways and back per shot by the gun's own recoil data and settles (`recoil`) | `body/gun_recoil`, `arm_pose::kick_gun` |
| Off hand on the handguard (two-handed) | the game's handguard grip | `support_weight >= 0.5` |
| Off hand with a grenade | closed (magazine shape) | `grenade_in_hand()` |
| Gun hand on a gun | the game's grip; the index straightens along the frame when the finger leaves the trigger (needs the trigger-touch binding) | `task::gun_grip` |
| Gun hand with a knife, grenade or gadget; any arm the IK did not solve | as animated | `task::item` |

All shapes ease over ~70 ms (`shape_smoother`). A magazine pushed in slides from the hand into the
magwell over 0.1 s (`seat_seconds`).

Rig facts and the method are in `re/notes/skeleton.md` ("Fingers"). The engine-free parts are in
`src/body/finger_pose`, tested by `tests/finger_pose_test.cpp`. Controller sensors: `src/input/fingers`
(trigger touch, thumb touch, squeeze, trigger; touch bindings fall back to none if SteamVR refuses them).

## Verified on the monitor (fakehead + fakehand + pipe `fingers`)

- 28 finger joints found on the AK-12 rig; the bend axis is right (open hand at curl 0, fist at 1).
- A magazine pulled from the gun or taken from the pouch sits in the drawn fist and inserts from there.
- Off hand opens in the magazine zone.
- Racking: palm on the handle, `pull=0.060` after a 6 cm pull.
- Gun hand's index: 8/10/6° bend off the trigger, 25/34/95° (the game's) on it.

## Live testing needed later

### A. In the game, no headset (monitor; `research = 1`, fakehead + fakehand)

| # | Check | How | Tune |
| --- | --- | --- | --- |
| A0 | ✅ 2026-11-03 (monitor): **neutral off-hand wrist**. Empty, the hand stands upright, thumb on top, knuckles forward (as you hold the controller); a held magazine stands upright in the fist and re-inserts from there. Now the **default** (`off_hand_pose = neutral`); headset row H6 decides | Still open: yaw 40° / pitch 30° (fakehand turns both hands, so the gun covers the off hand: move the gun hand away first) | If the headset says otherwise: `off_hand_pose = animated` |
| A0b | **Handle inside the hooked index**: while racking, the hold puts the centroid of Index1-3 on the handle (`hook_in_hand`), not the palm. The rack itself works (2026-11-03: pull 0.077, `round_ejected`, both sounds recorded); the hand's look at the handle is not screenshotted yet | Rack with fakehand (palm at `handle_zone`, `pad 0 0 0 0 4000 0100` in a background job, move 6 cm back) and screenshot while held | If the hand sinks into the receiver: move the hook point towards the fingertip (Index2-3 only) |
| A6 | ✅ 2026-11-03: **recoil**. A 1.2 s AK-12 burst kicks the drawn gun ~6.5° up and 1.4 cm back mid-burst; it settles within ~1.5 s after the trigger. Pipe `recoil [shots]` | | `recoil` (0-3), `recoil_config` in `body/gun_recoil.hpp` |
| A6b | ✅ 2026-11-03: **per-gun recoil** from GunSwayData (`tools/recoil_probe.py`). AK-12: x1.00, first shot x2.1 (5.1°), peak 7.7°, sideways up to 1.5° mostly right. SCAR-H: x1.67, first x1.75, peak 14.9° (cap 15), ±1.3° sideways, settles 1.5x slower (τ 0.32 s measured). Mid-burst screenshot: the SCAR-H pitched up and back | Other guns (pistol, LMG, shotgun) when a checkpoint has them: `recoil_probe` prints their data | `profile_from_game` (`gun_recoil.cpp`): sideways gain 0.5, clamps |
| A7 | ✅ 2026-11-03: **two-stage rack sound** (recorded): `bolt_back` ~0.1 s after the pull passed 4 cm, before the release; the game's bolt on release | | WAVs `bfvr_sounds\bolt_back.wav` / `bolt_home.wav` replace the synthesised cues |
| A8 | ✅ 2026-11-03: **trigger finger without touch sensors** (`fingers gun 0 0`): the index stays on the trigger (25/34/95°); with sensors and off the trigger (`fingers gun 0 0 0 1`) it straightens (8/10/6°) | | |
| A1 | **Where is the charging handle?** The hold uses `handle_ak = (0, 0.03, 0.40)` / `handle_ar = (0, 0.06, 0.20)` in `Wep_Root` axes (top centre), but the AK-12's handle is on the right side, and `Wep_Slide`'s origin sits 16 cm further forward (gun frame (-0.009, 0.037, 0.558)) | `bone 128 0 0 -0.08` pulls the slide bone 8 cm; screenshot the gun's right side with and without it (gun hand pointing left: `fakehand 90 0 ... 0.30 -0.05 -0.55`, framing still to be found). See what moves and where the handle is; `bone -1 0 0 0` clears it | `hand_config::handle_ak/handle_ar` (`reload_hands.hpp`), `slide_travel` (8 cm) |
| A2 | Does `Wep_Slide` move the bolt/handle mesh at all, on the AK-12 and an AR (M416, SCAR-H)? | Same as A1 on each gun | If not: drop the slide pull for that gun |
| A3 | The magazine in the fist on an AR (only the AK-12 was checked) and the 0.07 m top offset | Grab with the hand at the magazine zone (pipe `reload` prints the zones; `fakehand` sets the **palm**), move away, screenshot | `held_magazine_drop` (`arm_pose.cpp`), magazine/handle curls in `finger_pose.cpp` |
| A4 | Grenade belt: the hand opens near the belt, closes on a grenade | `grenade = physical`, fakehand at the belt, pipe `fingers` shows `task=reach`, then `magazine` while holding | `reach_belt` |
| A5 | Pistol (secondary) and two-handed: the index off-trigger pose on a pistol grip; transitions on/off the handguard without popping | `fingers gun 0 0 0 1` vs `fingers gun 0 0.5 1 1`; `twohand` grab | bend ranges in `finger_pose.cpp` |

### B. In the headset (rows H1-H5 in `docs/HEADSET-TESTS.md`)

- Do the fingers match what your hand does (squeeze, trigger, thumb), with no lag or snapping?
- Does the magazine sit in the hand when you look at it, and turn with your wrist? Does inserting feel like pushing it in?
- When racking, is the hand on the handle, and does the pull distance feel right (4 cm to chamber, 8 cm travel)?
- Does the index straightening on the gun distract you, or help (trigger discipline)?
- Log the pipe `fingers` output once per hand, with your hand open, squeezing and touching: if `trigger_touch`/`thumb_touch` never change, SteamVR refused the touch bindings (look for "finger touch bindings refused" in `bfvr.log`).

## Compared with Overlord (design docs read 2026-11-03; GPLv3, nothing copied)

| Overlord | Here |
| --- | --- |
| Trigger discipline: touch drives the index; no touch sensor (binding refused, inactive) keeps the authored pose; leaving the trigger 35 ms half-life, returning 12 ms | Same contract: `finger_input::touch_sensors` from the action's `isActive`; `shape_smoother` closes with a 12 ms and opens with a 35 ms half-life. Overlord also fits the straightened index per gun family to clear the receiver: we use one bend range (A5) |
| Free wrists use a neutral basis, not the foregrip's animated angle; the magazine's long axis follows the controller's up | `off_hand_pose = neutral` (default now), magazine placed in the drawn fist |
| Two magazine grasps (wrap, or bottom pinch when the palm turns past ~190°), latched at the draw | One grasp (wrap). Next step if H3 feels wrong |
| Charging handle: full stroke near the end of travel; rear and close sounds split from the gun's chamber recording; catch/unhook haptics | Rear stop at `rack_pull` (4 of 8 cm) with a tick and `bolt_back`, the game's `Bolt` (or `bolt_home`) on release. The game's sounds are graph events, not recordings, so they cannot be sliced: the rear cue is synthesised. Consider `rack_pull` nearer the full 8 cm after H8 |
| Recoil: muzzle pitch per shot from the weapon's hip kick values, τ 0.22 s, cap 55°, standing 3x / crouched 2x / prone 1x, 4x one-handed | `body/gun_recoil`: 2.5° and 1.2 cm per shot on the AK-12, scaled per gun by BF4's GunSwayData (hip recoil of the current stance: up, sideways, decrease, first-shot multiplier; re/notes/weapon.md), τ 0.22 s / 0.06 s on the AK-12, cap 15°, two-handed x0.4, `recoil` strength. No stance multiplier: BF4's data is the same for every stance, and the player's real arms hold the controller the same way whatever the soldier does |
| Empty hands drawn with no weapon; magazine-strike releases; handle catches; per-gun receivers (MP5, AUG, ...) | Not here: BF4 always has a weapon in hand; only AK and AR reload variants exist so far |

## Possible next steps (after the live results)

- Per-gun handle points from A1 instead of the two variant constants.
- The racking hand's rotation fixed to the gun (like the support hand) if the controller's rotation looks wrong on the handle.
- Holsters: the gun hand keeps the gun's grip while reaching back; a visible hand opening would need the gun hidden first.
- Hand poses for the knife and gadgets (task `item` today).
