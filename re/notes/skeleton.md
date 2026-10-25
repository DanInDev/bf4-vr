# First-person skeleton and arm pose — BF4 build 179547

Status: **injection point proven (2026-10-25).** Editing a bone of the local soldier's first-person
local pose before the engine converts it moves the drawn arm (left hand pushed 0.3 m along the
forearm: glove and forearm moved up to the sight). Groundwork for arm IK (#11).

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

## Next for IK

Read the shoulder in model space, solve two-bone IK (upper arm, forearm) to the controller pose,
write local rotations for Arm/ForeArm/Hand. Needs the model-space frame relative to the
first-person camera (+0xED0) to map controller poses in.
