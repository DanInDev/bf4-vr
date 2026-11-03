# Headset test log

Quest 3 via Steam Link, SteamVR 2.18.4 OpenXR, RTX 5070. Newest first.

## Next — overnight 2026-11-04 (`feature/overnight-2026-11-04`, on top of `feature/headset-feedback-2`)

Use the **Release** build in the headset (Debug runs at ~14 fps). Monitor-verified overnight (fakehead +
fakehand, Release unless noted):

| Area | Monitor result |
| --- | --- |
| Sights | The AK's red dot lens is opaque outside the game's ADS and clear in it. Holding the sight to an eye now gives the game its ADS (`sight_ads`): lens clears, front post visible through it, gun stays life size (gun pass 17 deg in ADS, compensated) |
| Bullets | Shots follow the sight line measured in the game's own ADS (AK 0.0 deg, SCAR 0.25 deg off the barrel). Five single shots through the AK's red dot: one hole ~4 cm below the view centre at ~2.5 m (a constant offset). A bug found and fixed on the way: firing swung the old hip estimate by +-13 deg |
| Chest rig | Knife from the high left chest, SCAR over the right shoulder, AK from the right hip, gadget 1/2 over the left shoulder / left hip (drawn by either hand; the checkpoint's gadget slots are empty, so the game had nothing to switch to), grenade taken from the high right chest and put back, magazine pouch on the low chest, no zone overlaps |
| Left arm | Elbow no longer flips at the belt/hip; shoulders anchored either side of the neck (`body_shoulders`): worst elbow step 19.8 -> 7.6 cm (hand passing its own shoulder), others 1-2.5x the hand step |
| SCAR | Empty chamber + new magazine + B: chambers (20 -> 19, chamber 1) |
| AK two-handed | Hold stays through a slide along the whole handguard under the handle zone |
| Panel | Backdrop premultiplied at 60 %, bars 85 % |
| Performance | Release, 2160x2160 per eye, pair rendering: 96-116 fps in the checkpoint room (mono 108); 10 min soak: median 94.6, 5th percentile 93.6, lowest 72.6, no stalls or rejected pairs |
| Magnifier | Flipped in (D-pad up) and aimed: centred sight picture, gun pass 18 deg (compensated), no black scope overlay; no magnification (the world view is the headset's) |

| # | Check | Look for | Result |
| --- | --- | --- | --- |
| N1 | **Aim down the sights**: raise the gun until one eye looks through the optic | Within ~2.5 cm of the line the game aims down the sights (lens clears, the walk slows); the green dot sits in the optic; gun size unchanged. Lower it: back to the hip. `sight_ads` off (Weapon page) to compare | |
| N2 | Shots through the sight at ~10 m and ~25 m, single shots | Hits where the dot / the optic's centre is. Hip shots: where the dot is | |
| N3 | Scoped or magnified optic (flip the AK's magnifier with the accessory toggle, then aim) | Note what the lens shows and whether a black scope overlay covers the view | |
| N4 | **Chest rig**: look down; left hand to the high right chest + grip: grenade; right hand high left chest: knife; right hip: secondary; over the right shoulder: primary; either hand over the left shoulder / left hip: gadgets 1 / 2 (after taking C4 / M320 from the stash) | Each on the first try; the magazine pouch is now low on the chest | |
| N5 | Left arm: reach to the belt, hip, chest, across, behind | Elbow and shoulder move smoothly; the shoulder sits beside the neck, not mid-chest. `body_shoulders` off to compare | |
| N6 | Comfort of auto-ADS while moving | ADS slows walking; if it engages by accident (gun near the face), note when | |

## 2026-11-03 evening — feedback on `main` 246f2fb, fixes on `feature/headset-feedback-2`

| Item | Result (user) | Change |
| --- | --- | --- |
| Shots and dot | hit the dot, but dot and bullets follow the controller's pointing ray, not the drawn barrel | `shot_line = gun` (new, default): shots and dot leave the drawn gun's optic (Wep_Scope1) along its Wep_Root barrel; `controller` = old behaviour. Shots now also follow the recoil kick |
| Reload, chambering sounds | great, except the start of the chamber sound (not like a real asset) | the synthesised rear stop (`bolt_back`) is muted; only a supplied `bfvr_sounds\bolt_back.wav` plays |
| Two-handed | fine on the SCAR, keeps dropping on the AK | the AK handle zone overlapped the handguard grip: a held two-hand grip now only lets go when the off hand really takes the magazine or handle (`reload_engages_off_hand`), and all zones are smaller |
| Grab rings | liked, but 30 % smaller | every reach ×0.7 (magazine 10 cm, handle/slide 8.5 cm, chest 21 cm, handguard 12.5 cm, belt 15.5 cm, pin 10 cm, holsters 11-15.5 cm, accessories 5 cm) |
| Wrists and hands | mostly fine; left shoulder (that whole side) glitches and twists | arm solver: (1) the elbow hint flipped as the hand passed its direction (down-out-back: belt, hip), the elbow jumped 35 cm in a 0.75 mm hand step in the unit test; a second hint (out and back) now takes over there (4.8 mm); (2) the upper arm turned by a shortest arc, so the elbow bent about a sideways axis: it now keeps the animated elbow hinge; (3) both hints turn with the torso; (4) the torso eases in as the hands' midpoint passes 15 cm instead of switching |
| SCAR chambering | the handle is front-left; with a new magazine and an empty chamber, the reload button dropped the magazine | the button now chambers after a new magazine also when the bolt did not lock (old magazine out before empty); SCAR handle zone moved front-left (estimate: 0.035, 0.04, 0.57 on Wep_Root, not measured) |
| Settings panel | looks great; should be see-through like the HUD, with the menu's pointer | backdrop at `panel_opacity` (0.6, Controls page), bars a little denser, buttons and text opaque; the laser beam and a dot on the panel from the pointing hand |
| Physical stance, foley | on the backburner | — |
| Sights | "broken beyond belief" | **open:** what they look like is not known yet (reticle wrong or missing in the scope, or misaligned with the shots) |

Not monitor-checked yet (the user was at the PC): `twohand` shows `shot_line=gun`; the panel's opacity
and pointer in `panel shot` and a screenshot; SCAR B chambering through `reload`.

| # | Check | Look for | Result |
| --- | --- | --- | --- |
| H1 | One-handed, then two-handed: look down the sights at a mark ~15 m, fire single shots | Hits where the sight points, the dot on the sight line; recoil kicks the shots with the gun. `shot_line = controller` (Weapon page) for comparison | |
| H2 | AK-12 two-handed: hold the handguard, move around, aim | The hold stays until you open the grip; it no longer drops by itself | |
| H3 | All grab rings (`grab_debug = 1`) | ~30 % smaller, still easy to find | |
| H4 | SCAR-H: empty it, new magazine in, press B (reload button) | It chambers (no magazine drop); B again drops the magazine. The front-left handle zone: does the ring sit on the handle? | |
| H5 | Left arm: off hand to the belt, hip, chest, behind, across | The elbow and shoulder move smoothly, no flip or twist | |
| H6 | Settings panel | See-through backdrop, readable text; the beam and dot show where you point; `panel_opacity` changes it live | |
| H7 | Chambering sound | No synthetic first click; the rest as before | |

## Next — 2026-11-03 gun handling check (branch `test/vr-check-2026-11-03`)

Build: `test/vr-check-2026-11-03` = `feature/gun-feel` + `claude/vr-menu-open-f3e3d1` (not on `main`). Its
debug DLL is already deployed to `E:\EA\Battlefield 4`; start with `tools\dev-run.ps1 -NoBuild -Resume` or
BF4 directly, **not** the launcher's Update mod (that installs `main`'s DLL). The task brief and causes:
`docs/tasks/gun-handling.md`. Game `bfvr.ini` additions for this test: `physical_pad = 0`, `grab_debug = 1`.

Monitor-verified 2026-11-02/03 (fakehead + fakehand): drawn barrel, aim ray and shot direction within
0.02° at four poses; two-handed lock: grab, barrel 35.7°/15.1° towards an off hand moved 20 cm right/10 cm
up (geometry ~34°/14°), off hand drawn on the handguard, clean release; life-size factor 0.466 at the
rendered FOV; no phantom fire with `physical_pad = 0`. Unit tests for the lock, palm reach and zone
overlap. Not verifiable on the monitor: everything below that depends on real hands and depth.

| # | Check | Look for | Result |
| --- | --- | --- | --- |
| G0 | Before putting the headset on: is the Xbox 360 pad (or anything on XInput slot 0) still connected? `tools\pipe.ps1 status`, line `physical_pad` | On 2026-11-02 it reported `rt=124` at rest and fired the gun by itself. With `physical_pad = 0` it says `ignored`. If the gun ever fires on its own, that line first | |
| G1 | **Gun size and place.** Hold the gun one-handed, look at it, move and turn your hand | The gun is life size and in your hand: the drawn hand sits where you feel the controller, also when you hold it to the side or high. Then `gun_life_size` off on the panel (View page) for comparison: much larger and off the hand. Fine-tune with `gun_scale` (1 = life size) | |
| G2 | **Shots go where the barrel points** (`aim = controller` is now the default). One-handed, aim at a wall mark ~10 m, then ~25 m, fire single shots | Impacts on the green reticle and along the barrel line; the view never turns with the gun. `gun_pose = animated` (Weapon page) for comparison: the old pose, gun drifts off the line | |
| G3 | **Two-handed lock.** Off hand onto the handguard (anywhere ahead of the magwell, generous reach), squeeze the grip | The barrel snaps onto the line between your hands (no "suggestion"), your left hand is drawn wrapped on the handguard and slides along it, twisting the gun hand cants the gun, a short tracking blip keeps the hold, opening the grip releases within ~0.1 s. `two_hand_mode = soft` for the old behaviour | |
| G4 | Two-handed aim at 10 and 25 m | Hits where the barrel points; steadier than one-handed | |
| G5 | **Grab rings** (`grab_debug = 1`): look at the gun and your body | Rings at the magazine (below the magwell), the charging handle, the chest pouch, the handguard and five holsters, each as large as its reach, turning green when your hand is inside. Do they sit on the drawn gun? (If not: note where; that is the life-size factor) | |
| G6 | **Reload (AK-12):** empty it, off hand to the magazine (feel the tick), grip and pull, drop it; off hand to the chest (tick), grip, bring the new one near the magwell (it seats within ~14 cm), off hand to the handle (tick), grip and pull back ~4 cm | Every step works without hunting for the spot; the magazine grab no longer racks instead (handle and magwell are 3 cm apart on the AK: the nearest wins), and a hand on the handguard ahead of the magwell never grabs the magazine | |
| G7 | **Holsters:** gun hand over the shoulder / at the hip / on the chest, grip | Primary / secondary / knife each drawn on the first try (zones ~40-50 % larger, measured from the palm) | |
| G8 | Grenade: off hand to the belt (22 cm reach), gun hand pulls the pin (14 cm) | Easier to take and arm than before | |
| G9 | Menu: rows 0a and 0b below (both sticks; is ≡ the SteamVR dashboard?) | | |

**Hands and fingers** (branch `feature/hand-animation` = this test branch + finger poses; its DLL is the one
deployed since 2026-11-03 afternoon). Monitor-verified with fakehand and pipe `fingers`: open hand at squeeze
0, fist at 1; a magazine taken from the gun or the pouch sits in the drawn fist (fingers round its body, top
above the thumb) and goes in from there; the off hand opens inside a grab zone; racking holds the palm on
the handle and follows a 6 cm pull; the gun hand's index straightens off the trigger (8/10/6° bend) and goes
back on it at a touch (25/34/95°). Not visible on the monitor: the handle hold's angle, the bolt moving, the
slide-in of a seated magazine (0.1 s), how it all feels. Off switch: `finger_poses` (Body page). Game-only
checks to run first (handle spot, bolt movement, AR magazine) and the tuning knobs: `docs/tasks/hand-animation.md`.

| # | Check | Look for | Result |
| --- | --- | --- | --- |
| H1 | Off hand empty: squeeze the grip slowly, pull the trigger, rest the thumb on a button / lift it | Fingers close with the grip, the index with the trigger, the thumb with the touch; open hand when you let go, no snapping | |
| H2 | Gun hand: lift the index off the trigger, then touch and pull | The drawn index lies straight along the frame off the trigger and wraps it on a touch | |
| H3 | Reload: off hand to the magazine (the hand opens on the tick), grip and pull, look at it, turn it | The magazine is in the fist and turns with it; bring it to the magwell: it slides home (no pop) | |
| H4 | Rack: off hand to the handle, grip, pull back slowly | Palm stays on the handle while you pull; the bolt/handle comes back with it and snaps forward on release. Does the hold sit on the right spot (AK: right side)? | |
| H5 | Two-handed hold, grenade in the off hand, the knife | Support hand keeps the game's handguard grip; grenade in a closed fist; knife hand as before | |
| H6 | Off hand wrist (`off_hand_pose = neutral`, now the default): empty, then holding a magazine; compare with `animated` on the panel | The drawn hand is held like your controller (thumb on top); a held magazine stands upright in the fist and follows your wrist | |
| H14 | Physical stance (`physical_stance = 1`, Controls page; stand up straight when switching it on): crouch down, kneel, lie down, get up again; then crouch with the right stick while standing | Crouching ~30 cm lower crouches the soldier within ~½ s, lying down goes prone, getting up stands; the view follows your real head only (it must not drop twice); the game's own stance sound plays. The stick flick still crouches (now that its B press reaches the game) | |
| H13 | Cutscenes (`cutscene_view`, View page): play into a cutscene (or die once: the death fall counts), first with `screen`, then `look` | `screen`: the scene appears on the flat virtual screen and returns to 3D within ~½ s after it; `look`: the scene stays 3D and you can look around while it moves you. Note any moment that wrongly switches during normal play (aiming, firing, sprinting, turning) | |
| H12 | Magazine hold (palm sideways, `magazine_grip = auto`): take a magazine from the pouch and from the gun on the AK-12, then the SCAR-H; look at it and turn the hand | The hand holds it the way the game's own reload does (AK: fingers along its body, thumb across; AR: upright along the fingers, thumb on top), it turns with the wrist, and it still goes in when brought to the magwell. Compare the old fist hold: none (removed), so note anything that looks off | |
| H11 | Bolt pinch (`rack_grip = pinch`, Body page): off hand to the charging handle, grip, look at the hand, pull back and let go; compare `hook` | The knob sits between the thumb and the side of the index (a key pinch), the other fingers tucked; the hand stays on it through the pull. The fingers take the game's own rack pinch (thumb swung across onto the index). AK-12: the handle is now the measured spot, right side ahead of the magwell; the SCAR-H still uses the AR guess (top rear). Note whether the pinch sits on the knob and whether the thumb visibly misses it (monitor: ~4 mm each side) | |
| H10 | Magazine cup (`magazine_grip = auto`, Body page): take a magazine, turn the off hand palm up, look at it, turn back; then `cup` and `wrap` fixed | Palm up: the magazine slides onto the palm and stands on it, base plate in the hand, the fingers closing on its front (not through it); turning back puts it in the fist again without flicker near the switch. Note any finger that sinks into it or floats off it (pipe `fingers` prints each gap in mm) | |
| H9 | Torso (`torso_follow = 1`, Body page): hold the gun out in front, then turn your whole body 60-90° in the room without the stick; then keep the body still and only look 30° and 70° aside | Turning in the room: the shoulders come round with you within ~¼ s and the arms stay natural (not reaching across the chest). Looking 30° aside moves nothing; 70° drags the shoulders ~30°. Compare `torso_follow = 0` | |
| H7 | Recoil (`recoil = 1`): single shots, a long burst one-handed, then two-handed; then the same with the SCAR-H | The muzzle kicks up (and a little sideways) and back and settles in ~0.2 s, a burst climbs ~7°, two hands steady it; the SCAR-H kicks harder and settles slower than the AK-12; never sickening (the view does not move). Tune 0-3 on the Weapon page | |
| H8 | Rack in two stages: pull the handle back to the stop, hold, then let go | A tick and a slide sound at the rear stop (before you let go), the bolt's slam on release; releasing after riding the handle forward still chambers | |

## Next — everything since 2026-10-26, on `main`

Build: `main` (all branches merged 2026-10-27). New: two-handed grip (#29), shots converge on the barrel
line (#29), shoulder reach and arm stretch (#31), right stick turns under controller aim (#22) and turns
that no longer get dropped after stick use, in-VR settings panel (#7), the game's own reload sounds (#30),
melee, knife, use reach and C4 (#15), vignette (#10), walking where you face, aim reticle, haptics (#32),
the launcher's VR setup (#16).

Monitor-verified 2026-10-27 (fakehead + fakehand, no headset): the right stick snap-turns 30° in `look`
mode under controller aim; two-handed grab with no jump, the barrel and shot direction follow the off
hand (20 cm right = 35°), released cleanly, the grenade button never reached the game; the panel opens
and closes on a long Start, clicks change tabs and settings, a short Start opens the pause menu; arm
reach to a target 0.75 m ahead: `reach 1.60`, wrist 0.415 m ahead without fixes, 0.534 m with
`shoulder_reach`, 0.675 m with `arm_stretch` as well. Reload sounds verified by recording (row 10). What
needs the headset: feel, sizes, aim accuracy against real targets.

| # | Check | Look for | Result |
| --- | --- | --- | --- |
| 0a | **Opening the settings panel** (branch `claude/vr-menu-open-f3e3d1`). Load a level: below the view "Hold both sticks: VR settings" shows for ~8 s. Press both thumbsticks in and hold: a light tick on both hands at once, after ~0.5 s a stronger pulse and the panel opens 0.8 m ahead at eye height where you look. Point at Close at its bottom edge and pull the trigger (or hold both sticks again). Then a single stick click: sprint (left) and melee (right) still work, 0.1 s late; holding both never sprints or melees | Hint shows and goes; tick then pulse; panel in front at eye height, Close reachable with either hand; no sprint/melee from the chord. Pipe `panel` second line: `stick_menu=1 chord=… hint=…` | |
| 0b | **Is the ≡ button SteamVR's dashboard?** In a level, hold the left ≡ for ~1 s, let go; repeat twice. Then `tools\pipe.ps1 status` (line `xr_focus`) and the end of `bfvr.log` | If the SteamVR dashboard opens: the notice "SteamVR has the controllers: close its menu" shows below the view while it is open, `xr_focus` says `session=VISIBLE input_focus=0 … (SteamVR has the controllers)`, and the log has `theater: session FOCUSED -> VISIBLE: the runtime took the controllers … menu button held 0.xx s` for each press: hypothesis confirmed (the panel then only opens with both sticks). If the panel opens instead and `focus_losses` stays 0, the ≡ button works and the earlier flips had another cause: note what the log's `buttons=` and `held` values say | |
| 1 | `setting "set aim controller"`, right stick left/right | View snap-turns (also with `right_stick = look`); pipe `rightstick` shows `aim=controller (stick turns)` and `turns` counting up | |
| 2 | Reach both arms straight out; pipe `arms` | `reach` above 1 means the game's arms are too short; with `arm_stretch` 0.25 the hands should now stay on the controllers. The shoulder now reaches forward first (`shoulder_reach` 0.12 m), then the arm stretches (`arm_stretch` 0.25). Try `shoulder_reach` 0 / 0.12 / 0.2 and `arm_stretch` 0 / 0.25 in `tools\settings.ps1`: what looks natural? | |
| 3 | Rifle: left hand on the handguard, squeeze the grip | No jump at the grab; moving the left hand steers the barrel; letting go blends back within ~0.1 s; no grenade thrown (LB is held back while you hold the gun). Pipe `twohand`: `held=1` | |
| 4 | Two-handed, aim at a wall mark ~25 m away and fire (aim = controller) | Bullets hit where the barrel points; at 5–10 m they land a little off (try `aim_convergence`) | |
| 5 | One-handed, same | Same as 4 | |
| 6 | Holding two-handed, press the off-hand trigger (gun with a laser, light or magnifier) | Toggles once per press (`accessory_toggle = trigger`, the default); no zoom from that trigger while holding; grabbing anywhere on the handguard, rail included, never toggles | |
| 6b | Shoulders: `shoulder_width` 0, 0.05, 0.1 in `tools\settings.ps1` (`shoulder_offset` now ±0.5 m) | Which width makes the arms look natural? | |
| 7 | Reload and grenades while two-handed | Grabbing a magazine or a grenade releases the two-handed hold cleanly | |
| 9 | Hold the left menu button ~0.6 s (in a level and in the main menu) | The settings panel appears ahead of you; point with either controller, the trigger clicks -/+, toggles and tabs; Save says "saved to bfvr.ini"; Close or holding the button again closes it. A short press still opens the game's pause menu (on release). The game ignores the triggers while the panel is open. Monitor first: pipe `panel open`, `panel shot <file.bmp>` | |
| 10 | `reload = physical`: full reload while squeezing both grips as you naturally would (AK-12, then SCAR-H) | No spot/voice call-outs (`grips_to_game = 0`); the game's own sounds at each step (synthesised ones for other guns and the pouch): magazine out, magazine in, bolt when racking; nothing when taking or dropping. Do they sound right and on time? | |
| 11 | Panel (View page): `vignette` turning, then both; snap turn, smooth turn, stick aiming, walking, sprinting; also look around with your head only | The edge darkens while the game turns or moves you and fades within ~0.3 s; head movement alone never darkens it; the HUD stays readable. Try `vignette_strength` 0.4–1. Seated: recenter while seated | |
| 12 | Melee (`melee_gesture` on): thrust the gun hand quickly forward (like a punch or a bayonet jab), then aim, reload, draw from holsters and throw grenades normally | The knife stab plays on the thrust (monitor: slot 7 for ~0.3 s at 4.9 m/s); never while aiming, reloading, drawing or throwing. Too easy or too hard to trigger? (threshold 2.5 m/s forward) | |
| 13 | Stand still, turn your body to face sideways in the room, push the left stick forward (`move_direction` head, then body, then gun) | head: you walk where you face; body: the old way (sideways); gun: where the gun points | |
| 14 | `aim = controller`: the aim reticle (green dot with a dark ring) | Sits on the barrel line ~25 m out, about 1° wide, same depth in both eyes; bullets hit it at that distance. Size via `reticle_size` | |
| 15 | Knife out (chest holster or key 6, `knife_grip` on): point, twist and tilt the gun hand | The blade points where the controller points and turns with every wrist movement; the hand holds it as the game does (monitor: 70° up = blade up, 80° left = blade level to the left). Blade tilted up or down along the ray? Tune `knife_pitch` (30°). `knife_grip` off for comparison (the old, wrong angle) | |
| 15b | Use (`use_reach` on): at a dropped weapon, a door or a "use" prompt, reach the off hand out ahead (arm's length, below your eyes, away from the gun) and squeeze its grip; hold for pickups | The game's use (E) is held as long as the grip is: weapon picked up, door opened. Never while holding the handguard, a magazine or a grenade, or giving the squad signal (hand above the eyes). Monitor: reach+grip held E, near the gun = two-handed grab instead | |
| 15c | Gadget stash at the start: face the green crates, reach out and grip (use = E), point and click Gadget One/Two in its menu; draw the C4 from the belt, pull the off-hand trigger (throws a pack), step back, pull the gun-hand trigger (detonates); draw the M320 | Stash opens; the menu takes laser clicks; C4 thrown by the off hand and set off by the detonator hand; the detonator and the M320 turn with your hand. Monitor: all of this with pipe clicks and pad triggers | |
| 15d | Snap/smooth turn after using the right stick in a menu or in look mode; `neutral_pitch`; `aim = controller` | Turning keeps working (it stopped once the game had seen stick input; fixed 17c0b04) | |
| 17 | Haptics (#32): fire single shots and a burst one-handed, then two-handed; throw a grenade nearby; get shot | Each shot kicks the gun hand, scaled from the gun's own rumble data (both hands while holding the handguard); a nearby explosion rumbles both hands (game rumble, ~0.5 for 0.35 s on the monitor); getting hurt gives a hit pulse on both, stronger for bigger hits; no lingering buzz. Strength via `haptics_strength`; `haptics_game` off = none of this | |
| 18 | Haptics taps: draw from each holster, grab and seat a magazine, rack, take a grenade and pull the pin, grab the handguard, reach-and-grip use, a panel click | A short tap for each, on the right hand; none feel annoying or too weak. `haptics_gestures` off = no taps | |
| 16 | Panel toggles: every new feature off, then on again, live: `two_handed`, `arm_reach`, `accessory_toggle` off, `game_sounds`, `vignette` off, `melee_gesture`, `knife_grip`, `use_reach`, `c4_trigger`, `haptics`, `move_direction` body, `reticle`, `pause_flat`, `vr_panel` (desktop: `tools\settings.ps1`) | Each switches its feature off at once and back on, without a restart | |
| 8 | Vehicle with aim = controller | Right stick now turns the body, not the turret (known risk); report what happens | |

## 2026-10-26 late — feedback on `main` 22da57a (recorded afterwards)

The table below (row numbers of the "pair rendering with gun depth" checklist) was not filled in row by
row; the user's summary: "a lot of them seem to be better".

| Finding | Issue |
| --- | --- |
| Right stick left/right does not turn the camera. Likely cause found afterwards: with `aim = controller` the stick in `look` mode did nothing (fixed on `feature/two-hand-aim`) | #22 |
| No custom settings menu in VR: only `tools\settings.ps1` on the desktop exists | #7 |
| Physical reload: taking a new magazine and putting it in seemed to work; the sounds were way off | #14, #30 |
| The arms do not reach far from the body: hands, torso and gun look eerily close | #31 |
| Next priority (user): two-handed mode working, and the gun shooting where the barrel points | #29 |

## Next — pair rendering with gun depth (pending)

Build: `main`, Release
(`tools\dev-run.ps1 -Preset bf4-release -Resume`).
New since the last test: the gun and arms get their own eye offset in `pair` (first-person per-view
block, `re/notes/stereo-rendering.md`). Monitor: scope disparity −297 px at 64 mm (expected ≈ 273 px).

| # | Check | Look for | Result |
| --- | --- | --- | --- |
| 1 | Gun in `pair` | Gun at a believable distance in your hands, not in the wall/ground; no double image | |
| 2 | Close one eye, then the other, looking at the gun | Each eye sees it from its own side; brightness may differ a little (view-dependent shine), not a lot | |
| 3 | Arm IK + `pair` (`arm_ik = 1`) | Hands on the gun, sizes plausible, shoulders/chest position (note what to tune) | |
| 4 | Same in `aer` (`tools\pipe.ps1 stereorender aer`) | Which looks better, gun and world | |
| 5 | Resolution | Sharper than last time? `status` shows `back_buffer=2160x2160` if the fullscreen fix works | |
| 6 | `tools\pipe.ps1 rightstick turn` | Right stick left/right snaps 30°, down flicks crouch then prone, up flicks stand back up; no stick aiming | |
| 7 | Menus: point the right or left controller at the menu screen, pull its trigger | Cursor follows the ray, trigger clicks; the hand whose trigger was pulled last points | |
| 8 | `tools\pipe.ps1 setting "set hud_mode body"` | Squad/objective markers sit on their targets; crosshair centred; HUD corners out of the way (`hud_marker_distance`, default 10 m) | |
| 9 | `hud_mode = body`: look at the inside of your left forearm | Minimap appears on the forearm after ~¼ s, leaves when you look away (`hud_watch_width`) | |
| 10 | `hud_mode = body`: turn the gun sideways, look at it, hold still ~1 s | Ammo counter appears beside the gun, stays until you look away; never while aiming down the sights or sprinting | |
| 10b | `tools\settings.ps1`: toggle `hud_show_main/minimap/ammo/bottom`, both `hud_mode`s | Each piece disappears/returns at once; nothing else changes | |
| 11b | `setting "set aim controller"` | Bullets go where the right controller points, the view stays with the head; snap turn still turns the view. Expect: walking follows the gun direction, the HUD crosshair no longer marks the aim | |
| 11 | `tools\settings.ps1` with the headset on | Change gun_scale, hud_distance, ipd_scale live; `s` saves | |
| 11c | `setting "set dominant_hand left"` | Right stick moves, left stick looks/turns, left trigger fires, X/Y act as A/B; the gun arm follows the left controller; minimap on the right inner forearm. Known: the body is not mirrored yet, so the right arm reaches across to the left controller | |
| 13 | `setting "set reload physical"`, AK-12: empty it, grip the magazine with the left hand and pull, drop it; grip at the chest, bring the new one to the gun; grip the slide and pull back | Magazine follows the left hand and falls when dropped; new one seats at the magwell; rack chambers. Judge: grab distances (8 cm magwell, 7 cm slide, 20 cm chest), where the slide point sits (bone origin), magazine orientation in the hand | |
| 13b | Same with the SCAR-H (AR): fire dry, X, new magazine, X | Bolt locks open on empty, X drops the magazine, X after the new one chambers | |
| 14 | (`accessory_toggle = touch` only) Touch the optic (or the front of the rail) with the left hand and squeeze the grip | The accessory toggles once per squeeze (AK-12: the magnifier flips in/out); holding the gun's handguard elsewhere does nothing. Judge: the 7 cm touch radius, whether the rail point sits where lights/lasers are | |
| 15 | `setting "set grenade physical"`: left hand to the belt, grip; right hand grips next to the grenade and pulls away; throw with the left arm and let go (outdoors) | Grenade leaves the hand along the throw, no wind-up delay; slow release with the pin out drops it at the hand; release with the pin in puts it back. Judge: belt spot, pin distance, throw speed (`grenade_throw_scale`) | |
| 16 | Holsters (on by default): right hand over the right shoulder + grip; at the right hip + grip; on the left chest + grip | Primary / secondary / knife drawn within ~0.3 s; nothing happens at the aiming position. Judge: zone positions and sizes, looking over the shoulder (the zone should stay) | |
| 17 | Squad order (on by default), with enemies in view: raise the left hand ahead above your eyes and squeeze the grip | The squad engages (keyboard Q); the HUD's ENGAGE READY goes into cooldown. Holding the gun's handguard while aiming never gives the order | |
| 18 | Buttons: right B with physical reload; left X | B drops the AR magazine / chambers after a new one (bolt catch); X opens the pause menu, shown flat in front of you with the pointer beam; RESUME with the pointer | |
| 19 | Magazine in the hand (AR from below, AK by the sides), turning with the left hand; seat it, then rack (AK: grip above the magazine and pull back; AR: B, or grip the rear top and pull) | Magazine sits in the hand, not floating; rack spot findable; sounds: cloth at the chest, click on insert, rack on chambering | |
| 20 | Look down; arms reach; gun size | Legs hidden; hands reach where the controllers are (`arm_length` now 1.15); gun size (`gun_scale`) | |
| 12 | Sizing (#24), `tools\settings.ps1`: tune `gun_scale`, `shoulder_offset` (x right, y up, z back), `ipd_scale` until the gun and arms feel right in `pair`; `s` saves | Note the values that feel right | |

## 2026-10-26 — pair rendering (both eyes from one game frame)

**Result (user, headset):** "looked amazing, depth and motion was great"; no judder or eye strain; the
gun sits "somewhere in the ground/wall" (expected gap). AER was not compared this time. Status during
the test: session VISIBLE, ~110 fps, 0 rejected pairs, render size still 1280×720 (windowed game).

Build: `feature/render-quality`, **Release** (`tools\dev-run.ps1 -Preset bf4-release -Resume`). The game's
`bfvr.ini` already has `stereo_render = pair` and `research = 1` (added 2026-10-25 for this test).
Switch modes live from a terminal: `tools\pipe.ps1 stereorender aer` / `pair`.

What changed: in `pair` every game frame renders the world twice: left eye, then right eye with its own
camera constants. Both images go to the headset together, so the eyes no longer differ by a frame.
**Known gap:** the gun and arms are still identical in both eyes (no depth on the gun) in `pair`; in `aer`
they have depth as before. Monitor checks without the headset (fake 64 mm head): world disparity
−5 to −8 px at 1280×720, correct direction and order by distance; gun +2 px (flat).

| # | Check (Steam Link, stay in BF4) | Look for | Result |
| --- | --- | --- | --- |
| 1 | Level in `pair`, look around slowly | World has normal depth, no double images; walls/props feel the right size | |
| 2 | Walk/strafe and turn quickly in `pair`, then `stereorender aer` and repeat | Is `pair` steadier (less shimmer/doubling while moving) than `aer`? | |
| 3 | Gun in `pair` | Expected wrong: gun looks flat/far away or "inside" the wall. How bad? | |
| 4 | Comfort over 2–3 minutes in `pair` | Eye strain, judder | |
| 5 | Pipe `status` in `pair` (read by Claude afterwards) | `fps`, `eyepass eye_ms`, `eye_readback_ms`, `eye_rejected=0` | |
| 6 | Menus, loading screen, HUD layer in `pair` | Theater screen as before, HUD layer still sharp | |

## 2026-10-25 night — render quality (feature/render-quality), batched test pending

Already reported by the user (build with `render_size = 2160x2160` and `keep_focus`): "the changes
helped a ton, partly on performance but for the look definitely". Remaining complaint: the image still
feels like a low-FOV, curved flat screen stretched around the head (see re/notes/stereo-rendering.md).

User verdict on the view: "not that bad a viewing experience", but the monitor shows much more
horizontally than the headset, and the gun looks small while the tracked arms look big. Items 2–4, 6, 7
were not reported.

Checklist (game `bfvr.ini`: `render_size = 2160x2160`, `hud_layer = 1`, `keep_focus` default on):

| # | Check | Result |
| --- | --- | --- |
| 1 | HUD floats as a flat panel ahead (minimap bottom-left, ammo bottom-right), sharp and fully inside the view; nothing in the lens corners | ✅ "really cool, I see it way in front of me" |
| 2 | HUD size/distance comfortable? (`hud_width` 1.3 / `hud_distance` 1.5; try 1.1 or 1.5) | |
| 3 | World image has no HUD burnt in; no black or flickering HUD | |
| 4 | Menus, pause menu and loading screens still on the theater screen | |
| 5 | Squad/objective markers on the HUD panel: acceptable, or distracting (they no longer sit on their targets)? | ❌ a little distracting; squad triangles are off from the people they belong to |
| 6 | Click to the desktop or open the SteamVR dashboard for ~10 s, come back: image stays sharp, no 720p drop, game kept running | |
| 7 | fps in a firefight (pipe `status`): stays at 120? | |

## 2026-10-25 evening — arm IK (feature/arm-ik)

| Item | Result |
| --- | --- |
| Hands follow the controllers (position, rotation) | ✅ |
| Gun stays in the right hand and follows it | ✅ for guns ("flawless") |
| Knife and other odd items | ❌ hand rotation wrong (animated hand is not a gun grip) |
| Shoulders and arms look right | not judged (viewpoint); looked fine |
| GPU driver reset 15:00:51 (nvlddmkm 153; Helldivers 2 crashed at the same moment) | froze BF4 once; not reproduced |

## 2026-10-25 retry — menus, controllers, sweeps

| Item | Result |
| --- | --- |
| VR session via Steam Link | ✅ came up immediately; no SteamVR restarts (mod no longer launches SteamVR) |
| Controllers without the Cepter | ✅ game polls the virtual pad continuously (feel in game: to confirm) |
| Main menu | ✅ on the theater screen; ❌ loading screens still head-tracked (level path empty while loading) → next: stereo only while a campaign level is loaded |
| View-crop sweep (h/v: 1, 0.9, 0.8, 0.7/0.75, 0.6/0.68) | black side lines at steps 4-5; HUD corners visible from step 3; bottom cut-off line at step 5 → **saved view_crop_h 0.78, view_crop_v 0.72** in the game's bfvr.ini |
| "Weird depth of field" at steps 4-5 | open: Weapon DOF is off and motion blur 0 in the profile; suspects: 1280x720 drop (BF4 lost focus at 14:16:38 even in windowed mode), post AA (AntiAliasingPost 3) |
| Gun-size sweep (1, 0.85, 0.7, 0.55) | no answer yet |
| Stall/freeze while Claude had focus and the headset was hidden | the mod's VR step was idle: BF4 itself stops presenting in the background; not a mod deadlock |
| Profile | FieldOfViewVertical 105, WeaponDOF 0, MotionBlur 0, AntiAliasingPost 3, AntiAliasingDeferred 2, ResolutionScale 1 |

## Next focus (user, 2026-10-25)

1. **Resolution:** remove the blur at crop ~0.78/0.72 (render larger than the 1920x1080 window; keep it independent of focus).
2. **Hand tracking:** controllers drive the first-person arms (IK) via the arm-pose hook.

## 2026-10-25 afternoon — combined build (main + feature/virtual-pad)

Reported per-eye FOV (logged): left eye left −54°, right 40°, up 44°, down −55°; right eye mirrored.
The user's visible area in the Quest (SteamVR "VR View" with the visible region marked) is roughly
40° to the sides, 44° up and ~30° down: about two thirds of the rendered image per axis.

| Item | Result |
| --- | --- |
| Gun stays on the aim while the head looks around (morning) | ✅ all five checks passed |
| Sharpness | ❌ blurry; low resolution; very blurry during fast head turns (one or both eyes) |
| Cause 1: rendered FOV wider than visible | measured above → **view crop** added (pipe `viewcrop <h> <v>`, config `view_crop_h/v`); estimate h ≈ 0.61, v ≈ 0.68; not tuned yet |
| Cause 2: 720p drops | exclusive fullscreen falls back to a 1280×720 window on focus loss (Claude, SteamVR overlay) → BF4 switched to windowed (`GstRender.FullscreenEnabled 0`, profile backed up in `re/`); windowed starts at 1280×720 in the menu but goes to 1920×1080 in the level |
| Cause 3: fast head motion | alternate-eye rendering (60 Hz per eye, #6) and Steam Link encoding |
| Gun size | feels too large/close ("head further back or higher FOV") → `gun_scale` sweep pending |
| HUD | unreadable: the bottom HUD lies outside the visible area (#3: needs its own layer) |
| 7 fps while the headset is off / dashboard open | fixed (hidden frames completed on the waiter thread) — but see freeze below |
| SteamVR kept restarting while connecting Steam Link | caused by the mod (instance retries launch SteamVR) → fixed: never launch SteamVR, wait for it |
| Entering via Link Dash (Oculus) | menu upside down, controllers dead → use Steam Link only |
| Controllers without the Cepter | ✅ game reads the virtual pad continuously after the "present while idle" fix; in-game feel not tested yet |
| **Main menu glued to the view** | ❌ the menu was drawn head-tracked over the whole view (only the middle third readable) instead of the theater screen: menu-camera detection missed → fix: front end always theater |
| **Freeze after reconnecting Steam Link during level load** | ❌ SteamVR showed "BF-VR Loading", desktop black; BF4 stopped presenting (presents frozen, session state 3) until closed → suspected concurrent SteamVR calls from the waiter (hidden frames) and Present (swap chain/copy); fix: serialise XR calls, plus a stage readout in `status` |

Test procedure notes: stay in BF4 (don't click out to Claude or the desktop, don't reconnect
Steam Link mid-run); connect Steam Link first, then start BF4.
