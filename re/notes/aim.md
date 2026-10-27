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

