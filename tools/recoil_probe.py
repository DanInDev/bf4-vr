"""Weapon recoil data (docs/tasks/hand-animation.md; game must run with the DLL, campaign only).

    python -E -P tools\\recoil_probe.py                 every gun slot's GunSwayData recoil, the stance
    python -E -P tools\\recoil_probe.py --watch 3       also sample the aiming sway/recoil offset for 3 s

Chain (re/notes/weapon.md, TypeInfo offsets from tools/typeinfo_dump.py):
  ClientSoldierWeapon +0x30 SoldierWeaponData +0x90 WeaponFiringData +0x30 WeaponSway (GunSwayData)
  GunSwayData: Stand +0x10 (NoZoom +0x00, Zoom +0xBC; Recoil at +0x90 of each), Crouch +0x188 and
  Prone +0x240 (NoZoom +0x00, Zoom +0x5C; Recoil at +0x30), ShootingRecoilDecreaseScale +0x440,
  FirstShotRecoilMultiplier +0x444, CameraRecoil +0x448.
  GunSwayRecoilData: RecoilAmplitudeMax, RecoilAmplitudeIncPerShot, HorizontalRecoilAmplitudeIncPerShotMin,
  HorizontalRecoilAmplitudeIncPerShotMax, HorizontalRecoilAmplitudeMax, RecoilAmplitudeDecreaseFactor.
  FiringFunctionData +0x168 FireLogic: +0x2C RecoilData (angles), +0x60 RateOfFire.
"""
import argparse
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import peek  # noqa: E402
from slot_probe import SOLDIER_GLOBAL, current, q, slots  # noqa: E402

RECOIL_FIELDS = ('max', 'inc', 'hmin', 'hmax', 'hcap', 'decrease')
STANCES = (('stand', 0x10, 0xBC, 0x90), ('crouch', 0x188, 0x5C, 0x30), ('prone', 0x240, 0x5C, 0x30))


def f(address, count=1):
    return struct.unpack(f'<{count}f', peek(address, 4 * count))


def recoil(address):
    return ' '.join(f'{n}={v:.3f}' for n, v in zip(RECOIL_FIELDS, f(address, 6)))


def describe(slot, name):
    weapons = q(q(SOLDIER_GLOBAL) + 0x570)
    weapon = q(q(weapons + 0x890) + 8 * slot)
    firing = q(q(weapon + 0x30) + 0x90)
    sway = q(firing + 0x30)
    function = q(firing + 0x10)
    print(f'slot {slot} {name}: WeaponFiringData {firing:#x} GunSwayData {sway:#x}')
    if not sway:
        return
    for stance, at, zoom, offset in STANCES:
        print(f'  {stance:6s} hip  {recoil(sway + at + offset)}')
        print(f'  {stance:6s} zoom {recoil(sway + at + zoom + offset)}')
    decrease, first = f(sway + 0x440, 2)
    print(f'  ShootingRecoilDecreaseScale {decrease:.3f}  FirstShotRecoilMultiplier {first:.3f}')
    camera = q(sway + 0x448)
    if camera:
        print('  CameraRecoil spring k={:.2f} damping={:.2f} min={:.3f}'.format(*f(camera + 0x10, 3)))
    if function:
        logic = function + 0x168
        angles = f(logic + 0x2C, 8)
        print('  FireLogic.Recoil X {:.2f}..{:.2f} Y {:.2f}..{:.2f} Z {:.2f}..{:.2f} fov {:.2f}..{:.2f}'.format(
            angles[1], angles[0], angles[3], angles[2], angles[5], angles[4], angles[7], angles[6]))
        print(f'  RateOfFire {f(logic + 0x60)[0]:.0f}')


def watch(seconds):
    weapons = q(q(SOLDIER_GLOBAL) + 0x570)
    weapon = q(q(weapons + 0x890) + 8 * current())
    aiming = q(weapon + 0x4988)
    end = time.time() + seconds
    while time.time() < end:
        yaw, pitch = f(aiming + 0x28, 2)
        print(f'{time.time():.3f} sway/recoil yaw {yaw:+.5f} pitch {pitch:+.5f} rad')
        time.sleep(0.02)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--watch', type=float, default=0)
    args = parser.parse_args()
    stance = struct.unpack('<I', peek(q(SOLDIER_GLOBAL) + 0x4F0, 4))[0]
    print(f'stance {stance} (0 stand, 1 crouch, 2 prone), current slot {current()}')
    for slot, name in slots().items():
        describe(slot, name)
    if args.watch:
        watch(args.watch)


if __name__ == '__main__':
    main()
