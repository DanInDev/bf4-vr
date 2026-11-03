# First-person skeleton and arm pose — BF4 build 179547

Status: **arm IK works in the headset (2026-10-25).** The first-person arms follow the Quest
controllers (two-bone IK in the pose-conversion hook, `src/body/arm_pose.cpp`, `arm_ik.cpp`); the
gun follows the right hand. Injection point proven earlier the same day (left hand pushed 0.3 m
along the forearm moved the glove and forearm up to the sight).

## Where the pose lives

| Path | Content |
| --- | --- |
| `[0x14230F260]` local `ClientSoldierEntity` | |
| soldier `+0x1E8` | first-person `ClientAntAnimatableComponent` (skeleton `Animations/Skeletons/Warsaw1pAntSke01`, 179 bones) |
| soldier `+0x1F0` | third-person body component (`WarsawAntSke01`, 213 bones) |
| component `+0x128` | `Animatable` (typed) |
| Animatable `+0x230` | `SkeletonAsset` (+0x10 name, +0x18 BoneNames, +0x28 Hierarchy, +0x30 LocalPose, +0x38 ModelPose; arrays keep their count at data−4) |
| Animatable `+0x48` | **local pose**: QuatTransform[bones] (0x20 each: translation xyz + scale, then rotation quaternion xyzw), relative to the parent bone |
| Animatable `+0x50` | model pose (same format, rig space: shoulders ~1.9 m, hands 0.25–0.55 m ahead) |
| Animatable `+0x40` | another per-bone buffer (matrix rows; probably skinning data) |

First-person bone indices: CameraBase 4, CameraJoint 5, Spine 8–10, LeftShoulder 45, LeftArm 46,
LeftForeArm 48, LeftHand 51, LeftWeapon 52, Neck 81, Head 83, RightShoulder 90, RightArm 91,
RightForeArm 93, RightHand 96, RightWeapon 97 (read live from BoneNames; tool: tools/find_typed.py).

## Who writes it (hardware write-watch, 200 writes/s at 200 fps, worker threads)

- local pose: `0x14071BCD2` via `0x140712996` (animation evaluation)
- model pose `0x140716550` and the +0x40 buffer `0x140716946`, both inside the conversion
  `0x140716050` (prologue `40 53 48 83 EC 20 45 33 C9 48 8B D9 44 38 89 6A 01`), called from `0x140712EC8`
- The object passed to `0x140716050` is the Animatable − 0xE0: +0x120 → Animatable +0x40,
  +0x128/+0x140 → local pose, +0x130/+0x138 → model pose.

## Hook (`src/body/arm_pose.cpp`)

MinHook on `0x140716050`; recognises the local soldier's first-person pose object by its buffer
pointers and edits the local pose before calling the original. Research command
`bone <index> <dx> <dy> <dz>` adds a translation to one bone every frame (`bone -1` = off).

## Rig facts (live, 2026-10-25)

- **Hierarchy** (`SkeletonAsset +0x28`, `Array<Int32>` parent indices, parent < child): Reference 0 ->
  AITrajectory 1 -> CameraBase 4 -> CameraJoint 5; 1 -> SpineX 43 -> SpineXLeft 44 -> LeftShoulder 45 ->
  LeftArm 46 -> LeftArmRoll 47 -> LeftForeArm 48 -> LeftForeArmRoll 49 -> LeftHand 51 (-> LeftWeapon 52,
  fingers 53-76); right side mirrored: SpineXRight 89 -> RightShoulder 90 -> RightArm 91 -> RightArmRoll 92
  -> RightForeArm 93 -> RightForeArmRoll 94 -> RightHand 96 (-> RightWeapon 97). ForeArmRoll1 50/95 are
  siblings of the hands.
- **The gun does not hang from the hand:** `Wep_Root` 126 is a child of SpineX 43 and parents every
  weapon bone (Trigger, Slide, Mag, Scope, Belt, Bipod, `IK_Joint_LeftHand` 149, `IK_Joint_RightHand`
  150, ...). `Wep_Aim` 161 is another child of SpineX.
- **Spaces:** the local pose's root (Reference) carries the **world** transform, so forward kinematics
  over the local pose gives world positions. The engine's model pose (+0x50 / object +0x130) is the
  same with the root left out (our FK matches it to 0.00000 m; reading the quaternions as conjugates
  is off by 0.4-1 m). Quaternions are Hamilton xyzw, model = parent * local, translation scaled by the
  parent's scale (all 1 seen).
- **First-person camera = CameraJoint + aim pitch:** the world first-person camera (view params
  `+0xF10`) has exactly the CameraJoint position and yaw, but the joint carries no pitch:
  camera = joint rotation * 180 deg about y (joint x points left, z forward; OpenXR x right, -z
  forward) * pitch about x, pitch = soldier `+0x4DC` (matched the camera to 1e-6 rad).

## Arm IK (`arm_ik = 1`, default on, also in release builds)

1. The frame waiter records the predicted display time; the Present thread locates the grip and aim
   poses (OpenXR LOCAL) right after `xrSyncActions`, under the XR-call lock, and publishes them
   (`stereo::hand_sample`).
2. In the conversion hook (local soldier's first-person rig, matched by its local-pose pointer,
   refreshed every 250 ms; bones found by name) the IK'd bones are first restored to the animation
   if it has not rewritten them since our last write (the conversion can run on a pose the
   animation skipped).
3. Rig frame F = CameraJoint (world) with the rotation above. Wrist goal = F * (grip position + grip
   rotation * `arm_ik_wrist`); hand goal = F * aim * F^-1 * animated hand rotation (the controller turns
   the authored hand, so pointing straight ahead keeps the authored grip); elbow pole = shoulder +
   F * `arm_ik_pole`.
4. Two-bone solve (`ik::solve_arm`): upper arm and forearm by shortest arcs to the law-of-cosines elbow,
   half the wrist-twist change to the forearm roll bone, then the hand rotation. Out of reach: arm
   straight towards the goal (rig arm about 0.56 m shoulder to wrist).
5. `Wep_Root` is set to right hand * (animated hand^-1 * animated Wep_Root), so the gun keeps its
   authored grip in the moved hand (`arm_ik_weapon`).

Pipe: `arms` (rig, controller validity, FK check, camera joint, per-arm shoulder/elbow/wrist/goal and
wrist in camera axes), `armik 0|1`; research: `hand <l|r> <x y z> [yaw pitch] | off` (synthetic
target relative to the tracked head), `armaxes <q>`. Unit test: `bfvr_arm_ik_test`.

## Verification (2026-10-25)

| Test | Result |
| --- | --- |
| FK vs engine model pose (root removed) | 0.00000 m |
| `hand r 0 0 -0.5` before the pitch fix | hand above the target; camera pitch -7 deg equals soldier +0x4DC -> added to F |
| Synthetic targets (r 0.1 -0.15 -0.4, l -0.2 -0.25 -0.35) | both wrists on their goals; gun in the right hand after the Wep_Root fix (before: gun stayed at its animated place) |
| `hand r ... 40 0` / `... 0 35` | gun turns left / points up |
| Headset (Quest 3, Steam Link) | both hands follow the controllers in position and rotation; guns track the right hand "flawlessly". Knife and other odd items: hand rotation wrong. Shoulders/arms not judged (viewpoint) |

## Open

- Other items (gadgets): their animated hand pose is not a gun grip, so "aim turns the animated hand"
  gives the wrong rotation; needs per-item handling like the knife below.

## Knife (`knife_grip`, 2026-10-27, monitor)

The knife (`U_Knife`, slot 7) hangs from `Wep_Root` like a gun, but its frame differs: the gun's
Wep_Root sits at (0,-1,0,0) in camera axes when aimed ahead (+z along the barrel, +y up); the knife's
idle sits at (-0.5,-0.5,0.5,0.5) and its blade lies about 30 degrees off -z towards +y, edge on +y,
saw back on -y (found by turning a fake controller 70 deg up and 80 deg left/right and reading the
profile). With the knife out, the hand goal is chosen so that Wep_Root = F * aim * Rx(knife_pitch) *
(0,0,1,0), and the hand = that * (animated hand^-1 * animated Wep_Root)^-1: the blade follows the
controller ray, edge down, the hand keeps the knife's authored grip. Verified: pitch 70 up = blade up,
80 left = blade level to the left. `arms` prints `knife=` and `wep_root_in_camera`.
- Bullets still go where the aimer points, not the gun (decoupled aiming, #12).
- Left hand on the weapon (`IK_Joint_LeftHand`) for two-handed aiming; shoulder (clavicle) reach;
  tune `arm_ik_wrist` / `arm_ik_pole` in the headset.
- With `gun_scale` != 1 the drawn arms are scaled about the view and no longer line up with the controllers.

## Fingers (`finger_poses`, 2026-11-03, monitor)

Every finger is `<Left|Right>Hand<Finger>0..4` under the hand bone (Index, Middle, Ring, Pinky: 0 =
metacarpal, 4 = tip end; Thumb1..4), left 53-76, right 98-121 (`tools/skeleton_probe.py --match LeftHand`
lists them with the live local pose). Joints 1-3 bend about their local **+z on both hands** (the right
hand's quaternions come with w < 0: same rotation). Live bends on the AK-12: left hand on the handguard
62/45/43 deg (middle 1-3), right hand on the pistol grip 83/56/67, its index 25/34/95 (on the trigger).
The thumb bends at Thumb2/Thumb3; Thumb1 swings it across the palm and is left as animated.
SkeletonAsset +0x30 is not a QuatTransform bind pose (values read as unit axes); not needed.

`src/body/finger_pose` replaces each joint's twist about z (keeping its spread) with a bend from an
open/fist range, blended with the animation per finger; `arm_pose` applies it after the arm solve with
the same "restore what the animation did not rewrite" memory as the arms. A held magazine is placed in
the drawn fist (grasp frame from Index1, Pinky1, Middle1, Middle3, Thumb3) and published back
(`weapon_points.held_magazine`), so the reload inserts what is drawn. Racking moves `Wep_Slide` (child
of `Wep_Root`) back along its local -z by the pull.

## Torso (#13, 2026-11-03, monitor with fakehead yaw + fakehand)

- The first-person clavicles hang off the CameraJoint, which under controller aim follows the aimer,
  i.e. the gun: the rig's shoulder line already turns with the gun's yaw (in the rendered frame it
  faces -turn.yaw, `stereo::first_person_view_turn`). Pointing a one-handed gun 60 deg right twisted
  the whole shoulder line 60 deg right with the body facing ahead.
- `body/torso` estimates the real torso yaw (head within 40 deg of it, both hands' midpoint when out in
  front, 0.25 s easing); `arm_pose` turns both clavicles about a vertical axis 10 cm behind the head
  by torso - shoulder yaw before the arm solve (`torso_follow`, pipe `arms` prints `torso_deg`).
- Measured (reach = shoulder-to-goal over arm length): everything ahead 0.70/0.72; body and gun turned
  50 deg: torso 47.7, unchanged arms (the gun had turned the shoulders already); gun 60 deg right
  one-handed, body ahead: torso 0, right reach 0.81 -> 0.76 (shoulders stay with the body).

## Contact grips (2026-11-03, monitor)

- Fingertips: `<Hand><Finger>4` for all five (Thumb4 included); used as the finger's end point.
- Hand bone +y is the palm on both hands: rolling the fake left controller -90 deg (pipe `fakehand`, 11
  numbers: ... left yaw pitch roll) gives palm . up = 0.94, +90 gives -0.94, 0 gives 0 (palm sideways).
- Cup on the AK-12 (magazine box 3 x 19 x 7.5 cm, base 2 cm above the hand bone's axis): middle and ring
  touch at curl 0.54 / 0.51 (0 mm), index and pinky pass beside the 3 cm magazine (8 / 11 mm) and are held
  at 0.64 by the coupling, the thumb closes fully 16 mm off the back face.
