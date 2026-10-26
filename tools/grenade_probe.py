"""Grenade research (docs/tasks/grenades.md stage 0; game must run with the DLL, campaign only).

    python -E -P tools\\grenade_probe.py            the grenade slot's data: shot, projectile, count
    python -E -P tools\\grenade_probe.py --slot 6

Reads the local soldier's weapon in the slot (client array, re/notes/aim.md), its FiringFunctionData
(primary fire) with ShotConfigData and HoldAndReleaseData, the projectile data it spawns, and the
server-side firing object's counters (+0x1A0, as for guns, re/notes/weapon.md).
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import peek  # noqa: E402
from object_probe import load_vtables  # noqa: E402

SOLDIER_GLOBAL = 0x14230F260
SERVER_SOLDIER_GLOBAL = 0x142311DB8


def q(address):
    return struct.unpack('<Q', peek(address, 8))[0]


def f(address, count=1):
    return struct.unpack(f'<{count}f', peek(address, 4 * count))


def name_at(address):
    try:
        return peek(q(address), 64).split(b'\0')[0].decode(errors='replace')
    except RuntimeError:
        return '?'


def class_of(vtables, address):
    try:
        return vtables.get(q(address), '?')
    except RuntimeError:
        return '?'


def describe(slot, vtables):
    soldier = q(SOLDIER_GLOBAL)
    weapons = q(soldier + 0x570)
    client = q(q(weapons + 0x890) + 8 * slot)
    print(f'slot {slot}: client weapon {client:#x} {name_at(q(client + 0x4850) + 0x20)}')
    data = q(client + 0x30)
    firing = q(data + 0x90)
    function = q(firing + 0x10)
    print(f'  SoldierWeaponData {data:#x}  WeaponFiringData {firing:#x}  FiringFunctionData {function:#x}')
    shot = function + 0x60
    print(f'  Shot.InitialPosition {f(shot, 3)}  InitialDirection {f(shot + 0x10, 3)}  InitialSpeed {f(shot + 0x20, 3)}')
    print(f'  Shot.SpawnDelay {f(shot + 0x70)[0]:.3f}  SpawnDelayZoomed {f(shot + 0x74)[0]:.3f}  '
          f'ForceSpawnToCamera {peek(shot + 0x85, 1)[0]}  SpawnVisualAtWeaponBone {peek(shot + 0x86, 1)[0]}')
    projectile = q(shot + 0x50)
    print(f'  ProjectileData {projectile:#x} {class_of(vtables, projectile)}: InitialSpeed {f(projectile + 0x90)[0]:.2f} '
          f'TimeToLive {f(projectile + 0x94)[0]:.2f} DetonateOnTimeout {peek(projectile + 0xD8, 1)[0]} '
          f'Gravity {f(projectile + 0x130)[0]:.2f}')
    logic = function + 0x168
    print(f'  HoldAndRelease MaxHoldTime/MinPower/MaxPower/PowerPerSecond/Delay {f(logic, 5)}')
    print(f'  Ammo MagazineCapacity/NumberOfMagazines {struct.unpack("<ii", peek(function + 0x220, 8))}')
    server_weapon = q(q(q(SERVER_SOLDIER_GLOBAL) + 0x7C0) + 0x828 + 8 * slot)
    server_firing = q(server_weapon + 0x200)
    print(f'  server weapon {server_weapon:#x} firing {server_firing:#x} +0x1A0: {struct.unpack("<ii", peek(server_firing + 0x1A0, 8))}')
    print(f'  client firing blocks: {[struct.unpack("<ii", peek(q(client + o) + 0x1A0, 8)) for o in (0x49C0, 0x49C8)]}')
    return function, projectile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--slot', type=int, default=6)
    args = parser.parse_args()
    describe(args.slot, load_vtables(None))


if __name__ == '__main__':
    main()
