# Soldier collision and walls (#53)

Monitor run 2026-11-12, 23:38-23:50: BF4 campaign, Baku checkpoint ("Get Grenade Launcher"), the
owner's deployed Release test build (research = 1), no headset (theater waiting for one). The soldier
was walked with the pipe's virtual pad; the feet position comes from the first-person rig
(`ClientSoldierEntity+0x1E8` -> `+0x90` LinearTransform, translation at `+0xC0`).

## Where the radius lives

- `typeinfo_dump.py --class CharacterPhysicsData`: `+0x48 PhysicalRadius` (Float32), beside `Mass`
  (`+0x40`), `MaxAscendAngle` (`+0x44`), `Poses` (`+0x18`, per stance: `EyePosition`,
  `CollisionBoxMinExpand`/`MaxExpand`, `Height`, `CollisionType`) and `AutoPushAwayFromWallsInProne`.
- Live chain: local soldier `[0x14230F260]` -> `+0x30` SoldierEntityData -> `+0x118`
  CharacterPhysicsData -> `+0x48` radius = **0.35** (Mass 100.00, MaxAscendAngle 50.00). After a
  checkpoint restart the new soldier object pointed at the **same** data asset (`0x16892F4B0`).
- No copy of 0.35 sat in CharacterPhysicsEntity (`soldier+0x1B8`, 0x2A0 bytes), the
  BFClientSoldierPhysicsComponent (`+0x148`) or the soldier itself: the live shape is probably inside
  the physics engine.

## Wall test (raw)

Same path both times: from the checkpoint spawn, stick forward 3 s, then 1.5 s, into an inside corner.

```
r = 0.35 (default)
start                              feet -435.669  397.490 -649.687
pushed forward                     feet -433.172  397.999 -639.967   moved 10.035 m
pushed again                       feet -431.576  397.881 -637.690   moved 2.781 m
another push                                                          moved 0.017 m   (stopped)
poke radius 0.10 (live)            -> a push moved 0.005 m, then 0.001 m          (no effect)

checkpoint restart, radius still 0.10
start                              feet -435.669  397.490 -649.687
pushed forward                     feet -433.313  398.124 -639.725   moved 10.236 m
pushed again                       feet -431.579  397.852 -637.358   moved 2.934 m
another push                                                          moved 0.004 m   (stopped)
```

- With 0.10 the soldier stopped **0.33 m further into the corner** (z -637.690 -> -637.358).
- The radius is read when the soldier spawns: a live write did nothing, a checkpoint restart applied it.
- Restored to 0.35 and the game closed afterwards; the deployed DLL and bfvr.ini were unchanged
  (hashes compared with backups).

## For the mod

- A setting can write the radius into the data asset as soon as the soldier is known. It then applies
  from the next spawn: a checkpoint restart, a death, the next level's soldier if the asset is
  written again there. The first spawn of a level keeps 0.35 unless the write happens before it (or
  the code that reads `+0x48` at spawn is hooked: not found yet).
- Smaller radii also let the soldier stand closer to doors, props and companions; how far to go needs
  the headset (lean out of a window with 0.15-0.20).

## The setting, live (2026-11-12/13, 23:58-00:01, branch `research/lean-report` Release build)

The mod's own chain found the asset (`0x1677AD630` in this session) and read 0.35. Raw:

```
> setting set soldier_radius 0.15
soldier_radius wanted=0.15 ... game=0.350 now=0.150
23:59:47.738 soldier radius: 0.35 -> 0.15 m in 00000001677AD630 (the game's 0.35; applies from the next spawn ...)
checkpoint restart, same walk from the spawn (feet -435.669 397.490 -649.687):
pushed again (stopped at wall?)    feet -431.596  397.844 -637.410
another push moved 0.019 m (stopped)
> setting set soldier_radius 0
00:01:09.740 soldier radius: 0.15 -> 0.35 m in 00000001677AD630 (the game's 0.35; ...)
soldier_radius wanted=0.00 ... game=0.350 now=0.350
```

Corner reached: z -637.690 (0.35), -637.410 (0.15, 0.28 m closer), -637.358 (0.10). Afterwards BF4 was
closed and the owner's deployed DLL, deploy marker and bfvr.ini were put back (hashes equal to the backups).

## Where the spawn reads the radius, and the setting from a level's first spawn (2026-11-13, 02:00-02:25)

Read watch (pipe `watch 167ADA8F8 4 10000 rw`, seven times in a row) on `PhysicalRadius` across a checkpoint restart,
the owner's deployed Release build (no radius code in it):

```
rip_after_write=0x140b3e0c4 count=2700 / 10 s  backtrace 0x140b4540d 0x140b4bb38 0x14085de5d ...   (every frame)
rip_after_write=0x140b4685a count=900  / 10 s  backtrace 0x140b4b1d6 0x1412e206e 0x140b4c1a9 ...   (every frame)
rip_after_write=0x140b4133f count=6           backtrace 0x140b3d889 0x140b11ebc 0x140fbf1ca 0x140fc1ce4 0x14080a303 0x1407cef37
rip_after_write=0x140b41523 count=2           backtrace 0x140b3d8ba 0x140b11ebc ...                  (once, at the respawn)
```

- `0x140B412A0` (pdata 0x140B412A0..0x140B414CA) is the pose shape builder: `(rcx out, rdx Array<CharacterPoseData*>,
  r8 CharacterPhysicsData*)`; per pose it reads `+0x74` CollisionType, `+0x50` Height and `[r8+0x48]` PhysicalRadius.
  Called at `0x140B3D884` (`mov r8, [rbx+0xE8]`) in `0x140B3D590`, followed by `0x140B414D0` (the second read).
  Prologue `48 8B C4 55 56 57 41 54 41 55 41 56 41 57 48 81 EC E0 00 00 00`.
- The builder runs for every character: the AI's `Characters/Soldiers/AISoldierPhysics` beside the player's
  `Gameplay/Soldiers/DefaultSoldierPhysics_SP` (asset Name at `+0x10`). The mod's hook changes only the player's.

Live with the hook (`research/lean-report` Release build, deployed temporarily): `setting set soldier_radius 0.15` in the
main menu, campaign resumed, **no checkpoint restart**:

```
02:21:41 soldier radius: spawn hook at 0000000140B412A0: MH_OK
02:23:59 soldier radius: 0.35 -> 0.15 m in 00000001666003D0 (at spawn; the game's 0.35)
radius: wanted=0.15 game=0.350 now=0.150 spawn_hook_calls=2
corner reached: z -637.421   (0.35: -637.690, 0.15 after a restart: -637.410)
```

The owner's DLL, deploy marker and bfvr.ini were put back afterwards (hashes equal).
