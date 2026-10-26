# Weapons, ammo and physical reload — BF4 build 179547

Status: **ammo mapped and written (2026-10-26)**; physical reload (#14) built on it
(`src/weapon/`), monitor-tested, not headset-tested.

## Where the ammo lives

```
client  [0x14230F260] soldier +0x570 weapons component -> current slot (aim.md) -> ClientSoldierWeapon
          +0x4850 unlock asset, +0x20 -> "U_AK12"                      weapon name for the reload profile
          +0x30 SoldierWeaponData +0x90 WeaponFiringData +0x10 FiringFunctionData
                +0x220 Ammo.MagazineCapacity (31 for the 30-round AK-12: counts the chamber)
          +0x49C0 / +0x49C8 -> two ammo blocks, magazine +0x1A0, reserve +0x1A4   (copies)
server  [0x142311DB8] ServerSoldierEntity +0x7C0 ServerSoldierWeaponsComponent
          +0x828 + 8 * slot -> ServerSoldierWeapon (same slot index as the client)
          +0x440 int32 rounds in the gun, +0x444 reserve          (authoritative)
```

- Found with `tools/ammo_probe.py` and a value search for the HUD's 31/124 across the client weapon
  and two levels of pointers: three pairs, one inside `ServerSoldierWeapon` (typed by its vtable),
  two in untyped blocks behind the client weapon.
- The single-player server runs in the game process (209 `ServerSoldierEntity` objects in the
  global scan, typeinfo.md), and it owns the ammo.

## Tests (campaign checkpoint "Get Grenade Launcher", AK-12 in hand, SCAR-H in slot 0)

| Test | Result |
| --- | --- |
| Write 5 into one client block | back to 31 within 1.5 s |
| Write 5 into the server weapon | both client blocks and the HUD show 5 within 1.5 s; persists |
| Write 0 into the server weapon | the game reloads by itself within ~4 s: 30/94 (no chambered round) |
| Write 1 | stays 1 (no reload); the HUD shows "RELOAD" |
| `bone 131 0 0.3 0` (research) | the magazine mesh moves 0.3 m off the gun on its own |

## Consequences for the physical reload

- Write only the server weapon; read shots back from it (the game counts down from what we wrote).
- Never let the game's magazine reach 0: it would start its own reload. The mod writes the rounds
  in the gun plus 2 spare rounds and holds the trigger back while the chamber is empty, so the HUD
  ammo counter reads 2 high in physical mode (`hud_show_ammo = 0`, or a wrist counter later).
- The game's reload button (pad X) is taken: AR magazine release / bolt catch.
- `Wep_Mag` is drawn by the mod while the magazine is out (in the hand, falling, or 50 m below).

## Open

- Hardware-watch the writer of the server ammo to find the auto-reload decision, if the spare rounds
  ever show (HUD) or leak (a full-auto burst faster than one frame).
- Shotguns, bolt actions, belt-fed guns, pistols: variants and their bones (`Wep_Belt*`, `Wep_Slide`).
- Underslung launchers (`Wep_Grenade1/2`) use a second firing function.
