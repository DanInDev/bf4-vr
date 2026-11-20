"""Turn bolt in four steps (#96), live: for each bolt-action gun a real shot (the game's own bolt cycle must stay hidden),
then the fake off hand takes the knob at rest, turns it up, draws it back, pushes it forward, tries the trigger, turns
it down and fires. Prints the reload `bolt` line and the `arms` pump/slide line at each step.

    python -E -P tools\\bolt_probe.py U_SRS U_Scout

The knob's world points come from the `bolt` report line (knob_rest, knob_up); "back" is the gun's barrel axis taken as
the view's +z (the fake hold aims straight ahead). Needs a level with the mod loaded; no focus. free_hands and use_reach
are switched off while it runs and set back to their earlier values.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from camera_probe import command  # noqa: E402

HOLD_LEFT = (0.007, -0.214, -0.600)
HOLD_RIGHT = (0.085, -0.222, -0.300)


def vec(text, name):
    m = re.search(name + r'=\(([-\d.]+),([-\d.]+),([-\d.]+)\)', text)
    return [float(m.group(k)) for k in (1, 2, 3)] if m else None


def grab(text, pattern):
    m = re.search(pattern, text)
    return m.group(0) if m else '-'


def setting_value(name):
    m = re.search(re.escape(name) + r' = (\S+)', command('setting get ' + name))
    return m.group(1) if m else None


def state():
    r = command('reload')
    a = command('arms')
    f = command('fingers')
    return ' | '.join([grab(r, r'magazine=\d+ chamber=\d+'), grab(r, r'bolt steps=\d+ up=\d+ back=\d+ held=\d+ lift=[\d.]+ turn_deg=[-\d.]+ pull=[\d.]+'),
                       grab(a, r'drawn_z=[-\d.]+') + ' ' + grab(a, r'drawn_turn_deg=[-\d.]+'),
                       grab(f, r'handle_hold=[\d.]+') + ' ' + grab(f, r'off task=\S+'), grab(r, r'recent:.*')[-60:]])


def hands(left):
    command('fakehand 0 0 %.3f %.3f %.3f %.3f %.3f %.3f' % (left[0], left[1], left[2], *HOLD_RIGHT))


def place(left, target_of):
    dist = 9.0
    for _ in range(10):
        hands(left)
        time.sleep(0.3)
        r = command('reload')
        target, palm = target_of(r), vec(r, 'palm')
        if not target or not palm:
            return dist
        d = [target[k] - palm[k] for k in range(3)]
        dist = sum(x * x for x in d) ** 0.5
        if dist < 0.008:
            return dist
        for k in range(3):
            left[k] += 0.7 * d[k]
    return dist


def move(left, delta, steps, label):
    start = list(left)
    for k in range(1, steps + 1):
        for i in range(3):
            left[i] = start[i] + delta[i] * k / steps
        hands(left)
        time.sleep(0.12)
    time.sleep(0.2)
    print('  %s: %s' % (label, state()), flush=True)


def shot(label):
    """Fire and sample the bolt bone for 1.8 s: the game's animated z (its own cycle) against the drawn one."""
    command('pad 0 0 0 0 200 0300 0 1')
    animated, drawn = [], []
    t0 = time.time()
    while time.time() - t0 < 1.8:
        a = command('arms')
        m = re.search(r'slide_animated_z=([-\d.]+) drawn_z=([-\d.]+)', a)
        t = re.search(r'animated_turn_deg=([-\d.]+) drawn_turn_deg=([-\d.]+)', a)
        if m and t:
            animated.append((float(m.group(1)), float(t.group(1))))
            drawn.append((float(m.group(2)), float(t.group(2))))
        time.sleep(0.06)
    span = ('game: z %.3f..%.3f turn up to %.1f deg; drawn: z %.3f..%.3f turn up to %.1f deg (%d reads)' % (
        min(a[0] for a in animated), max(a[0] for a in animated), max(a[1] for a in animated),
        min(d[0] for d in drawn), max(d[0] for d in drawn), max(d[1] for d in drawn), len(animated)) if animated else 'no arms reads')
    print('  %s: %s || %s' % (label, span, state()), flush=True)


def run(gun):
    left = list(HOLD_LEFT)
    hands(left)
    print('> givegun %s -> %s' % (gun, command(f'givegun {gun} 0').strip()[:24]), flush=True)
    time.sleep(3)
    command('pad 0 0 0 0 9500 0300')
    time.sleep(0.5)
    if ' chamber=0' in state():
        command('reload rack')  # a round in, the bolt locked, so the first shot fires
        time.sleep(0.3)
    print('  start: ' + state(), flush=True)
    shot('a real shot (the game\'s own bolt cycle must stay hidden)')
    command('pad 0 0 0 0 300 0000')
    time.sleep(0.4)
    r = command('reload')
    rest, up = vec(r, 'knob_rest'), vec(r, 'knob_up')
    if not rest or not up:
        print('  no bolt line', flush=True)
        return
    dist = place(left, lambda rr: vec(rr, 'knob_rest'))
    print('  hand at the knob (%.3f m off): %s' % (dist, state()), flush=True)
    command('pad 0 0 0 0 9500 0300')
    time.sleep(0.4)
    move(left, [0.0, 0.0, 0.08], 4, 'pulled while locked (8 cm)')
    move(left, [0.0, 0.0, -0.08], 4, 'back to the knob')
    move(left, [up[i] - rest[i] for i in range(3)], 10, 'turned up')
    move(left, [0.0, 0.0, 0.11], 10, 'drawn back 11 cm')
    move(left, [0.0, 0.0, -0.11], 10, 'pushed forward')
    shot('trigger before turning down')
    command('pad 0 0 0 0 9500 0300')
    time.sleep(0.3)
    move(left, [rest[i] - up[i] for i in range(3)], 10, 'turned down')
    # a second cycle on the live round: back ejects it (round_ejected), forward feeds the next
    move(left, [up[i] - rest[i] for i in range(3)], 10, 'live round: turned up')
    move(left, [0.0, 0.0, 0.11], 10, 'live round: drawn back')
    move(left, [0.0, 0.0, -0.11], 10, 'live round: pushed forward')
    move(left, [rest[i] - up[i] for i in range(3)], 10, 'live round: turned down')
    command('pad 0 0 0 0 300 0000')
    time.sleep(0.4)
    shot('trigger, locked')


def main():
    guns = sys.argv[1:] or ['U_SRS']
    print('### bolt_probe ' + time.strftime('%H:%M:%S'), flush=True)
    saved = {name: setting_value(name) for name in ('free_hands', 'use_reach')}
    command('fakehead 0.064')
    command('setting set free_hands 0')
    command('setting set use_reach 0')
    try:
        for gun in guns:
            run(gun)
    finally:
        for name, value in saved.items():
            if value is not None:
                command('setting set %s %s' % (name, value))
        command('fakehand off')
        command('fakehead off')
        print('restored: ' + ', '.join('%s=%s' % (n, setting_value(n)) for n in saved), flush=True)


if __name__ == '__main__':
    main()
