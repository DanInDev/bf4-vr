"""Off hand on a foregrip (#91), live: for each gun (given into slot 0) the fake off hand grabs the handguard 6 cm ahead
of the game's own hold, and the `arms` support line says where the drawn off wrist ended up against the game's own
left hand (gun axes). With a foregrip and foregrip_hold on it must sit on the game's spot (within 2 cm); on a bare
handguard it stays where it grabbed (about 6 cm ahead, the slide).

    python -E -P tools\\foregrip_probe.py U_MX4 U_UMP45:2 U_FAMAS:3:off -:auto -:auto:off

A gun named "-" is the gun in hand, not given (a crate gun keeps its attachments; givegun gives none). Field two forces a foregrip kind (pipe `foregrip <n>`: 0 none, 1 vertical, 2 angled, 3 stubby) for a level that loads no
grips (Shanghai); without it the gun's own grip is read (`foregrip auto`). A third field "off" runs with foregrip_hold 0.
Needs a level with the mod loaded; no focus. free_hands, use_reach and foregrip_hold are set back afterwards, the
forced kind returns to auto, fakehand and fakehead are cleared.
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
AHEAD = 0.06  # metres the off hand grabs ahead of the game's hold (support_slide_forward allows 0.08)


def setting_value(name):
    m = re.search(re.escape(name) + r' = (\S+)', command('setting get ' + name))
    return m.group(1) if m else None


def vec(text, name):
    m = re.search(name + r'=\(([-\d.]+),([-\d.]+),([-\d.]+)\)', text)
    return [float(m.group(k)) for k in (1, 2, 3)] if m else None


def grab(text, pattern):
    m = re.search(pattern, text)
    return m.group(0) if m else '-'


def hands(left, dz=0.0):
    command('fakehand 0 0 %.3f %.3f %.3f %.3f %.3f %.3f' % (left[0], left[1], left[2] + dz, *HOLD_RIGHT))


def support():
    a = command('arms')
    drawn, game = vec(a, 'solved_wrist_in_gun'), vec(a, 'game_hand_in_gun')
    line = grab(a, r'support along_now=.*')
    if not drawn or not game or not re.search(r' held=1 ', line):
        return None, line
    return [drawn[k] - game[k] for k in range(3)], line


def run(gun, kind, hold):
    command('setting set foregrip_hold %d' % hold)
    print(command('foregrip %s' % kind).strip(), flush=True)
    hands(HOLD_LEFT)
    if gun == '-':
        print('> the gun in hand (from the game\'s crate, with its attachments)', flush=True)
        time.sleep(1.0)
    else:
        print('> givegun %s 0 -> %s' % (gun, command(f'givegun {gun} 0').strip()[:24]), flush=True)
        time.sleep(3.5)
    print('  ' + command('foregrip').strip(), flush=True)
    command('pad 0 0 0 0 300 0000')
    hands(AWAY_LEFT)
    time.sleep(0.6)
    hands(HOLD_LEFT, -AHEAD)  # camera -z is forward: the hand 6 cm towards the muzzle
    time.sleep(0.4)
    command('pad 0 0 0 0 9000 0300')
    time.sleep(1.0)
    off, line = support()
    print('  ' + line, flush=True)
    if off is None:
        print('  no two-handed hold: not measured', flush=True)
        return None
    dist = sum(x * x for x in off) ** 0.5
    print('  grabbed %d cm ahead (foregrip_hold %d): drawn off wrist %.1f cm from the game\'s hand (x %.1f y %.1f z %.1f cm)'
          % (round(AHEAD * 100), hold, dist * 100, off[0] * 100, off[1] * 100, off[2] * 100), flush=True)
    shots = os.environ.get('FOREGRIP_SHOTS')  # a folder: an eye shot of the hold (the second of two, see CLAUDE.md)
    if shots:
        path = os.path.join(shots, 'foregrip-%s-%s-%d.png' % (gun.strip('-') or 'inhand', kind, hold))
        command('shot ' + path)
        time.sleep(0.6)
        print('  ' + command('shot ' + path).strip()[:60], flush=True)
    command('pad 0 0 0 0 300 0000')
    hands(AWAY_LEFT)
    time.sleep(0.5)
    return dist


def main():
    guns = sys.argv[1:] or ['U_MX4:2', 'U_MX4:0', 'U_MX4:2:off']
    print('### foregrip_probe ' + time.strftime('%H:%M:%S'), flush=True)
    saved = {name: setting_value(name) for name in ('free_hands', 'use_reach', 'foregrip_hold')}
    # FOREGRIP_HEAD_YAW (degrees): the head turned against the hands, so the shots see the gun from its side.
    command('fakehead 0.064 %s' % os.environ.get('FOREGRIP_HEAD_YAW', '0'))
    command('setting set free_hands 0')
    command('setting set use_reach 0')
    results = []
    try:
        for item in guns:
            parts = item.split(':')
            name = parts[0]
            kind = parts[1] if len(parts) > 1 and parts[1] else 'auto'
            hold = 0 if len(parts) > 2 and parts[2] == 'off' else 1
            results.append((item, kind, hold, run(name, kind, hold)))
    finally:
        command('foregrip auto')
        for name, value in saved.items():
            if value is not None:
                command('setting set %s %s' % (name, value))
        command('fakehand off')
        command('fakehead off')
        print('restored: ' + ', '.join('%s=%s' % (n, setting_value(n)) for n in saved) + ', foregrip auto, fakehand and fakehead off', flush=True)
    print('summary:', flush=True)
    ok = True
    for item, kind, hold, dist in results:
        on_grip = hold == 1 and kind != '0' and (kind != 'auto' or item.startswith('-'))
        if dist is None:
            verdict = 'not measured'
            ok = False
        elif on_grip:
            verdict = 'ok' if dist < 0.02 else 'FAIL (over 2 cm)'
            ok = ok and dist < 0.02
        else:
            verdict = 'slides as before' if dist > 0.03 else 'no slide seen'
        print('  %-16s %.1f cm  %s' % (item, (dist or 0) * 100, verdict), flush=True)
    print('RESULT ' + ('ok' if ok else 'FAIL'), flush=True)


if __name__ == '__main__':
    main()
