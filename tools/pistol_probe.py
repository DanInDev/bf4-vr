"""Pistols with both hands (#99), live: for each gun (given into the slot named, default 1) the fake off hand closed round
the gun hand: does the two-handed hold take it (`twohand` held, pistol), and does the reload leave the hand alone
(`reload` zone, busy)? Then the gun hand alone turns 4 degrees (the gun should turn about half), then both hands.

    python -E -P tools\\pistol_probe.py U_M9:1 U_MP412Rex:1 U_CZ805:0

Needs a level with the mod loaded; no focus. free_hands and use_reach are switched off while it runs and set back to
their earlier values; the gun in each slot is not given back.
"""
import math
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from camera_probe import command  # noqa: E402

RIGHT = (0.085, -0.222, -0.300)
CUP = (-0.03, -0.04, -0.01)  # the off hand under and beside the gun hand


def grab(text, pattern):
    m = re.search(pattern, text)
    return m.group(0) if m else '-'


def setting_value(name):
    m = re.search(re.escape(name) + r' = (\S+)', command('setting get ' + name))
    return m.group(1) if m else None


def barrel_yaw():
    m = re.search(r'barrel=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', command('twohand'))
    if not m:
        return float('nan')
    x, _, z = (float(m.group(k)) for k in (1, 2, 3))
    return math.degrees(math.atan2(x, -z))


def state():
    t = command('twohand')
    r = command('reload')
    return ' | '.join([grab(t, r'two_handed=\d held=\d weight=[\d.]+'), grab(t, r'pistol=\d'), grab(t, r'input tracked=\d grip=\d busy=\d'),
                       grab(r, r'weapon=\S+'), grab(r, r'zone=\S+ snap=\S+ busy=\d'), grab(r, r'magazine_in=\d magazine=\d+')])


def hands(right_yaw=0.0, left_yaw=0.0):
    left = [RIGHT[k] + CUP[k] for k in range(3)]
    command('fakehand %.1f 0 %.3f %.3f %.3f %.3f %.3f %.3f %.1f 0 0' % (right_yaw, *left, *RIGHT, left_yaw))


def run(gun, slot):
    hands()
    print('> givegun %s %d -> %s' % (gun, slot, command(f'givegun {gun} {slot}').strip()[:24]), flush=True)
    time.sleep(3)
    command('pad 0 0 0 0 300 0000')
    time.sleep(0.5)
    print('  cupped, open hand: ' + state(), flush=True)
    command('pad 0 0 0 0 9000 0300')
    time.sleep(0.6)
    print('  cupped, grip closed: ' + state(), flush=True)
    yaw0 = barrel_yaw()
    hands(right_yaw=4.0)
    time.sleep(0.6)
    yaw1 = barrel_yaw()
    hands(right_yaw=4.0, left_yaw=4.0)
    time.sleep(0.6)
    yaw2 = barrel_yaw()
    print('  barrel yaw: still %.2f, gun hand +4 -> %+.2f, both +4 -> %+.2f' % (yaw0, yaw1 - yaw0, yaw2 - yaw0), flush=True)
    hands()
    command('pad 0 0 0 0 300 0000')
    time.sleep(0.6)
    print('  let go: ' + state(), flush=True)


def main():
    guns = sys.argv[1:] or ['U_M9:1', 'U_MP412Rex:1', 'U_CZ805:0']
    print('### pistol_probe ' + time.strftime('%H:%M:%S'), flush=True)
    saved = {name: setting_value(name) for name in ('free_hands', 'use_reach')}
    command('fakehead 0.064')
    command('setting set free_hands 0')
    command('setting set use_reach 0')
    try:
        for item in guns:
            name, _, slot = item.partition(':')
            run(name, int(slot or 1))
    finally:
        for name, value in saved.items():
            if value is not None:
                command('setting set %s %s' % (name, value))
        command('fakehand off')
        command('fakehead off')
        print('restored: ' + ', '.join('%s=%s' % (n, setting_value(n)) for n in saved), flush=True)


if __name__ == '__main__':
    main()
