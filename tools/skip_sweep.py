"""Research: find which second-eye draws draw a screen region by skipping draw ranges (pipe eyeskip).

    python -E -P tools\\skip_sweep.py --box 700 300 1150 600 --chunk 50

Pair rendering must be on and a level loaded. Prints, per draw range, how much the box and the whole
image change against the unskipped image; then bisects the range that changes the box most.
"""
import argparse
import os
import subprocess
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIPE = os.path.join(ROOT, 'tools', 'pipe.ps1')


def pipe(command, *args):
    line = [command] + ([' '.join(str(a) for a in args)] if args else [])
    out = subprocess.run(['powershell', '-NoProfile', '-File', PIPE, *line], capture_output=True, text=True)
    return out.stdout.strip()


def shot(path):
    pipe('shot', path)
    return np.asarray(Image.open(path).convert('L'), dtype=np.float32)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--box', type=int, nargs=4, required=True)
    parser.add_argument('--chunk', type=int, default=50)
    args = parser.parse_args()
    x0, y0, x1, y1 = args.box
    tmp = tempfile.mkdtemp()
    total = 0
    while total == 0:  # the count is reset at the start of each second eye
        reply = pipe('eyeskip', 0, 0)
        total = int(reply.split('count ')[-1].rstrip(')'))
    print(reply)
    base = shot(os.path.join(tmp, 'base.png'))

    def score(a, b):
        skipped = shot(os.path.join(tmp, f'skip-{a}-{b}.png'))
        return float(np.mean(np.abs(skipped[y0:y1, x0:x1] - base[y0:y1, x0:x1]))), float(np.mean(np.abs(skipped - base)))

    results = []
    for a in range(0, total, args.chunk):
        b = min(total, a + args.chunk)
        pipe('eyeskip', a, b)
        box, whole = score(a, b)
        results.append((box, whole, a, b))
        print(f'{a:5}-{b:5} box {box:7.2f} whole {whole:7.2f}')
    box, whole, a, b = max(results)
    # Bisect the strongest range down to single draws.
    while b - a > 1:
        m = (a + b) // 2
        pipe('eyeskip', a, m)
        left = score(a, m)[0]
        pipe('eyeskip', m, b)
        right = score(m, b)[0]
        print(f'  {a}-{m}: {left:.2f}  {m}-{b}: {right:.2f}')
        a, b = (a, m) if left >= right else (m, b)
    pipe('eyeskip', 0, 0)
    print('strongest single draw:', a)


if __name__ == '__main__':
    main()
