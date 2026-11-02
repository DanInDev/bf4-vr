# Task: VR-native gun handling (and an obvious way to open the VR menu)

Status: **built and monitor-verified (2026-11-03), headset test pending** (docs/HEADSET-TESTS.md, rows
G0-G9; build `test/vr-check-2026-11-03`). Phases 0-5 done; phase 4 by the static FOV result (45 deg
foreground) instead of calibration. Open: the support point comes from the animated left hand (eased,
not from a rest pose); grab radii and life size await the headset. Original feedback 2026-11-02: the gun
shoots where the stick aimed, the two-handed grip only "suggests" a direction, grab spots are too small,
the size looks wrong, the in-VR panel never opens.

Goal: the gun behaves like a modern VR shooter's. **The drawn gun is the truth:** bullets leave along
the barrel you see, the support hand locks onto the handguard and steers it, and every grab spot is
easy to hit without looking twice.

## What the code does today (causes found while planning)

| Symptom | Cause | Where |
| --- | --- | --- |
| Shots go where the stick aimed | The game's `bfvr.ini` has no `aim` line, so `aim = head` (the default): the aimer follows the view and stick, and the controller only moves the drawn gun | `settings_store.cpp:73`, `E:\EA\Battlefield 4\bfvr.ini` |
| Even with `aim = controller` the drawn barrel and the shots can differ | The drawn gun is the game's *animated* hip pose turned by the controller (`goal.hand = F * aim * F^-1 * animated hand`), so the idle pose's own angle, sway and bob stay in the drawn barrel. The shots use the raw controller aim ray. The two only agree when the animation's gun points exactly along the camera | `arm_pose.cpp:~436` |
| Two-handed feels like a suggestion | At the grab the hand line is stored *in gun axes* and only its later change turns the gun (`from_to(remembered, line)`). An off hand 10 cm below the barrel keeps the gun 10 cm off forever, and the drawn left hand stays at the controller instead of on the handguard | `two_hand.cpp:66-84`; arm_pose never uses `IK_Joint_LeftHand` |
| Magazine, slide and grenade grabs barely work | Distances are measured from the **wrist** (`weapon_points.hand` = wrist goal, 6 cm behind the palm), and the radii are small: magwell 8 cm, slide 7 cm, chest 20 cm, pin 10 cm, holsters 12–15 cm, handguard 12 cm | `reload_hands.hpp:61-63`, `holsters.hpp`, `grenade_hands.hpp`, `physical_reload.cpp:345` |
| Gun size looks off | The gun and arms are drawn by the foreground pass with **their own, unknown FOV** (re/notes/first_person.md "Open"). `gun_scale` scales gun *and* arms about the view centre, so any value other than 1 pulls the hands off the controllers | `camera_hook.cpp:378` |
| VR panel never opens | Last headset log: the session went FOCUSED (5) → VISIBLE (4) about ten times in 6 minutes. On Quest through SteamVR the left ≡ button is the SteamVR dashboard button, so holding it takes focus away; an unfocused session reads no input, and the 0.6 s long press never completes. **Hypothesis**: confirm in the headset | `controller_actions.cpp:85`, `vr_panel.cpp:167` |

## Plan

Each phase is its own commit on `feature/gun-feel` (phases 0–4) or `feature/vr-menu-open` (phase 5),
with a live setting, a panel row, an ini line, unit tests for engine-free logic, and a monitor check
(fakehead + fakehand) before it counts as done.

### Phase 0: measure first (monitor, no headset)

- Pipe `gunlook`: controller aim ray, drawn barrel (`Wep_Root` +z) and muzzle point, the shot direction
  sent to the aimer, the angle between each pair, palm vs wrist distance, two-hand state.
- Run it with `fakehand` at five poses (ahead, 40° left/right, 30° up/down), one- and two-handed, in
  both aim modes. Record the angle errors in `re/notes/aim.md`; they are the baseline for phases 1–2.
- Fire at a wall at 5, 10 and 25 m (screenshot: impact vs reticle).

### Phase 1: bullets leave the drawn barrel

1. `aim = controller` becomes the default (settings default, `tools/bfvr.ini.example`, launcher VR setup).
   `head` stays available for comparison.
2. **Canonical gun pose:** like the knife (`arm_pose.cpp`, `knife_in_hand`), set the hand goal so that
   `Wep_Root`'s barrel axis lies exactly on the controller's aim ray and the authored grip
   (`IK_Joint_RightHand`) sits at the controller's grip. The animated idle angle, sway and bob no longer
   tilt the drawn gun. Setting `gun_pose = aim|animated` keeps the old pose for comparison.
3. **One source of truth:** shots, the reticle and haptics use the barrel read back from the rig after the
   solve (`weapon_points.root`), with the controller ray as fallback when the rig is stale. Convergence
   at `aim_convergence` stays (bullets leave the camera), now measured from the muzzle point.
4. Check scripted sections and vehicles: where the aimer is not authoritative, fall back cleanly to
   the head (no fighting the game).

Done when `gunlook` shows < 0.5° between the drawn barrel, the aim ray and the shot direction at all
five poses, and impacts land on the reticle at 10 and 25 m.

### Phase 2: two hands like a VR shooter

1. **Hard lock:** while held, barrel direction = the line from the gun hand's grip to the off hand's
   palm, corrected by the gun's own grip→handguard vector (`IK_Joint_RightHand` → `IK_Joint_LeftHand`
   in gun axes) so the barrel, not the hand line, points where the hands say. No stored offset.
2. **Roll from the gun hand:** twist about the barrel comes from the gun hand's up vector projected onto
   the plane across the barrel (canting works, no flips when the hands line up).
3. **Off hand drawn on the gun:** the left arm's IK goal snaps to the nearest point on the handguard
   segment, with the hand rotation of `IK_Joint_LeftHand`. It slides along the handguard, and a short
   blend at grab and release avoids pops.
4. **Easier grab, sticky hold:** grab within 18 cm of the handguard (was 12), anywhere from the magwell
   to the muzzle. Once held it stays held until the grip opens (no distance break), with a short
   release grace for tracking blips.
5. Light smoothing (one-euro filter) on the two-handed direction, setting `two_hand_smoothing`, default
   low. Steadies aim at range without lag.
6. `two_hand_mode = lock|soft` keeps today's behaviour selectable.

Unit tests: `tests/two_hand_test.cpp` grows cases for the lock, roll, slide and grace.
Done when `gunlook` shows the barrel within 0.5° of the corrected hand line and the drawn left hand on
the handguard at every pose.

### Phase 3: grab spots you can hit

1. **Measure from the palm:** all grab tests (magazine, slide, chest pouch, grenade belt and pin,
   holsters, use reach) use the controller's grip point, not the wrist goal.
2. **Wider radii (settings, panel rows):** magwell 8 → 15 cm, slide 7 → 12 cm, chest pouch 20 → 30 cm
   (and the whole off-hand chest/hip side counts), grenade pin 10 → 14 cm, holsters +50%.
3. **Magnetic insert:** a magazine within reach of the magwell and moving towards it seats itself
   (snap and click), instead of needing an exact hit.
4. **Rack:** grab anywhere on the rear top of the gun within reach; pull 4 cm (was 5) along the barrel.
5. **Hints:** a light, repeating tick while the hand is inside a zone and the grip is open, so the hand
   finds the spot without looking. `grab_debug` (off by default) draws every zone as a dot on the
   reticle layer, for the headset session and monitor screenshots.

Unit tests: `reload_test`, `use_reach_test`, a new magnet test. Done when a full reload, a rack, a draw
from every holster and a grenade pin pull succeed with fakehand placed 10 cm off each spot.

### Phase 4: life-size gun

1. **Find the first-person FOV** (research, static first): candidates `FPSCameraData.FOV` (+0x108),
   `GameRenderSettings.FovMultiplier` (+0x4C), `WeaponZoomLevelData.RenderFov`, or the projection built
   on the render thread (re/notes/first_person.md). REA/Ghidra on the dump for writers of the foreground
   projection, then a live write test.
2. If found: drive it from the world FOV every frame (`fp_fov = match`), so 1 game metre in the
   foreground equals 1 m in the world, and `gun_scale = 1` means life size with the hands on the controllers.
3. If not found tonight: **calibrate** instead. With fakehand at known positions, screenshot and compare
   where the drawn hand appears with where the world projection puts the controller. Set `gun_scale`
   to the value that makes them meet, and change `gun_scale` to scale about the gun hand, not the
   view, so the hands stay on the controllers.

Done when the drawn hand lands within ~1 cm (projected) of the controller at three depths.

### Phase 5: open the VR menu for sure (`feature/vr-menu-open`, can run in parallel)

1. Second opener that SteamVR never takes: **hold both thumbstick clicks ~0.5 s** (unused by the game
   together). The left ≡ long press stays.
2. On level load, a hint on the HUD layer for ~8 s: "Hold both sticks: VR settings". A haptic tick when
   the hold starts counting and a pulse when the panel opens.
3. Log every focus change, and while unfocused say so on the theater/HUD ("SteamVR dashboard open: the
   game gets no input"), so a lost button press is visible.
4. The panel opens at eye height ~0.8 m ahead of where you look, with Close at the top as well.

### Phase 6: review and the headset batch

- After each phase: `/code-review` of the diff (high effort), both presets warning-free, ctest.
- A fresh `docs/HEADSET-TESTS.md` section ordered by gameplay impact, with a ready-made test ini
  (`aim = controller`, `grab_debug = 1`), and a short A/B for each new mode (`gun_pose`, `two_hand_mode`, `gun_scale`).
- Post monitor results on #29 / #12 / #14 / #24 / #7.

## After the gun (only if time remains)

Next priorities from the open issues, in order: #31 arm reach feel, #27 HUD markers on targets,
#22 stick stance flicks, #9 mirrored body for left-handers.

## Parallel work

One BF4 instance serves every live test, so the game-bound phases (0–4) run one after another in one
session. Two pieces are independent of it:

- **Phase 5 (menu)** in its own worktree session: code and unit tests; its single live check is queued
  into the main session's game time.
- **Phase 4.1 (first-person FOV, static research)**: a read-only REA/Ghidra session on the dump, writing
  `re/notes/first_person.md` with candidate addresses; the live write test happens in the main session.
