# Headset test log

Quest 3 via Steam Link, SteamVR 2.18.4 OpenXR, RTX 5070. Newest first.

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
| 14 | Touch the optic (or the front of the rail) with the left hand and squeeze the grip | The accessory toggles once per squeeze (AK-12: the magnifier flips in/out); holding the gun's handguard elsewhere does nothing. Judge: the 7 cm touch radius, whether the rail point sits where lights/lasers are | |
| 15 | `setting "set grenade physical"`: left hand to the belt, grip; right hand grips next to the grenade and pulls away; throw with the left arm and let go (outdoors) | Grenade leaves the hand along the throw, no wind-up delay; slow release with the pin out drops it at the hand; release with the pin in puts it back. Judge: belt spot, pin distance, throw speed (`grenade_throw_scale`) | |
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
