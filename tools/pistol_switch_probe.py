"""#99, the ordinary path: a pistol put into the sidearm slot while a rifle is in hand, then a real weapon switch to it
(BF4 brought to the front, key 2), then the separation the game holds (`arms` support line) sampled for 6 s and the cup
test (the off hand cupped, the grip, `twohand` held/pistol). Run right after a level load. Takes BF4's focus: ask first.

    python -E -P tools\\pistol_switch_probe.py U_P226
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from camera_probe import command  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
RIGHT = (0.085, -0.222, -0.300)
CUP = (-0.03, -0.04, -0.01)


def grab(text, pattern):
    m = re.search(pattern, text)
    return m.group(0) if m else '-'


def setting_value(name):
    m = re.search(re.escape(name) + r' = (\S+)', command('setting get ' + name))
    return m.group(1) if m else None


def line():
    t = command('twohand')
    a = command('arms')
    r = command('reload')
    return ' | '.join([grab(r, r'weapon=\S+ slot=\d+'), grab(t, r'two_handed=\d held=\d weight=[\d.]+'), grab(t, r'pistol=\d along=[-\d.]+'),
                       grab(a, r'support along_now=[-\d.]+ along_hold=[-\d.]+ cupping=\d held=\d solved_wrist_in_gun=\([^)]*\) game_hand_in_gun=\([^)]*\)')])


def keys(k):
    out = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(HERE, 'game.ps1'), 'keys', '-Keys', k],
                         capture_output=True, text=True)
    return (out.stdout or out.stderr).strip()[-60:]


def main():
    gun = sys.argv[1] if len(sys.argv) > 1 else 'U_P226'
    print('### pistol_switch_probe ' + time.strftime('%H:%M:%S'), flush=True)
    saved = {name: setting_value(name) for name in ('free_hands', 'use_reach')}
    command('fakehead 0.064')
    command('setting set free_hands 0')
    command('setting set use_reach 0')
    left = [RIGHT[k] + CUP[k] for k in range(3)]
    command('fakehand 0 0 %.3f %.3f %.3f %.3f %.3f %.3f' % (*left, *RIGHT))
    try:
        print('  in hand: ' + line(), flush=True)
        if len(sys.argv) > 2 and sys.argv[2] == 'switch-only':
            print('  (the pistol is already in the sidearm slot)', flush=True)
        else:
            print('  givegun %s 1 (the sidearm slot) -> %s' % (gun, command(f'givegun {gun} 1').strip()[:30]), flush=True)
            time.sleep(1.0)
            print('  after the give: ' + line(), flush=True)
            if ' slot=1' in line():
                print('  switch back to the primary (key 1): ' + keys('1'), flush=True)
                time.sleep(2.5)
                print('  primary in hand: ' + line(), flush=True)
        print('  switch to the sidearm (key 2): ' + keys('2'), flush=True)
        t0 = time.time()
        while time.time() - t0 < 6.0:
            time.sleep(1.0)
            print('  %.0f s: %s' % (time.time() - t0, line()), flush=True)
        command('pad 0 0 0 0 4000 0300')
        time.sleep(0.8)
        print('  cupped, grip closed: ' + line(), flush=True)
        command('pad 0 0 0 0 300 0000')
        time.sleep(0.6)
        print('  let go: ' + line(), flush=True)
    finally:
        for name, value in saved.items():
            if value is not None:
                command('setting set %s %s' % (name, value))
        command('fakehand off')
        command('fakehead off')
        print('restored: ' + ', '.join('%s=%s' % (n, setting_value(n)) for n in saved), flush=True)


if __name__ == '__main__':
    main()
