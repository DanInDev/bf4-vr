"""Muzzle flash data of the guns in hand (game must run with the DLL, campaign only; read only).

    python -E -P tools\\fire_effect_probe.py

Chain (re/notes/weapon.md, TypeInfo offsets from tools/typeinfo_dump.py):
  ClientSoldierWeapon +0x30 SoldierWeaponData +0x90 WeaponFiringData +0x10 FiringFunctionData
  FiringFunctionData: Shot (ShotConfigData) +0x60, FireEffects1p +0x150, FireEffects3p +0x158
  (arrays: pointer to the first element, the count as u32 just before it).
  FireEffectData (0x50): Offset +0x00, Rotation +0x10, ZoomOffset +0x20, ZoomRotation +0x30 (Vec3, 16 bytes
  each), Effect +0x40 (asset; its name at +0x20 like other assets), UseZoomOffset +0x48, UseZoomRotation
  +0x49, DisableDuringZoom +0x4A, UpdateTransform +0x4B, StopLoopingEffects +0x4C.
  ShotConfigData: InitialPosition +0x00, InitialDirection +0x10, ForceSpawnToCamera +0x85,
  SpawnVisualAtWeaponBone +0x86, ActiveForceSpawnToCamera +0x87.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import peek  # noqa: E402
from slot_probe import SOLDIER_GLOBAL, q, slots  # noqa: E402


def f(address, count=1):
    return struct.unpack(f'<{count}f', peek(address, 4 * count))


def name_of(asset):
    if not asset:
        return '-'
    try:
        text = q(asset + 0x20)
        raw = peek(text, 96) if text else b''
        return raw.split(b'\0', 1)[0].decode('ascii', 'ignore') or '?'
    except Exception:  # noqa: BLE001 - a dead pointer must not stop the listing
        return '?'


def vec(address):
    return '({:.3f} {:.3f} {:.3f})'.format(*f(address, 3))


def effects(label, array):
    if not array:
        print(f'  {label}: none')
        return
    count = struct.unpack('<I', peek(array - 4, 4))[0]
    print(f'  {label}: {count} at {array:#x}')
    for i in range(min(count, 8)):
        e = array + 0x50 * i
        flags = peek(e + 0x48, 5)
        print(f'    [{i}] offset {vec(e)} rotation {vec(e + 0x10)} zoom_offset {vec(e + 0x20)} '
              f'use_zoom_offset={flags[0]} use_zoom_rotation={flags[1]} disable_zoomed={flags[2]} '
              f'update_transform={flags[3]} stop_looping={flags[4]} effect={name_of(q(e + 0x40))}')


def main():
    weapons = q(q(SOLDIER_GLOBAL) + 0x570)
    for slot, name in slots().items():
        weapon = q(q(weapons + 0x890) + 8 * slot)
        if not weapon:
            continue
        firing = q(q(weapon + 0x30) + 0x90)
        function = q(firing + 0x10) if firing else 0
        print(f'slot {slot} {name}: FiringFunctionData {function:#x}')
        if not function:
            continue
        shot = function + 0x60
        spawn = peek(shot + 0x85, 3)
        print(f'  Shot InitialPosition {vec(shot)} InitialDirection {vec(shot + 0x10)} '
              f'ForceSpawnToCamera={spawn[0]} SpawnVisualAtWeaponBone={spawn[1]} ActiveForceSpawnToCamera={spawn[2]}')
        effects('FireEffects1p', q(function + 0x150))
        effects('FireEffects3p', q(function + 0x158))


if __name__ == '__main__':
    main()
