"""Charging handle from either side (#97): for each gun, the off hand (fake controllers) at the handle zone mirrored across
the gun, then grip, pull back 8 cm, let go. Prints the reload state at each step (zone, mirrored, racking, events).

    python -E -P tools\\handle_mirror_probe.py U_FAMAS U_FAMAS:0 U_P90 U_SCAR-H

A gun name with ":0" runs with the setting handle_both_sides off. The mirrored point is the gun's own handle zone (read
with the hand well away, so mirrored=0) reflected across the magwell's x: the magwell lies on the gun's mid-plane and
the gun points about straight ahead in the fake hold. Needs fakehead/fakehand; free_hands and use_reach are switched
off while it runs and every setting is restored at the end. BF4 must be in a level with the mod loaded.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from camera_probe import command  # noqa: E402

HOLD_LEFT = (0.007, -0.214, -0.600)
HOLD_RIGHT = (0.085, -0.222, -0.300)
AWAY_LEFT = (0.25, -0.45, -0.35)


def vec(text, name):
    m = re.search(name + r'=\(([-\d.]+),([-\d.]+),([-\d.]+)\)', text)
    return [float(m.group(k)) for k in (1, 2, 3)] if m else None


def grab(text, pattern):
    m = re.search(pattern, text)
    return m.group(0) if m else '-'


def state():
    r = command('reload')
    return ' | '.join([grab(r, r'magazine=\d+ chamber=\d+'), grab(r, r'zone=\S+ snap=\S+ busy=\d+ racking=\d+ pull=[\d.]+'),
                       grab(r, r'rack underhand=\d+ mirrored=\d+'), grab(r, r'handle_zone=\([^)]*\)'), grab(r, r'recent:.*')[-45:]])


def hands(left, dz=0.0):
    command('fakehand 0 0 %.3f %.3f %.3f %.3f %.3f %.3f' % (left[0], left[1], left[2] + dz, *HOLD_RIGHT))


def place(left, target):
    dist = 9.0
    for _ in range(10):
        hands(left)
        time.sleep(0.3)
        palm = vec(command('reload'), 'palm')
        if not palm:
            return dist
        d = [target[k] - palm[k] for k in range(3)]
        dist = sum(x * x for x in d) ** 0.5
        if dist < 0.01:
            return dist
        for k in range(3):
            left[k] += 0.7 * d[k]
    return dist


def run(gun, setting):
    command('setting set handle_both_sides %d' % setting)
    hands(HOLD_LEFT)
    print('> givegun %s (handle_both_sides %d) -> %s' % (gun, setting, command(f'givegun {gun} 0').strip()[:20]), flush=True)
    time.sleep(3)
    command('pad 0 0 0 0 300 0000')
    hands(AWAY_LEFT)
    time.sleep(0.8)
    r = command('reload')
    zone, well = vec(r, 'handle_zone'), vec(r, 'magwell')
    print('  own side (hand away): %s %s %s %s' % (grab(r, r'handle_zone=\([^)]*\)'), grab(r, r'magwell=\([^)]*\)'),
                                                 grab(r, r'mirrored=\d'), grab(r, r'handle_in_gun=\S+')), flush=True)
    if not zone or not well:
        print('  no handle zone', flush=True)
        return
    target = [2 * well[0] - zone[0], zone[1], zone[2]]
    print('  mirrored target: (%.3f,%.3f,%.3f)' % tuple(target), flush=True)
    left = list(HOLD_LEFT)
    dist = place(left, target)
    print('  hand on the mirrored zone (%.3f m off): %s' % (dist, state()), flush=True)
    command('pad 0 0 0 0 4000 0300')
    time.sleep(0.4)
    print('  grip: ' + state(), flush=True)
    for k in range(1, 5):
        hands(left, 0.02 * k)
        time.sleep(0.15)
    print('  pulled 8 cm: ' + state(), flush=True)
    command('pad 0 0 0 0 200 0000')
    time.sleep(0.4)
    print('  let go: ' + state(), flush=True)


def main():
    guns = sys.argv[1:] or ['U_FAMAS', 'U_FAMAS:0', 'U_SCAR-H']
    print('### handle_mirror_probe ' + time.strftime('%H:%M:%S'), flush=True)
    command('fakehead 0.064')
    command('setting set free_hands 0')
    command('setting set use_reach 0')
    try:
        for item in guns:
            name, _, flag = item.partition(':')
            run(name, 0 if flag == '0' else 1)
    finally:
        command('setting set handle_both_sides 1')
        command('setting set free_hands 1')
        command('setting set use_reach 1')
        command('fakehand off')
        command('fakehead off')
        print('restored: ' + command('setting get handle_both_sides').strip()[:28], flush=True)


if __name__ == '__main__':
    main()
