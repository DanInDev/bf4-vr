# View angles (yaw/pitch) — BF4 build 179547

Status: **mapped and verified by writing (2026-10-25).** Writing the aimer's yaw/pitch turns the
rendered camera by exactly that angle and the value persists (the game integrates input on top).

## Chain

```
[0x14230F260]                         local ClientSoldierEntity
  +0x570                              ClientSoldierWeaponsComponent
    +0x28 bit 2 ? [+0x8E0] : [+0x8D8] -> +0x8  u32 current weapon index
    +0x890 / +0x898                   begin / end of ClientSoldierWeapon* array
    +0x8F8                            fallback aiming pointer (used when the indexed weapon has none)
    +0x8C8                            object used when no weapon aims (functions 0x140F8B500 yaw, 0x140F8B4F0 pitch)
  weapon (ClientSoldierWeapon) +0x4988 aiming simulation (untyped)
    aiming +0x10                      **aimer** (untyped): +0x14 yaw, +0x18 pitch (radians) — the source
    aiming +0x18 / +0x1C              aimer yaw/pitch + sway/recoil (+0x28 / +0x2C), rewritten each frame
  soldier +0x4D8 / +0x4DC             copy of aiming +0x18/+0x1C, rewritten each frame
  soldier +0x34C / +0x350             further copy (yaw negated), rewritten each frame
```

- **Convention:** camera yaw (atan2 of the look vector's x over z) = −aimer yaw, modulo 2π
  (aimer 5.5107 rad = 315.74° ↔ camera 44.26°). Pitch has the same sign: positive = look up.
- **Code:**
  - `0x140FB9610`: aimer update. Integrates input into +0x14/+0x18, then wraps/clamps via `0x140FAFFD0` / `0x140FAFF40`.
  - `0x140FA2660`: aiming simulation update. Copies the aimer into aiming +0x18/+0x1C at `0x140FA35B9`, then adds sway/recoil.
  - `0x140FA6944`: soldier update. Copies the result into soldier +0x4D8/+0x4DC via the getter `0x140FFFC90`.
- In a scripted no-weapons section (Baku intro walk), writes to every copy were overwritten. The
  aimer is only authoritative while the player can aim; scripted sections need their own handling.

## Evidence

| Test (2026-10-25) | Result |
| --- | --- |
| `tools/aim_probe.py --axis yaw`: stick right 300 ms | camera −7.18°; soldier +0x4D8 0 → 0.1254 rad, pitch run +4.53° → +0x4DC 0.0790 |
| Write soldier +0x4D8/+0x34C, aiming +0x18/+0x1C | overwritten within a frame |
| Hardware write-watch on soldier +0x4D8 / aiming +0x18 | writers above, 30 writes/s (unfocused 30 fps) |
| Write aimer +0x14 += 0.5 rad | camera yaw 44.26° → 15.61° (−28.65°), value kept |
| Write aimer +0x18 −= 0.2 rad | camera pitch −3.40° → −14.86° (−11.46°), value kept |
| Soldier global after a section change | new soldier object (0x179ACCB60), aimer chain still valid |

## Use for #5 (comfort)

- **Snap/smooth turn:** add to aimer +0x14 (once per frame, game thread; e.g. from the view-params hook).
  **And to the prediction** (2026-10-27): the render path's aim prediction `0x140FBA340` (called from
  `0x140FA6BFF`) writes predicted yaw/pitch to aimer +0x60/+0x64 and the frame time to +0x68. The next
  aimer update `0x140FB9610` copies +0x60/+0x64 over +0x14/+0x18 (`0x140FB993A`..`0x140FB9950`) when
  +0x68 > 0 and its input's byte +0x188 is set, then clears +0x68. That byte stays set once the game
  has seen pad stick input, after which a write to +0x14/+0x18 alone is lost every frame. Symptom:
  snap turns counted but the view stayed put after one use of the stick in look mode (fresh checkpoint:
  0 -> 30 -> 60 deg; after look input: 60 -> 60). `input::write_aim` writes both when +0x68 > 0: after
  the fix 60 -> 90 -> 120 deg, and controller aim followed a fake controller +-40 deg. Hardware watch:
  +0x14 written only at `0x140FB994A` (the copy), `0x140FB9989`, `0x140FB99C2`; +0x60 only at
  `0x140FBA529`. Likely the headset's "right stick does not turn" (2026-10-26).
- **Neutralise game pitch:** hold aimer +0x18 at 0 (head pitch only), or hook after `0x140FB9610`.
- **Recenter:** set aimer yaw from the head's yaw.
- Writing the aimer also moves where bullets go; aiming with the controller (#12) will replace this.

## Walking direction (2026-10-27, monitor, fakehead/fakehand)

- The game walks relative to its own camera (the body: the view before our head pose), in both aim
  modes. With `aim = controller` and the gun turned 90 deg, stick forward still walked straight ahead
  of the body; the aimer does not steer walking.
- So a player who turns in the room (head yaw relative to the body) walked the old way. `move_direction`
  (`input::for_game`) turns the left stick by the head's LOCAL yaw (default `head`), or the gun hand's
  (`gun`), or not at all (`body`). Measured with the head turned 90 deg left: `body` walked 90 deg off
  where you look, `head` 0 deg, in both aim modes.


## Controller aim: the drawn gun, the hands' frame and the shots (2026-11-02, monitor)

- **Frame bug (fixed):** under `aim = controller` the camera hook renders the first-person camera
  (+0xED0) level at the *body* yaw (`level_rows(body - game_yaw)`), but the arm IK placed the hands in
  the CameraJoint frame plus aimer pitch, and the joint follows the aimer (the gun). Baseline (old
  build): gun 40 deg left drawn at the bottom right, nearly off-screen; 30 deg up drew the arm across
  half the view. `arm_pose` now builds its frame as joint * Ry(body - game) * axes (level) when the
  hook re-aimed the camera (`stereo::first_person_view_turn`); pipe `armframe rendered|game` for A/B.
- **Drawn barrel = aim ray (`gun_pose = aim`):** the hand goal is chosen so Wep_Root lies on the aim
  ray, upright ((0,1,0,0) in camera axes), instead of the hip animation turned by the controller
  (its idle angle, sway and bob stayed in the drawn barrel). Pipe `gunlook`: barrel vs aim used vs
  aim ray, at yaw/pitch (0,0), (40,0), (0,30), (-30,-20): 0.00-0.02 deg.
- **Two-handed lock:** the barrel lies on the wrist-to-wrist line, corrected by the gun's support
  offset (LeftHand relative to RightHand in Wep_Root axes; AK-12: (0.057,-0.062,0.506) vs
  (-0.021,-0.070,0.203), i.e. 7.8 cm left, level, 30 cm ahead). Off hand 20 cm right, 10 cm up:
  barrel 35.7 deg right, 15.1 deg up (geometry: ~34/14); drawn barrel and shot 0.00 deg apart.
  The support point moves with the gun's animations (right after a load with an empty AK:
  (0.022,-0.172,0.413)), so it is eased over 0.33 s.
- **Phantom fire (not the mod):** a real "Xbox 360 Controller for Windows" on XInput slot 0 reported
  RT = 124/255 at rest; the mod merges a real pad on its slot, so the gun emptied its reserve with no
  input. `physical_pad = 0` ignores it; `status` prints `physical_pad merged|ignored ... rt=`.
