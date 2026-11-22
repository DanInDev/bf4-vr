"""#144: what firing costs the eye passes. Samples pipe `status` gpu_ms eye1/eye2 (and fps, readback) with the
trigger up, with the game's aim held (LT), firing (RT) and both, optionally with one setting switched per run.

    python -E -P tools\\fire_cost_probe.py [setting=value ...]

Each `setting=value` is set live for its own pass and restored afterwards (never saved). Needs a level with a gun
in hand and `fakehead` on (the eye passes only run with a head)."""
import os
import re
import sys
import time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from camera_probe import command  # noqa: E402

STATES = (
    ('idle', None),
    ('aim', 'pad 0 0 0 0 %d 0000 1 0'),
    ('fire', 'pad 0 0 0 0 %d 0000 0 1'),
    ('aim+fire', 'pad 0 0 0 0 %d 0000 1 1'),
)
HOLD_MS = 4000
SAMPLES = 5


def gpu():
    text = command('status')
    eyes = re.search(r'gpu_ms eye1=([\d.]+) eye2=([\d.]+)', text)
    fps = re.search(r'^fps=([\d.]+)', text, re.M)
    readback = re.search(r'eye_readback_ms=([\d.]+)', text)
    if not eyes:
        return None
    return float(eyes.group(1)), float(eyes.group(2)), float(fps.group(1)) if fps else 0.0, float(readback.group(1)) if readback else 0.0


def shots():
    m = re.search(r'\bshots=(\d+)', command('reload'))
    return int(m.group(1)) if m else -1


def measure(label):
    rows = []
    for name, pad in STATES:
        before = shots()
        if pad:
            command(pad % HOLD_MS)
            time.sleep(1.0)  # the effect builds up
        samples = []
        for _ in range(SAMPLES):
            sample = gpu()
            if sample:
                samples.append(sample)
            time.sleep(0.4)
        command('pad 0 0 0 0 100 0000 0 0')
        time.sleep(1.5)
        fired = shots() - before
        if samples:
            mean = [sum(s[k] for s in samples) / len(samples) for k in range(4)]
            rows.append('%-10s %-9s eye1=%6.2f eye2=%6.2f ms  fps=%5.1f readback=%6.2f  shots=%d' % (label, name, mean[0], mean[1], mean[2], mean[3], fired))
        else:
            rows.append('%-10s %-9s no gpu_ms in status' % (label, name))
        # a fresh magazine for the next state
        if fired > 0:
            command('reload setammo 30')
            time.sleep(0.5)
    return rows


def main():
    print(command('reload').splitlines()[0][:120])
    for row in measure('base'):
        print(row)
    for change in sys.argv[1:]:
        name, value = change.split('=', 1)
        old = re.search(r'=\s*(\S+)', command('setting get %s' % name))
        command('setting set %s %s' % (name, value))
        try:
            for row in measure(change):
                print(row)
        finally:
            if old:
                command('setting set %s %s' % (name, old.group(1)))


if __name__ == '__main__':
    main()
