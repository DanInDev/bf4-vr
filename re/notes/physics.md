# The game's physics ray cast (#59)

Static analysis 2026-11-14 on `re/dumps/bf4-179547-mem.exe` (REA/Ghidra 12.1.4 and `tools/disasm.py`).
Not yet called live: the monitor check is below.

## How it was found

- The physics query takes a name string for profiling. Strings that look like one:
  `CharacterPhysicsEntity::contactRayCast`, `VaultComponent::rayCast`,
  `ClientVehicleDeployWeapon::hasGroundBelowDeployArea`, `GameRenerer::updateCrosshairDepth` (sic),
  `ClientTripwireEntity`, `ClientPowerToolWeapon::findVehicleToRepair`, and server ones.
- `tools/callers.py` on the function the ground-below check calls (`0x140986CA0`) gives 22 direct
  callers. The client ones load the world from **`[0x142668F90]`**. The server ones load it from
  `[0x142714938]` (`serverEntryComponent:getExitPoint`).
- `CharacterPhysicsEntity::contactRayCast` (`0x140B40950`) instead goes through a virtual interface:
  `[this+0x50]->vfunc[0x120/8]()` returns a caster, and its `vtable[0]` is called with the same
  `(name, from, to, hit, flags, filter)` shape. The direct call is simpler, so the mod uses that.

## The call

```
bool 0x140986CA0(world,               // rcx: [0x142668F90] (client)
                 const char* name,    // rdx: any string (profiling)
                 int async,           // r8d: 0 = answer now (the callers seen pass 0)
                 const Vec4* from,    // r9:  16-byte aligned, w = 0, world metres
                 const Vec4* to,      // [rsp+0x20]
                 RayCastHit* hit,     // [rsp+0x28]
                 int flags,           // [rsp+0x30]
                 ExcludeList* skip)   // [rsp+0x38]
```

- **Prologue:** `40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 10 FE FF FF 48 81 EC F0 02 00 00`.
- **Flags seen:**
  - `0` (crosshair depth: everything);
  - `4` (character contact);
  - `0x34` (ground below a deploy area).
  - Read against Frostbite's usual RayCastFlags (`0x4` no water, `0x10` no ragdolls, `0x20` no
    characters), `0x34` is "the level only". The mod uses `0x34`.
- **RayCastHit** (0x40 bytes; the callers zero it and set `+0x2C` to -1 first):
  - `+0x00`: Vec4 position;
  - `+0x10`: Vec4 normal;
  - `+0x20`: rigid body pointer (null = no hit; the ground check tests it);
  - `+0x28`: int;
  - `+0x2C`: int, int (-1 before the call: material, part);
  - `+0x34`: float (the crosshair scales it: the hit's fraction along the ray).
- **ExcludeList:** an eastl `fixed_vector<const void*, 8>`, 0x60 bytes:
  - `+0x00` begin, `+0x08` end, `+0x10` capacity end;
  - `+0x18`: not read in the calls seen;
  - `+0x20`: 8 inline slots.
  - The callers put the player's own physics entity in it. The mod passes an empty one
    (begin = end = the inline slots), never null: the pointer reaches deep Havok code (`0x140C996F0`)
    whose null handling was not checked.

## Thread

- `GameRenderer::updateCrosshairDepth` casts its rays from the renderer's per-frame update on the game
  thread. The mod's game-thread tick is the camera hook on the same per-frame view copy
  (`docs/architecture/engine.md`), so the mod casts there, a few rays per tick.
- The BF3 mod cast on its level-update thread with a try-lock; BF4's own crosshair cast suggests
  that is not needed here. To be confirmed live: no crash over a few thousand rays.
- BF3: a zero-length ray divides by zero. The mod never casts one shorter than 1 cm.

## Monitor check (to do, needs BF4 running)

1. `pipe raycast down`: from the head straight down 3 m. The hit should be about 1.6 m below the eye
   standing, and about 1.0 m crouched; the normal should be near (0, 1, 0).
2. `pipe raycast ahead 20` facing a wall: the hit distance should match the picture.
3. `pipe raycast stats` after a minute of loose magazines: count, misses, microseconds a ray.
