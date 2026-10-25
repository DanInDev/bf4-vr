# Type registry (TypeInfo) — BF4 build 179547

Status: **mapped and dumped (2026-10-25).** `tools/typeinfo_dump.py` reads the registry from the
main-menu memory dump in under a second; `tools/object_probe.py` and `tools/global_scan.py` use it
to name live objects through the control pipe.

## What it gives us

| Count | Kind | Notes |
| --- | --- | --- |
| 4017 | Class | Name, size, alignment, parent chain, class id; fields only for data classes |
| 1692 | ValueType | Structs such as `LinearTransform`, `Vec3`, with fields |
| 589 | Enum | Value names |
| 3893 | Array | Element type |
| 1755 | vtables | vtable address → class, for every class with its own `getType()` (1562 classes) |

- **Data classes** (`…Data`, `…Asset`, settings) carry full field lists with offsets. These are the
  same classes Frosty shows for EBX assets; at runtime the loaded asset objects use these offsets.
- **Runtime classes** (`ClientSoldierEntity`, `ClientSoldierWeapon`, components) have a TypeInfo
  with a name, size and parent but **no fields**. They can still be recognised live by their vtable.
- **Not reflected at all:** `ClientGameContext`, `ClientPlayerManager`, `ClientPlayer`, `ClientLevel`,
  `GameRenderer`, `RenderView`, input concepts. Those need raw offsets found by experiment.
- A field whose type is a class is a pointer to that object (8 bytes), not an embedded struct.

Useful reflected runtime classes for the VR work: `ClientSoldierEntity`, `ClientSoldierWeapon`,
`ClientSoldierWeaponsComponent`, `ClientSoldierCameraComponent`, `VeniceClientSoldierCameraComponent`,
`ClientSoldierGripComponent`, `ClientAntAnimatableComponent` (animation/skeleton),
`ClientSoldierBodyComponent`, `ClientBoneCollisionComponent`, `ClientWeapon` (0xB70, base of all
client weapons), `ClientSoldierWeapon` (0x4A40), `ClientWeaponComponent` (a `ClientBoneComponent`),
`ClientWeaponLagEntity`, `ClientRagdollComponent`, camera classes (`Camera`, `FPSCamera`,
`VeniceFPSCamera`, `FreeCamera`, `VehicleCamera`, `ClientCameraComponent`). Useful data classes:
`SoldierWeaponData`, `WeaponFiringData`, `FiringFunctionData`, `GunSwayData`, `ZoomLevelData`,
`FirstPersonCameraData`, `SkeletonAsset`, `WorldRenderSettings`, `GameRenderSettings`.

## Layouts

Verified against the dump: all names resolve; in all 2707 types with fields, every field lies
inside the type's size and after its parent's size (e.g. `DataContainer` 0x10 → `DataBusPeer.Flags`
+0x10). Fields are not stored in offset order (661 types), so the tools sort them.

```
TypeInfo object     +0x00 info data*   +0x08 next TypeInfo*   +0x10 u16 runtime id   +0x12 u16 flags
ClassInfo object    +0x18 super ClassInfo*   +0x28 u16 class id   +0x2A u16 last subclass id
info data           +0x00 name*   +0x08 u16 flags   +0x0A u16 size   +0x10 module*
                    +0x18 u8 alignment   +0x1A u16 field count
    class           +0x20 super*   +0x28 create function   +0x30 fields*
    value type      +0x28 fields*
    enum            +0x20 values*
    array           +0x20 element TypeInfo*
field (0x18)        +0x00 name*   +0x08 u16 flags   +0x0A u16 offset   +0x10 type TypeInfo*
enum value (0x18)   +0x00 name*   +0x10 u32 value
flags               bits 0-1 member type (1 = type, 0 = field), bits 4-8 type code
type codes          0 Void 1 DbObject 2 ValueType 3 Class 4 Array 5 FixedArray 6 String 7 CString
                    8 Enum 9 FileRef 10 Boolean 11-18 Int8..Uint64 19 Float32 20 Float64 21 Guid
                    22 SHA1 23 ResourceRef   (checked: the basic types carry exactly these names)
```

- **List head:** global `0x1423E41B8`, written by the TypeInfo constructor
  (`mov rax,[head]; mov [rcx+8],rax; mov [head],rcx`). The dumper finds it by that pattern.
- The info data sits in the `typeinfo` section, field arrays in `fieldinf`, the TypeInfo objects in
  `typeinfo` or `.data`. All are encrypted on disk, so the dumper needs the memory dump.
- **getType():** `lea rax,[ClassInfo]; ret`, referenced from `.rdata`. Engine RTTI is stripped
  (only ~1000 third-party RTTI names such as Havok), so this is the only way to name an object.
  Confirmed live: objects resolve to sensible classes (`GameView`, `DxTexture`, `UISystem`).

## First live results (Baku "Reach the Safe House", 2026-10-25)

- `tools/global_scan.py` in gameplay: 10525 heap pointers in `.data`, 16153 typed objects reachable
  (incl. 92 `ClientSoldierEntity`, 209 `ServerSoldierEntity`: the single-player server runs in-process).
- **Local soldier:** global `0x14230F260` → the one `ClientSoldierEntity` whose animatable component
  sits 1.56 m below the camera (standing eye height), at the camera's x/z. Its components are listed in
  [`../symbols/bf4-179547.md`](../symbols/bf4-179547.md).
- **Camera manager:** untyped object at `[0x14230A5F8]` with `VeniceFPSCamera` (+0x458) and
  `ChaseCamera` (+0x480): the entry point for the first-person camera (#2) and turning (#5).

## Corrections

- The global `0x142670D80` **is** the (unreflected) ClientGameContext after all: besides a `GameView`
  (+0x70) and render targets it holds the current `Level*` at +0x28 (static analysis of the writer
  `0x140825E7E`; live level path read through it). A first reading here called it render-side; that
  was wrong.
