"""Weapon catalog: the 51 guns of the campaign weapon crate (campaign level loaded).

    python -E -P tools\\weapon_catalog_probe.py [--json re\\dumps\\weapon_catalog.json]

For every gun in the crate's list (UIWeaponSelectorGridWidget, re/notes/weapon.md "The weapon crate and
unlocks") reads, without the gun being spawned:
  unlock asset  +0x78 StoredWeaponData {WeaponClass, MagCount, MagCapacity}
                +0x58 NonStreamedBlueprint -> +0x40 SoldierWeaponData
  SoldierWeaponData +0x80 WeaponClass, +0x1C8 AnimBaseSet, +0x138 ZoomLevels, +0x90 WeaponFiringData
  WeaponFiringData  +0x10 PrimaryFire (FiringFunctionData), +0x4F ReloadWholeMags
  FiringFunctionData +0x168 FireLogic: +0x1C BoltAction, +0x60 RateOfFire, +0x70 ReloadDelay,
                     +0x74 ReloadTime, +0x80 ReloadTimeBulletsLeft, +0x98 ReloadLogic, +0x9C ReloadType,
                     +0xA0 FireLogicType, +0xA8 FireLogicTypeArray; +0x220 Ammo (AmmoConfigData)
The assets are found by the SoldierWeaponUnlockAsset vtable (full heap walk, tools/heap.py); the crate's
list (re/notes/weapon.md "The weapon crate and unlocks") gives the names and the order.
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from camera_probe import peek  # noqa: E402

UNLOCK_VTABLE = 0x141E76F40
CRATE = '''MP412Rex | CBJ-MS Scorpion JS2 MX4 P90 MagpulPDR PP2000 UMP45 | 870 HAWK M1014 QBS09_SP SAIGA_20K SPAS12
USAS-12_SP UTAS | A91 GalilACE52 AK5C G36C M4A1 SG553LB | AEK971 AK12 SteyrAug CZ805 FAMAS M16A4 M416 QBZ951 SAR21 SCAR-H |
M240 M249 MG4 Pecheneg QBB95 Type88 Ultimax | MK11 M39EBR QBU88 RFB_SP SCAR-HSV SVD12 | SRS FY-JS M82A3_SP Scout SV98'''
ROWS = ['sidearm', 'pdw', 'shotgun', 'carbine', 'assault', 'lmg', 'dmr', 'sniper']
RELOAD_TYPE = {0: 'single', 1: 'magazine', 2: 'magazine+short'}
FIRE_LOGIC = {0: 'single', 1: 'bolt', 2: 'auto', 3: 'burst', 4: 'hold', 5: 'detonated'}


def q(a):
    return struct.unpack('<Q', peek(a, 8))[0]


def cstr(a, n=96):
    if not a:
        return ''
    try:
        return peek(a, n).split(b'\0')[0].decode(errors='replace')
    except RuntimeError:
        return '?'


def array(a, stride=8):
    """Frostbite Array<T>: pointer to the first element, the count at -4."""
    if not a:
        return []
    count = struct.unpack('<I', peek(a - 4, 4))[0]
    return [a + i * stride for i in range(min(count, 64))]


def crate_assets():
    from heap import find_qword
    by_name = {}
    for a in find_qword(UNLOCK_VTABLE):
        try:
            by_name.setdefault(cstr(q(a + 0x20)), a)
        except RuntimeError:
            pass
    for row, group in zip(ROWS, CRATE.split('|')):
        for name in group.split():
            yield row, 'U_' + name, by_name.get('U_' + name)


def describe(asset):
    raw = peek(asset, 0x90)
    weapon_class, mag_count, mag_capacity = struct.unpack_from('<iii', raw, 0x78)
    row = {
        'unlock': cstr(struct.unpack_from('<Q', raw, 0x20)[0]),
        'asset': cstr(struct.unpack_from('<Q', raw, 0x10)[0]),
        'stored_class': weapon_class, 'mag_count': mag_count, 'mag_capacity': mag_capacity,
    }
    try:
        row.update(firing_data(q(q(asset + 0x58) + 0x40)))
    except RuntimeError as error:
        row['error'] = 'not streamed in (spawn it once, then --held)'
    return row


def firing_data(data):
    """From a SoldierWeaponData (the spawned weapon's +0x30, or the asset's blueprint object)."""
    row = {}
    if True:
        row['weapon_class'] = struct.unpack('<i', peek(data + 0x80, 4))[0]
        row['anim_base_set'] = struct.unpack('<i', peek(data + 0x1C8, 4))[0]
        row['zoom_levels'] = len(array(q(data + 0x138)))
        firing = q(data + 0x90)
        row['reload_whole_mags'] = peek(firing + 0x4F, 1)[0]
        fire = q(firing + 0x10)
        logic = peek(fire + 0x168, 0xB8)
        rof, = struct.unpack_from('<f', logic, 0x60)
        reload_delay, reload_time = struct.unpack_from('<ff', logic, 0x70)
        reload_left, = struct.unpack_from('<f', logic, 0x80)
        reload_logic, reload_type, fire_type = struct.unpack_from('<iii', logic, 0x98)
        bolt_delay, bolt_time = struct.unpack_from('<ff', logic, 0x1C)
        modes = [FIRE_LOGIC.get(struct.unpack('<i', peek(a, 4))[0], '?') for a in array(struct.unpack_from('<Q', logic, 0xA8)[0], 4)]
        ammo = peek(fire + 0x220, 0x24)
        row.update({
            'rate_of_fire': round(rof), 'reload_time': round(reload_time, 2), 'reload_time_left': round(reload_left, 2),
            'reload_type': RELOAD_TYPE.get(reload_type, reload_type), 'fire_logic': FIRE_LOGIC.get(fire_type, fire_type),
            'fire_modes': modes, 'bolt_time': round(bolt_time, 2),
            'ammo_raw': ammo.hex(),
        })
    return row


def held():
    """The spawned weapons in the soldier's slots: their full firing data."""
    from slot_probe import SOLDIER_GLOBAL
    weapons = q(q(SOLDIER_GLOBAL) + 0x570)
    begin, end = q(weapons + 0x890), q(weapons + 0x898)
    for slot in range((end - begin) // 8):
        weapon = q(begin + 8 * slot)
        if not weapon:
            continue
        asset = q(weapon + 0x4850)
        row = describe_stored(asset)
        try:
            row.update(firing_data(q(weapon + 0x30)))
        except RuntimeError as error:
            row['error'] = str(error)
        row['slot'] = slot
        yield row


def describe_stored(asset):
    raw = peek(asset, 0x90)
    weapon_class, mag_count, mag_capacity = struct.unpack_from('<iii', raw, 0x78)
    return {'unlock': cstr(struct.unpack_from('<Q', raw, 0x20)[0]), 'stored_class': weapon_class,
            'mag_count': mag_count, 'mag_capacity': mag_capacity}


def line(prefix, row):
    return (f"{prefix:<8}{row['unlock']:<14} class={row.get('weapon_class')} anim={row.get('anim_base_set')} mag={row['mag_capacity']}x{row['mag_count']} "
            f"{row.get('reload_type')} {row.get('fire_logic')} {row.get('fire_modes')} rof={row.get('rate_of_fire')} "
            f"reload={row.get('reload_time_left')}/{row.get('reload_time')} bolt={row.get('bolt_time')} zoom={row.get('zoom_levels')} "
            f"{row.get('error', '')}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--json')
    parser.add_argument('--held', action='store_true', help='the spawned weapons in the slots, with firing data')
    args = parser.parse_args()
    rows = []
    if args.held:
        for row in held():
            rows.append(row)
            print(line(f"slot {row['slot']}", row))
    else:
        for crate_row, name, asset in crate_assets():
            if not asset:
                print(name, 'not in memory')
                continue
            row = describe(asset)
            row['row'] = crate_row
            rows.append(row)
            print(line(crate_row, row))
    print(len(rows), 'weapons')
    if args.json:
        with open(args.json, 'w') as fh:
            json.dump(rows, fh, indent=1)


if __name__ == '__main__':
    main()
