"""Find bone pose arrays (runs of unit quaternions at a fixed stride) behind the soldier's animation
components (game must run with the DLL).

    python -E -P tools\\pose_search.py                       report runs of >= 16 bones
    python -E -P tools\\pose_search.py --save a.json          then again during a reload:
    python -E -P tools\\pose_search.py --compare a.json b.json   which runs changed

A pose array is >= --min consecutive records of `stride` bytes (0x20/0x30/0x40) starting with a unit
quaternion (x, y, z, w). Roots: soldier+0x1E8 / +0x1F0 (ClientAntAnimatableComponent), --depth hops.
"""
import argparse
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

import aim_probe  # noqa: E402
from aim_probe import LOCAL_SOLDIER, class_sizes, read_blocks, regions  # noqa: E402
from camera_probe import pointer  # noqa: E402
from object_probe import load_vtables  # noqa: E402

STRIDES = (0x20, 0x30, 0x40)


def unit_quaternion(blob, offset):
    q = struct.unpack_from('<4f', blob, offset)
    if not all(math.isfinite(v) for v in q):
        return None
    norm = math.sqrt(sum(v * v for v in q))
    return q if abs(norm - 1.0) < 2e-3 and abs(q[3]) < 1.0 else None  # skip pure identities


def runs(blob, base, minimum):
    found = []
    for stride in STRIDES:
        for start in range((-base) % 16, min(stride, len(blob)), 16):
            count, first = 0, None
            for offset in range(start, len(blob) - 15, stride):
                if unit_quaternion(blob, offset):
                    if count == 0:
                        first = offset
                    count += 1
                else:
                    if count >= minimum:
                        found.append((first, stride, count))
                    count = 0
            if count >= minimum:
                found.append((first, stride, count))
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--depth', type=int, default=3)
    parser.add_argument('--region', type=lambda s: int(s, 0), default=0x2000, help='bytes read from untyped objects')
    parser.add_argument('--min', type=int, default=16)
    parser.add_argument('--save')
    parser.add_argument('--compare', nargs=2, metavar=('A', 'B'))
    args = parser.parse_args()

    if args.compare:
        a, b = (json.load(open(path)) for path in args.compare)
        for key, run in a.items():
            other = b.get(key)
            if other and other['data'] != run['data']:
                changed = sum(x != y for x, y in zip(run['data'], other['data']))
                print(f"{key}: {changed} of {len(run['data'])} floats changed")
        return

    aim_probe.REGION = args.region
    sizes = class_sizes()
    soldier = pointer(LOCAL_SOLDIER)
    roots = {f'soldier+{off:#x}': (pointer(soldier + off), sizes['ClientAntAnimatableComponent']) for off in (0x1E8, 0x1F0)}
    blocks = regions(load_vtables(None), args.depth, roots)
    data = read_blocks(blocks)
    saved = {}
    seen = set()
    for name, blob in data.items():
        base = blocks[name][0]
        for first, stride, count in runs(blob, base, args.min):
            address = base + first
            if address in seen:
                continue
            seen.add(address)
            count = min(count, (len(blob) - first) // stride)  # the last record may be cut off
            floats = struct.unpack_from(f'<{count * stride // 4}f', blob, first)
            print(f'{address:#x} stride {stride:#x} x{count:3}  {name}+{first:#x}  first q={[round(v, 3) for v in floats[:4]]} t={[round(v, 3) for v in floats[4:7]]}')
            saved[f'{address:#x}/{stride:#x}'] = {'block': name, 'stride': stride, 'count': count, 'data': [round(v, 5) for v in floats]}
    print(f'{len(blocks)} blocks, {len(saved)} runs', file=sys.stderr)
    if args.save:
        with open(args.save, 'w') as f:
            json.dump(saved, f)


if __name__ == '__main__':
    main()
