"""Weapon unlock census (campaign, game running with the DLL).

    python -E -P tools\\unlock_census.py            every SoldierWeaponUnlockAsset in memory
    python -E -P tools\\unlock_census.py --json f   also write the list as JSON

The vtable is taken from the weapon in hand (`ClientSoldierWeapon+0x4850` is its unlock asset), then
`tools/heap.py` finds every asset with the same vtable (pipe `scan` stops at 64 per region). Fields: typeinfo `SoldierWeaponUnlockAsset`.
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from camera_probe import peek  # noqa: E402
from slot_probe import SOLDIER_GLOBAL, q  # noqa: E402


def cstr(address, n=96):
    if not address:
        return ''
    try:
        return peek(address, n).split(b'\0')[0].decode(errors='replace')
    except RuntimeError:
        return '?'


def weapon_unlocks():
    weapons = q(q(SOLDIER_GLOBAL) + 0x570)
    begin, end = q(weapons + 0x890), q(weapons + 0x898)
    out = []
    for slot in range((end - begin) // 8):
        weapon = q(begin + 8 * slot)
        if weapon:
            out.append((slot, q(weapon + 0x4850)))
    return out


def read_asset(a):
    raw = peek(a, 0x90)
    f = lambda o, fmt: struct.unpack_from(fmt, raw, o)[0]
    return {
        'address': hex(a),
        'name': cstr(f(0x10, '<Q')),
        'debug_id': cstr(f(0x20, '<Q')),
        'identifier': f(0x28, '<I'),
        'unlock_score': f(0x2C, '<I'),
        'storage_code': cstr(f(0x38, '<Q')),
        'available_for_player': f(0x40, '<I'),
        'auto_available': raw[0x44],
        'ignore_all_unlocked': raw[0x45],
        'bypass_license': raw[0x46],
        'hidden': raw[0x47],
        'bundle': cstr(f(0x48, '<Q')),
        'weapon_identifier': f(0x70, '<I'),
        'category': f(0x74, '<I'),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--json')
    args = parser.parse_args()
    held = weapon_unlocks()
    vtables = sorted({q(u) for _, u in held if u})
    print('in hand:', ', '.join(f'{s}:{cstr(q(u + 0x20))}' for s, u in held), '| vtables', [hex(v) for v in vtables])
    assets = []
    for vt in vtables:
        from heap import find_qword
        for a in find_qword(vt):
            try:
                assets.append(read_asset(a))
            except (RuntimeError, struct.error):
                pass
    seen, rows = set(), []
    for r in assets:
        if r['address'] in seen:
            continue
        seen.add(r['address'])
        rows.append(r)
    rows.sort(key=lambda r: (r['category'], r['debug_id']))
    for r in rows:
        print(f"{r['address']:>14} cat={r['category']:<3} auto={r['auto_available']} avail={r['available_for_player']} "
              f"ign={r['ignore_all_unlocked']} hid={r['hidden']} score={r['unlock_score']:<6} id={r['identifier']:<10} "
              f"{r['debug_id']:<24} {r['name']}")
    print(len(rows), 'assets')
    if args.json:
        with open(args.json, 'w') as fh:
            json.dump(rows, fh, indent=1)


if __name__ == '__main__':
    main()
