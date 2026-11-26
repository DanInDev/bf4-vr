# Ladders (#134)

BF4 build 179547, measured live on 2026-11-25/26 at SP_Shanghai "Reach the Hotel" (the alley's wall ladder in front
of the checkpoint) with the mod's pipe, plus REA/Ghidra and `tools/disasm.py` on `re/dumps/bf4-179547-mem.exe`.
Addresses are in [`../symbols/bf4-179547.md`](../symbols/bf4-179547.md) ("Ladders"). The feature built on this is
`src/weapon/ladder_climb` (engine-free, tested) and `src/weapon/ladders` (the game side).

## Prior work

- **The Great Destroyer** (BF3 VR, same Frostbite generation, code may be reused): no ladder or climbing code
  (`src/` tree at its 2026-11-19 commit has no ladder, climb or rung file; its notes list ladders as wanted, "only
  ladders, no ropes"). Nothing to reuse.
- **Overlord** (design only, GPLv3): `docs/vr-ladders.md`. Pulling the controllers moves the body the other way;
  two held hands average their targets instead of doubling the movement; adding or releasing a hand rebases the
  others so the body never jumps; walking input is suppressed while a hand holds; releasing the last hand returns to
  the game's own physics; a grounded player at the bottom just stands. Their rungs come from the ladder mesh's
  horizontal edges (a mesh reader we do not have). Taken over: the averaging, the rebase, release = the game's
  own state (BF4 lets you hang), the bottom handled by the game.

## How BF4 climbs

- The soldier's character physics (`ClientSoldierEntity+0x1B8`, `CharacterPhysicsEntity`) has a state object at
  `+0x78`. Its `+0x20` is the `CharacterStateType`: 0 OnGround, 3 **Climbing** (enum from the type registry: 1
  Jumping, 2 InAir, 4 Falling, 7 AnimationControlled, ...). Diffed on and off the ladder: this word is the only
  small int that changes with it.
- **Getting on**: walking into the ladder (left stick forward, facing it within `LadderAcceptAngle` 33 degrees,
  `CharacterPhysicsData+0x58`; pitch limit -10 at `+0x5C`) puts the soldier on it at once; the game also climbs on a
  little by itself (0.4-0.6 m) while the stick stays forward.
- **Climbing**: the left stick's forward/back only, no inertia: the speed jumps to the stick's value the frame it
  changes and stops the frame it is released. At the default 2 m/s:

  | stick | 0.3 | 0.5 | 0.7 | 0.9 | 1.0 |
  | --- | --- | --- | --- | --- | --- |
  | m/s | 0.01 | 0.6 | 1.25 | 1.9 | 2.0 |

  Down is the same. Fitted: `v = speed * min(1, 1.6 * (|s| - 0.3))` (`ladder::climb_speed`).
- **The speed** is the climbing state's pose velocity: `SoldierEntityData+0x118` (CharacterPhysicsData, player:
  `Gameplay/Soldiers/DefaultSoldierPhysics_SP`) `+0x20` States array, the state whose vtable is `0x141DB1528`
  (ClimbingStateData), its `+0x10` PoseInfo array, pose `+0x14` Velocity = 2.0. **Read live**: a poke to 4.0 climbed
  0.4 m per 0.1 s at once; restored to 2.0. The mod raises it to 4 m/s only while hands climb (a strong pull is
  followed), the stick alone keeps the game's speed.
- ClimbingStateData's other fields (live): LateralInputScale 0, DownAngleLimit 90, DropOffAngle 30,
  AttractionVelocity 1, PushAwayVelocity 5, ClimbHeightOffset 0, **ClimbOffVerticalDistance 1.65 /
  ClimbOffVerticalTime 0.5, ClimbOffHorizontalDistance 0.85 / ClimbOffHorizontalTime 0.5**: at the top the game lifts
  the soldier 1.65 m and moves him 0.85 m over the edge by itself.
- **Getting off at the bottom**: climbing down into the ground ends the state (3 -> 0); a stick still held back then
  walks backwards (a test walked 3.3 m off). The mod drops its stick the frame the state leaves 3.
- On the ladder the first-person rig's world transform (`+0x1E8` -> `+0x90`) reads all zeros; the rig is still
  evaluated (the camera joint moves, `arms` conversions count). The soldier's pitch copy `+0x4DC` is held at 0.
- While on it, the character position is at `[CharacterPhysicsEntity+0x138]+0x1A0` (Vec3), 0.45 m above the ground
  at the bottom (64.655 against the alley's 64.205), x/z snapped to the ladder's centre line, 0.435 m out from it.

## The ladder the soldier is on

The state object holds it while climbing (copied there every frame by a prediction restore, below):

| State object | Content | Shanghai's ladder |
| --- | --- | --- |
| `+0xD0` | Vec4 foot: the ladder entity's origin | (171.5197, 63.1151, -52.6987) |
| `+0xE0` | Vec4 out of the wall, towards the climber | (-1, 0, 0) |
| `+0xF0` | float length of the rails | 10.3875 |

## The level's ladders

- `ClientLadderEntity` exists in the type registry but **no live instance** in Shanghai (heap scan of its vtable
  found one word in a table, not an object). The ladders are **`ClientStaticModelEntity`s** (vtable `0x141D358F0`)
  carrying a **`ClientLadderComponent`** (vtable `0x141D3DAE0`, 0x38 bytes); 2 in this part of Shanghai.
- Component `+0x08` -> the owner's component block (entity `+0x1B0`, entries of 0x20: component, ...) whose `[0]` is
  the entity. Entity `+0x280`: `LinearTransform` right, up, forward, origin (rows of 4 floats, again at `+0x2C0`).
  The forward axis is the climbing state's "out", the origin its foot (identical floats). Shanghai: (171.52, 63.115,
  -52.70) facing -x, and (186.08, 63.115, -49.32) facing +z.
- The model's local box (rails): x -0.317..0.291, **y 0.127..10.3875**, z -0.038..0.034; shared by both ladders (same
  model, `0x1737ABE30`, reached from the second ladder's components). Not at a fixed offset of the entity, so the mod
  takes the length from the climbing state and assumes the model's 10.39 m before that.
- The mod finds them with a background scan of committed private read-write memory for the component vtable (8-byte
  aligned words, the `server_soldier` pattern), at each new soldier and every 60 s, plus every ladder the soldier
  climbs onto. Pipe `ladder` lists them, `ladder scan` scans now.

## The rungs

- **No collision**: the game's ray cast (`raycast`, flags 0x34) straight down through the rung column hits the alley
  floor (64.205); horizontally it hits the wall (x 171.849) behind the ladder. So the rungs were measured on the
  picture.
- Two screenshots from 2.2 m and 2.6 m with the camera's world matrix and projection (`RenderView+0x40`, `+0x320`,
  focal 2.4142 = 45 degrees): the yellow rungs' row centres between the rails, a ray per row, and the one vertical
  plane both views agree on: **x 171.495** (rms 0.6 mm over 6 rungs), 2.5 cm out from the foot's plane. Heights
  65.071, 65.368, 65.666, 65.964, 66.261, 66.559: **0.2978 m apart**; in the ladder's frame the centres are at
  **y = 0.166 + k * 0.2978** (the lowest one at the model box's bottom, 0.127 + half a rung), k = 0..34.
  `ladder::rung_model` holds these; other ladder models are not measured (a different model would show its rungs off
  by up to half a spacing).

## Climbing by hand on the monitor (2026-11-26)

Debug build, headless (~3.7 fps), fake head and hands (`fakehead 0.064`, `fakehand`, `pad ... 0200` = right grip held),
Shanghai's alley ladder. Pipe `ladder` throughout.

- **Getting on by reach**: the right palm 4 cm from a rung, squeeze: `grabs=1 mounts=1`, state 3 within 0.4 s, the
  hold on the measured rung (65.663 against 65.666 from the pictures).
- **Pull**: the hand 30 cm down, the view 66.126 -> 66.429 (target 66.426); the controller's world height stayed at its
  rung while the body rose past it.
- **Hand over hand**: the left on the rung above (66.557), the right on the one below (65.961), the right let go: the
  view did not move (66.427); the left pulled 30 cm: 66.730.
- **Both hands open**: state 3, the view held 2 s (BF4 lets you hang).
- **Down and off at the bottom**: three pushes of a rung, each one rung (66.77 -> 66.47 -> 66.17), each re-grip on the
  next rung down; on the third the soldier stood (state 0, the view 65.883 against 65.885 before), no stick, no
  backward walk.
- **All the way up and off at the top**: 24 one-rung cycles (+0.30 m each) to the top rung (73.406 = rung 34 as
  modelled); pushing it down past the chest, the game's climb-off lifted the view 73.75 -> 75.31 and the soldier stood
  on the roof (state 0, 74.80). With the final build the stick stayed in its rest band through the climb-off.
- **Turned 30 degrees away** (the game took him on so after a walk-on): before the aim fix a pull took him down and off
  (66.18 -> 65.99, state 0); after it, the same reach-mount and pull rose 0.303 m.
- The drawn fist closes round the rung (thumb over it, fingers under, along its axis); the arm reaches up to it.

What the monitor taught (each in the code):

- **Drive the view, not the character**: the character position read from the present thread jumps by a metre while
  the game places him (prediction replay), and the mount lifts the view 0.6 m against 0.2 m of it. The mod feeds back
  the tracking origin's world height (`weapon_points.world_origin`), and never falls back to the character position (at
  4 fps the arm points were older than 250 ms on some frames and the two sources alternated, 73.19 / 74.82).
- **Getting on, the game moves the body for up to a second** (up 0.9 m and back, and 0.4 m in to the ladder): the hands
  wait until it stands still for 0.25 s (1.5 s at most), then close on the rung nearest each controller.
- **The game's own moves** (the climb-off at the top) are taken along: the body moving other than the last two frames'
  sticks asked, summed over 0.15 s, past 6 cm, moves the hands' anchor instead of being fought (the first build pushed
  the stick full down against the climb-off for three frames).
- **DropOffAngle**: BF4 reads the climb stick relative to the soldier's facing and drops him off when it is 30 degrees
  or more off the ladder. In VR the facing follows the body round the room, so while the soldier is on a ladder (or
  walking onto one) the camera hook turns the game's aim into the ladder (`ladder_aim_world`); the view is the mod's own.
- The soldier's yaw copies (`+0x34C`, `+0x4D8`) read 0.000 off the ladder in these runs even through turns; nothing
  uses them. The walk-on pushes the stick straight ahead.

Not checked on the monitor: **getting on at the top** (walking onto the ladder from the roof missed it twice, the
soldier ended beside it or fell; the reach from the roof cannot reach the top rung standing), other ladder models.

## Dead ends

- Watching writes to the state object's ladder foot (`watch`) only finds `0x140B46355` in `0x140B46272`: a copy of a
  whole saved state (prediction restore, every frame, 150 writes in 5 s), not the detection.
- The foot's 12 bytes are in ~65 places of the heap (the prediction history), so a value scan leads nowhere; two of
  the hits sat in the ladder entity itself, which is how the entity was found.
- `0x140B4A300` (caller of `CharacterPhysicsEntity::contactRayCast` `0x140B40950`) is the climbing state's tick (it
  changes the state to 0, 4, 6, 8 through `0x1412E1F80`); the enter test was not traced further.

## Open

- Other ladder models (rung spacing, the top's hook rails: a hand cannot take the rails above the top rung yet).
- Getting on at the top by hand (the top rung is at the roof's foot level; a real climber kneels or takes the hooks).
- The background scan took 0.27-0.48 s after each load here; until it has run a reach from the ground finds no ladder
  (walking into it works, and adds it).
- The headless runs are ~4 fps; the feel at 90 fps (lag, the follow rate) is for the headset (CLIMB-1/2/3).
