"""Find the current weapon's ammo fields (magazine, reserve) by firing and reloading and diffing memory
(issue #14; local analysis, campaign only, game must run with the DLL and research = 1).

    python -E -P tools\\ammo_probe.py                     fire 3, fire 2, reload; print matching fields
    python -E -P tools\\ammo_probe.py --extra globals.json   also diff the objects listed there (server side)

Snapshots the local soldier's current ClientSoldierWeapon (0x4A40 bytes) and every object it points
to (two levels, 0x800 bytes each), fires with left clicks (tools\\game.ps1 click, takes focus), and
prints 32-bit int fields that dropped by exactly the shots fired, then what the reload (R key) did to
them and to fields that dropped at the reload (the reserve).
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import peek, peek_many  # noqa: E402

SOLDIER_GLOBAL = 0x14230F260
WEAPON_SIZE = 0x4A40
CHILD_SIZE = 0x800


def u64(data, offset):
    return struct.unpack_from('<Q', data, offset)[0]


def heap(p):
    return 0x10000 < p < 0x7FF000000000 and p % 8 == 0


def current_weapon():
    soldier = u64(peek(SOLDIER_GLOBAL, 8), 0)
    weapons = u64(peek(soldier + 0x570, 8), 0)
    body = peek(weapons, 0x900)
    holder = u64(body, 0x8E0) if body[0x28] & 4 else u64(body, 0x8D8)
    index = struct.unpack('<I', peek(holder + 0x8, 4))[0]
    array = u64(body, 0x890)
    return soldier, weapons, u64(peek(array + 8 * index, 8), 0), index


def snapshot(weapon, extra):
    blocks = {weapon: peek(weapon, WEAPON_SIZE)}
    level = {u64(blocks[weapon], o) for o in range(0, WEAPON_SIZE, 8)}
    level = [p for p in level if heap(p) and p not in blocks]
    for address, data in peek_many(level, CHILD_SIZE).items():
        if data:
            blocks[address] = data
    second = set()
    for address in level:
        data = blocks.get(address)
        if data:
            second |= {u64(data, o) for o in range(0, CHILD_SIZE, 8)}
    second = [p for p in second if heap(p) and p not in blocks]
    second += [p for p in extra if p not in blocks]
    for address, data in peek_many(second, CHILD_SIZE).items():
        if data:
            blocks[address] = data
    return blocks


def ints(blocks, base, offset):
    data = blocks.get(base)
    if not data or offset + 4 > len(data):
        return None
    return struct.unpack_from('<i', data, offset)[0]


def act(*arguments):
    subprocess.run(['powershell', '-NoProfile', '-File', os.path.join(HERE, 'game.ps1'), *arguments],
                   check=True, capture_output=True)


def fire(shots):
    for _ in range(shots):
        act('click', '-X', '640', '-Y', '360')
        time.sleep(0.6)
    time.sleep(0.5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--extra', help='JSON list of extra object addresses (hex strings) to diff')
    args = parser.parse_args()
    extra = []
    if args.extra:
        with open(args.extra) as f:
            extra = [int(a, 16) for a in json.load(f)]

    soldier, weapons, weapon, index = current_weapon()
    print(f'soldier {soldier:#x} weapons {weapons:#x} weapon[{index}] {weapon:#x}')
    s0 = snapshot(weapon, extra)
    fire(3)
    s1 = snapshot(weapon, extra)
    fire(2)
    s2 = snapshot(weapon, extra)
    act('keys', '-Keys', 'R')
    time.sleep(4.0)
    s3 = snapshot(weapon, extra)
    print(f'{len(s0)} objects')

    magazine = []
    for base, data in s0.items():
        for offset in range(0, len(data) - 3, 4):
            v = [ints(s, base, offset) for s in (s0, s1, s2, s3)]
            if None in v:
                continue
            if v[0] - v[1] == 3 and v[1] - v[2] == 2 and 0 < v[0] <= 500:
                magazine.append((base, offset, v))
    reserve = []
    for base, data in s0.items():
        for offset in range(0, len(data) - 3, 4):
            v = [ints(s, base, offset) for s in (s0, s1, s2, s3)]
            if None in v:
                continue
            if v[0] == v[1] == v[2] and 0 < v[2] - v[3] <= 200 and 0 < v[0] <= 2000:
                reserve.append((base, offset, v))
    where = lambda base: 'weapon' if base == weapon else f'{base:#x}'
    for base, offset, v in magazine:
        print(f'magazine? {where(base)} +{offset:#x}: {v[0]} -> {v[1]} -> {v[2]} -> reload {v[3]}')
    refilled = {v[3] - v[2] for _, _, v in magazine if v[3] > v[2]}
    for base, offset, v in reserve:
        tag = ' (matches refill)' if v[2] - v[3] in refilled else ''
        print(f'reserve?  {where(base)} +{offset:#x}: {v[0]} -> reload {v[3]}{tag}')
    if not magazine:
        print('no magazine-like field (did the shots fire? is a weapon in hand?)')


if __name__ == '__main__':
    main()
