# Headset test backlog

Quest 3 via Steam Link, SteamVR OpenXR, RTX 5070. Build: `main`, **Release** (Debug runs at ~14 fps).

All headset checks are in one list. Each check has a fixed ID (`AIM-4`), so feedback can name it:
"AIM-4 still low by a hand's width, GUN-2 works, HUD-6 already done". After a session I update the
Status and Last result columns and turn problems into fixes. The history of earlier sessions is in
git and in the session log at the end.

| Status | Meaning |
| --- | --- |
| `open` | Built and checked on the monitor, never judged in the headset |
| `retest` | A headset report led to a change; check the change |
| `done` | Confirmed in the headset |

Before each session:
- `bfvr.ini` keeps `physical_pad = 0`: a real Xbox pad on XInput slot 0 rests at RT 124 and fires the gun.
  `tools\pipe.ps1 status` says `physical_pad … ignored`.
- `grab_debug = 1` shows every grab ring (turn it off on the panel when it gets in the way).
- Tap both stick clicks briefly whenever something looks wrong: "Marked moment N" saves the state and a
  screenshot to `bfvr_marks\`. "Mark 3 was the gun floating" is enough.
- Stay in BF4: don't click out to the desktop or reconnect Steam Link mid-run (see Watch list).
- Maps of the controls and grab zones: [docs/img/quest3-controls.svg](img/quest3-controls.svg),
  [docs/img/grab-zones.svg](img/grab-zones.svg).

## Next session: run order

In order of value; stop wherever time runs out. The view comes first, then what is drawn in it, then the gun.

| Block | IDs | Time |
| --- | --- | --- |
| Start | CTRL-1 | 1 min |
| A. View | VIEW-1, VIEW-2, VIEW-3, VIEW-4, VIEW-5, VIEW-6 (panel **Save** the chosen values) | 10 min |
| B. HUD | HUD-1, HUD-2, HUD-3, HUD-4 | 5 min |
| C. Sights and shots | AIM-1, AIM-2, AIM-3, AIM-5, AIM-6, AIM-8, AIM-7, VIEW-9, AIM-4 | 15 min |
| D. Gun handling | GUN-1, GUN-2, GUN-3, GUN-5, GUN-4, GUN-6, HAND-8, CTRL-4, FEEL-3, GRN-2, GRN-3 | 20 min |
| E. If time is left | FEEL-1, FEEL-2, CTRL-9, CTRL-10, GUN-10, HAND-9, CTRL-2, CTRL-3 | 10 min |

## View and performance

| ID | Status | Check | Look for | Issue | Last result |
| --- | --- | --- | --- | --- | --- |
| VIEW-1 | open | Step `view_crop_bottom` (View page) up from 0: 8, 12, 16, looking ahead and at your feet | The highest value with no black band at the bottom of the lens. Is the picture sharper (wall text, world markers)? Monitor: 14 gives ~28 % more vertical sharpness | #1 | |
| VIEW-2 | open | At the chosen crop: look around, aim, reload, look down at the chest rig | The gun sits in your hands, horizons stay level, no tilt or swimming when you turn your head | #1 | |
| VIEW-3 | open | Walk and sprint along a corridor with `steady_walk` on, off, on | On: the world glides with no side-to-side rocking, gun and hands stay put. Off: the old 12 cm sway | | |
| VIEW-4 | open | Walk while aiming down the sights, and while looking sideways | No drift of the sight against the target beyond your own movement; nothing lags when you stop | | |
| VIEW-5 | open | `foveation = light` (default), look around; switch it off and on | The edge looks the same; no shimmer or blocks where you look straight ahead; 90 fps holds | | |
| VIEW-6 | open | `foveation` medium, then strong | The strongest level whose blockier edge (shiny floors, highlights) you don't notice | | |
| VIEW-7 | open | Main menu, pause menu, loading screens | All on the flat theater screen, none glued to the view | | 2026-10-25: loading screens were head-tracked |
| VIEW-8 | open | Open the SteamVR dashboard (or click to the desktop) for ~10 s, come back | Still sharp, no 720p drop, the game kept running | | |
| VIEW-9 | open | Frame rate in a firefight and while scoped (pipe `status`) | Smooth at 90 Hz. Monitor: 94-116 fps in the checkpoint room, 87-100 scoped. `scope` off (Weapon page) to compare | | |
| VIEW-10 | open | Play into a cutscene (or die: the fall counts) with `cutscene_view = screen`, then `look` | `screen`: the scene on the flat screen, back to 3D within ~½ s; `look`: stays 3D, you can look around. Note any wrong switch during play | | |
| VIEW-11 | open | `vignette` (View page, off by default): turning, moving, both; then head movement only | The edge darkens only while the game moves or turns you and fades in ~0.3 s; try `vignette_strength` 0.4-1 | #10 | |
| VIEW-12 | retest | Look at the gun in `pair` rendering, close one eye then the other | The gun at a believable distance in your hands, not in the wall or ground; no double image; each eye sees it from its own side | #24 | 2026-10-26: gun "somewhere in the ground/wall"; fixed since, "a lot seem better" (not checked row by row) |
| VIEW-13 | done | Pair rendering: depth, motion, comfort | | #6 | 2026-10-26: "looked amazing, depth and motion was great", no judder or eye strain |
| VIEW-14 | done | Sharpness with `render_size = 2160x2160` and `keep_focus` | | #1 | 2026-10-25: "the changes helped a ton" |
| VIEW-15 | done | Side and top crop sweep | | #1 | 2026-10-25: `view_crop_h 0.78`, `view_crop_v 0.72` saved |

## HUD

| ID | Status | Check | Look for | Issue | Last result |
| --- | --- | --- | --- | --- | --- |
| HUD-1 | open | `hud_show_crosshair` (HUD page) on and off | The game's crosshair appears and goes; nothing else in the middle of the HUD changes | #28 | |
| HUD-2 | open | Fight with `hud_show_hit_markers`, `hud_show_damage`, `hud_show_score` off in turn | Off: no hit marker, no red damage arcs, no "+100". On: they come back | #28 | |
| HUD-3 | open | At the gadget stash with `hud_show_prompts` off, then `hud_show_objectives` off | Prompts off: no "open" prompt, while E or the use reach still opens it. Objectives off: note what goes | #28 | |
| HUD-4 | open | `hud_show_markers` off | The names and triangles over your squad are gone; note whether the objective marker or grenade warnings go too | #28 | |
| HUD-5 | retest | `hud_mode = body` (default): squad triangles and the objective marker | Each sits on its target; the crosshair is centred; the HUD corners are out of the way (`hud_marker_distance`) | #27 | 2026-10-25: on the flat HUD panel the triangles were off their people, "a little distracting" |
| HUD-6 | open | Look at the inside of your left forearm | The minimap appears there after ~¼ s and leaves when you look away (`hud_watch_width`) | #26 | |
| HUD-7 | open | Turn the gun sideways, look at it, hold ~1 s | The ammo count appears beside the gun, stays until you look away; never while aiming or sprinting | #26 | |
| HUD-8 | open | Toggle `hud_show_main`, `hud_show_minimap`, `hud_show_ammo`, `hud_show_bottom` and both `hud_mode`s | Each piece goes and returns at once, nothing else changes | #26 | |
| HUD-9 | open | HUD size and distance (`hud_width` 1.3, `hud_distance` 1.5; try 1.1 and 1.5) | Comfortable to read; no HUD burnt into the world, no black or flickering HUD | #3 | |
| HUD-10 | done | HUD as a sharp flat layer ahead | | #3 | 2026-10-25: "really cool, I see it way in front of me" |

## Sights and shots

| ID | Status | Check | Look for | Issue | Last result |
| --- | --- | --- | --- | --- | --- |
| AIM-1 | open | Raise the gun until one eye looks through the optic | Within ~2.5 cm of the line the game aims down the sights (lens clears, walking slows), the dot sits in the optic, gun size unchanged; lowering returns to the hip. `sight_ads` off to compare | #12 | |
| AIM-2 | open | Auto-ADS while moving | ADS slows walking; note when it engages by accident (gun near the face) | #12 | |
| AIM-3 | open | Aim down the red dot (magnifier folded) | The gun has depth in ADS (it was flat before) | #24 | |
| AIM-4 | retest | Single shots one-handed, then two-handed: through the sight at ~10 and ~25 m, and from the hip on the green reticle | Hits where the sight or reticle points; recoil moves the shots with the gun. `shot_line = controller` to compare; `aim_convergence` if close shots land off | #12, #29 | 2026-11-03: shots hit the dot, but the dot followed the pointing ray, not the drawn barrel (now `shot_line = gun`); sights were "broken beyond belief" |
| AIM-5 | open | Flip the AK's magnifier in (accessory toggle), aim through it | The lens shows the world ~1.7x with a red dot; only the aiming eye; the picture sits inside the lens rim; no black scope overlay | | |
| AIM-6 | open | Shots through the magnifier at ~25 m | Hits at the lens's red dot | | |
| AIM-7 | open | Move the eye closer to, further from and off the magnifier's centre | The picture stays on the lens; the magnification follows the lens size | | |
| AIM-8 | open | Bright lights or a window through the magnifier; pan across an edge | No dark or smeared ring inside the lens edge | | |
| AIM-9 | open | Recoil (`recoil = 1`): single shots, a long burst one-handed, then two-handed; AK-12 then SCAR-H | The muzzle kicks up and back, settles in ~0.2 s, a burst climbs ~7°, two hands steady it; the SCAR kicks harder; the view never moves. Tune 0-3 | | |

## Gun handling

| ID | Status | Check | Look for | Issue | Last result |
| --- | --- | --- | --- | --- | --- |
| GUN-1 | open | Hold the gun one-handed, look at it, move and turn the hand | Life size and in your hand, also held to the side or high. `gun_life_size` off to compare; fine-tune `gun_scale` | #24 | |
| GUN-2 | retest | AK-12 two-handed: off hand on the handguard + grip, move, aim, slide along the handguard | The barrel snaps onto the line between your hands, the hold stays until you open the grip (~0.1 s release); no grenade thrown | #29 | 2026-11-03: fine on the SCAR, kept dropping on the AK |
| GUN-3 | retest | All grab rings (`grab_debug = 1`) on the gun and body | ~30 % smaller than before, still easy to find, sitting on the drawn gun | #14 | 2026-11-03: liked, but asked for 30 % smaller |
| GUN-4 | open | AK-12 reload: off hand to the magazine (tick), grip and pull, drop it; off hand to the magazine pouch, grip, bring it to the magwell (seats within ~14 cm); off hand to the handle, grip, pull back ~4 cm | Every step works without hunting for the spot; grabbing the magazine never racks; a hand on the handguard never takes the magazine | #14 | |
| GUN-5 | retest | SCAR-H: empty it, new magazine in, press B; then look at the handle ring | It chambers (no magazine drop); B again drops the magazine; the front-left handle ring sits on the handle | #14 | 2026-11-03: B dropped the new magazine instead of chambering |
| GUN-6 | open | Chest rig (grab-zones.svg): gun hand over the right shoulder (primary), right hip (secondary), high left chest (knife); either hand over the left shoulder / left hip (gadgets, after the stash) | Each drawn on the first try; looking down at a pouch doesn't move it | #14 | |
| GUN-7 | open | While holding two-handed: grab a magazine, then a grenade | Each releases the two-handed hold cleanly | #29 | |
| GUN-8 | open | Holding two-handed, pull the off-hand trigger (gun with a laser, light or magnifier) | Toggles once per press; no zoom from that trigger; grabbing the handguard never toggles | #29 | |
| GUN-9 | retest | Knife out (chest holster): point, twist and tilt the gun hand | The blade points where the controller points and turns with the wrist; tune `knife_pitch` (30°) | #15 | 2026-10-25: the hand rotation was wrong on the knife (now `knife_grip`) |
| GUN-10 | open | Gadget stash: reach and grip at the green crates, laser-click Gadget One/Two; draw the C4, off-hand trigger throws, gun-hand trigger detonates; draw the M320 | The stash opens, the menu takes clicks, C4 thrown and set off, detonator and M320 turn with your hand | #15 | |
| GUN-11 | open | A vehicle with `aim = controller` | Report what happens (known risk: the stick may turn the body, not the turret) | | |

## Grenades

| ID | Status | Check | Look for | Issue | Last result |
| --- | --- | --- | --- | --- | --- |
| GRN-1 | open | Off hand to the high right chest + grip, gun hand pulls the pin, throw outdoors; then a slow release with the pin out; then let go with the pin in | Leaves the hand along the throw, no delay; slow release drops it at the hand; pin in puts it back. Tune `grenade_throw_scale` | #14 | |
| GRN-2 | open | Take a grenade, look at it, turn the hand | A green M67 in the fist, fuse out of the thumb side, following the hand's tilt. Note if it sits off the palm or the fuse points wrong | #14 | |
| GRN-3 | open | Pull the pin, then throw; also put one back | The ring goes at the pull; the grenade vanishes as it leaves the hand (the game's takes over); no glow in dark rooms; put back: it disappears | #14 | |

## Hands, arms and body

| ID | Status | Check | Look for | Issue | Last result |
| --- | --- | --- | --- | --- | --- |
| HAND-1 | open | Empty off hand: squeeze the grip slowly, pull the trigger, rest and lift the thumb | Fingers close with the grip, the index with the trigger, the thumb with the touch; no snapping | #11 | |
| HAND-2 | open | Gun hand: lift the index off the trigger, then touch and pull | The drawn index lies straight along the frame, wraps the trigger on a touch | #11 | |
| HAND-3 | open | Take a magazine from the pouch and from the gun (AK-12, then SCAR-H), look at it, turn the hand (`magazine_grip = auto`) | AK: fingers along its body, thumb across; AR: upright along the fingers; it turns with the wrist and still seats | #14 | |
| HAND-4 | open | Holding a magazine, turn the palm up, then back | Palm up: it stands on the palm, fingers on its front (not through it); back to the fist without flicker | #14 | |
| HAND-5 | open | Off hand to the charging handle, grip, pull back slowly and let go (`rack_grip = pinch`) | The knob between thumb and index, the hand stays on it, the bolt comes back and snaps forward. AK: right side ahead of the magwell | #14 | |
| HAND-6 | open | Two-handed hold, a grenade in the off hand, the knife | Support hand keeps the handguard grip; grenade in a closed fist; knife hand as the game draws it | #11 | |
| HAND-7 | open | Off-hand wrist (`off_hand_pose = neutral`): empty, then with a magazine; compare `animated` | The drawn hand is held like your controller; a held magazine stands upright and follows the wrist | #11 | |
| HAND-8 | retest | Left arm: off hand to the belt, hip, chest, across, behind | Elbow and shoulder move smoothly, no flip or twist; the shoulder beside the neck. `body_shoulders` off to compare | #11 | 2026-11-03: the left shoulder side glitched and twisted |
| HAND-9 | open | Reach both arms straight out (pipe `arms`); try `shoulder_reach` 0 / 0.12 / 0.2, `arm_stretch` 0 / 0.25, `shoulder_width` 0 / 0.05 / 0.1 | Hands stay on the controllers; which values look natural | #31 | 2026-10-26: hands, torso and gun looked "eerily close" to the body |
| HAND-10 | open | Hold the gun out, turn your whole body 60-90° in the room; then only look 30° and 70° aside (`torso_follow`) | The shoulders follow within ~¼ s; 30° head turn moves nothing, 70° drags them ~30° | #13 | |
| HAND-11 | open | Look down | Legs hidden; arm length and gun size plausible (`arm_length` 1.15) | #13 | |
| HAND-12 | open | `physical_stance = 1` (stand straight when switching it on): crouch, kneel, lie down, get up; then crouch with the stick | The soldier follows within ~½ s; the view follows only your head; the stick flick still crouches | #33 | |
| HAND-13 | open | `dominant_hand = left` | Sticks, triggers and buttons swap; the gun follows the left controller; the minimap on the right forearm | #9 | |
| HAND-14 | done | Hands follow the controllers, the gun stays in the right hand | | #11 | 2026-10-25: "flawless" for guns |

## Controls and menus

| ID | Status | Check | Look for | Issue | Last result |
| --- | --- | --- | --- | --- | --- |
| CTRL-1 | open | Tap both sticks once in a level; hold both for the panel; sprint (hold left) and melee (right) | Tap: a pulse and "Marked moment N", no sprint or melee. Hold: the panel, no mark. Sprint and melee still work | | |
| CTRL-2 | open | Hold both stick clicks ~0.5 s | A tick on both hands, then a pulse, the panel opens 0.8 m ahead where you look; Close works with either hand; the hint "Hold both sticks" shows for ~8 s after loading | #7 | |
| CTRL-3 | open | Hold the left ≡ ~1 s, three times; then pipe `status` (`xr_focus`) | Does the SteamVR dashboard take it (notice "SteamVR has the controllers"), or does the panel open? A short press opens the pause menu | #7 | |
| CTRL-4 | retest | Settings panel: tabs, -/+, toggles, Save, `panel_opacity` | See-through backdrop, readable text; a beam and dot where you point; Save says "saved to bfvr.ini"; the game ignores the triggers while it is open | #7 | 2026-11-03: "looks great", asked for see-through with a pointer |
| CTRL-5 | open | Right stick left/right, then flick down twice and up; repeat after using a menu | 30° snap turns; down crouches then goes prone, up stands; turning keeps working after menus | #22 | 2026-10-26: the stick didn't turn under controller aim (fixed since) |
| CTRL-6 | open | Turn your body sideways in the room, push the left stick forward with `move_direction` head, body, gun | head: you walk where you face; body: the old way; gun: where the gun points | #5 | |
| CTRL-7 | open | Main menu: point either controller at the screen and pull its trigger | The cursor follows the ray, the trigger clicks; the last hand that clicked points | #23 | |
| CTRL-8 | open | Left X in a level | The pause menu, flat in front of you with the pointer beam; RESUME with the pointer | #23 | |
| CTRL-9 | open | Thrust the gun hand quickly forward; then aim, reload, draw and throw normally | The knife stab plays on the thrust, never during the other actions. Too easy or too hard? (2.5 m/s) | #15 | |
| CTRL-10 | open | At a dropped weapon, a door or a prompt: reach the off hand ahead (below your eyes, away from the gun) and grip; hold for pickups | The game's use (E) is held as long as the grip; never while holding the handguard, a magazine or a grenade | #15 | |
| CTRL-11 | open | With enemies in view: raise the off hand ahead above your eyes and grip | The squad engages (ENGAGE READY goes into cooldown); never while aiming two-handed | #15 | |
| CTRL-12 | open | Panel: switch each feature off and on live (`two_handed`, `arm_reach`, `vignette`, `melee_gesture`, `knife_grip`, `use_reach`, `c4_trigger`, `haptics`, `reticle`, `pause_flat`, …) | Each switches off at once and back on without a restart | #25 | |
| CTRL-13 | done | Controllers without a physical pad | | #4 | 2026-10-25: the game reads the virtual pad continuously |

## Haptics and sound

| ID | Status | Check | Look for | Issue | Last result |
| --- | --- | --- | --- | --- | --- |
| FEEL-1 | open | Single shots and a burst one-handed, then two-handed; a grenade nearby; get shot | Each shot kicks the gun hand (both hands two-handed); explosions rumble both; hits pulse both; no lingering buzz. `haptics_strength` | #32 | |
| FEEL-2 | open | Draw from each holster, grab and seat a magazine, rack, take a grenade, pull the pin, grab the handguard, use, a panel click | A short tap for each; none annoying or too weak | #32 | |
| FEEL-3 | retest | Chambering sound | No synthetic first click; the rest as before | #30 | 2026-11-03: "great", except the start of the chamber sound |
| FEEL-4 | open | Pull the handle back to the stop, hold, then let go | A tick and slide sound at the rear stop, the bolt's slam on release; riding it forward still chambers | #30 | |
| FEEL-5 | done | Reload sounds (the game's own) | | #30 | 2026-11-03: "great" |

## Watch list

Not checks, but things to note if they come back.

| What | Known |
| --- | --- |
| Freeze after reconnecting Steam Link during a level load | 2026-10-25; XR calls serialised since. Connect Steam Link first, then start BF4 |
| Entering through Link Dash (Oculus) | Menu upside down, controllers dead: use Steam Link only |
| GPU driver reset (nvlddmkm 153) | Once, 2026-10-25, while another game crashed too; not reproduced |
| BF4 stops presenting while its window is in the background | The game's own behaviour; keep BF4 focused |
| The gun fires by itself | A real pad on XInput slot 0: check `physical_pad` in pipe `status` |

## Session log

| Date | Build | Summary |
| --- | --- | --- |
| 2026-11-03 evening | `main` 246f2fb | Shots hit the dot but followed the pointing ray; reload sounds great; two-handed fine on the SCAR, dropped on the AK; rings wanted 30 % smaller; left shoulder glitched; SCAR B dropped the magazine; panel liked; sights "broken beyond belief". All fixed or rechecked by AIM-4, GUN-2, GUN-3, GUN-5, HAND-8, CTRL-4, FEEL-3 |
| 2026-10-26 late | `main` 22da57a | "A lot of them seem to be better". Stick didn't turn (#22), no VR menu (#7), reload sounds off (#30), arms too close (#31); next priority two-handed and shots along the barrel (#29) |
| 2026-10-26 | `feature/render-quality` | Pair rendering "looked amazing"; gun sat in the wall (no gun depth yet); ~110 fps |
| 2026-10-25 night | `feature/render-quality` | 2160x2160 render "helped a ton"; still felt like a curved flat screen; HUD layer liked, squad markers off-target |
| 2026-10-25 evening | `feature/arm-ik` | Arm IK flawless for guns; knife hand rotated wrong |
| 2026-10-25 | main + virtual pad | Blurry (rendered FOV wider than visible, 720p drops, alternate-eye at 60 Hz per eye); crop saved at 0.78 / 0.72. Visible area in the Quest: ~40° to the sides, 44° up, ~30° down (rendered: left −54° / right 40°, up 44°, down −55°) |

Checks removed in the 2026-11-04 clean-up, because a newer check or the current default replaced them:
the AER vs pair comparison (pair is the default), the `gun_scale` sweep (GUN-1, life size), the old
holster layout (GUN-6), the old AK/SCAR reload steps and magazine-in-hand rows (GUN-4, GUN-5, HAND-3),
`accessory_toggle = touch` (not the default), the black scope overlay row (AIM-5), `tools\settings.ps1`
in the headset (the in-VR panel), the "aim = controller" expectations (now the default), and the
"weird depth of field" at crop steps 4-5 (those crops are not used).
