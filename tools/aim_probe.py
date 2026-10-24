"""Find the soldier's yaw/pitch storage by turning with the virtual pad (game must run with the DLL,
in gameplay, with a physical pad connected so BF4 polls XInput; no headset session).

    python -E -P tools\\aim_probe.py                 turn right, then look up; list matching floats
    python -E -P tools\\aim_probe.py --axis pitch

Snapshots the local soldier, every object it points to and the camera manager, moves the right
stick, snapshots again and keeps floats whose change equals the camera's yaw (or pitch) change.
"""
import argparse
import glob
import json
import math
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import command, peek, peek_many, pointer  # noqa: E402
from object_probe import load_vtables, plausible_heap  # noqa: E402

GAME_RENDERER = 0x142672378
LOCAL_SOLDIER = 0x14230F260
CAMERA_MANAGER = 0x14230A5F8
REGION = 0x800  # bytes read from untyped objects; typed ones are read at their class size
MAX_REGION = 0x5000
CHUNK = 0x1000  # peekmany limit


def class_sizes():
    path = sorted(glob.glob(os.path.join(HERE, '..', 're', 'dumps', 'typeinfo-*.json')))[-1]
    with open(path) as f:
        return {t['name']: t['size'] for t in json.load(f)['types'] if t['kind'] == 'Class'}


def read_blocks(blocks):
    """{name: bytes} for blocks {name: (address, length)}, read in peekmany-sized chunks."""
    pieces = {}
    for address, length in blocks.values():
        for start in range(0, length, CHUNK):
            pieces.setdefault(min(CHUNK, length - start), set()).add(address + start)
    data = {}
    for size, addresses in pieces.items():
        data.update({(a, size): b for a, b in peek_many(addresses, size).items()})
    result = {}
    for name, (address, length) in blocks.items():
        parts = [data.get((address + s, min(CHUNK, length - s))) for s in range(0, length, CHUNK)]
        joined = b''
        for part in parts:  # keep the readable prefix
            if part is None:
                break
            joined += part
        if joined:
            result[name] = joined
    return result


def camera_angles():
    """Yaw and pitch (radians) of the rendered camera; the view looks along -z of its frame."""
    view = pointer(pointer(GAME_RENDERER) + 0x60)
    rows = struct.unpack('<12f', peek(view + 0x40, 48))
    look = [-rows[8], -rows[9], -rows[10]]
    return math.atan2(look[0], look[2]), math.asin(max(-1.0, min(1.0, look[1])))


def regions(vtables, depth, roots=None):
    """name -> (address, length) of every block to compare: the roots (default: the soldier and the
    camera manager) plus the objects they point to, `depth` pointer hops deep."""
    sizes = class_sizes()
    if roots is None:
        roots = {'soldier': (pointer(LOCAL_SOLDIER), sizes['ClientSoldierEntity']),
                 'cameras': (pointer(CAMERA_MANAGER), REGION)}
    found = dict(roots)
    seen = {address for address, _ in roots.values()}
    frontier = dict(roots)
    for _ in range(depth):
        bodies = read_blocks(frontier)
        children = {}
        for name, blob in bodies.items():
            for offset in range(0, len(blob) - 7, 8):
                target = struct.unpack_from('<Q', blob, offset)[0]
                if plausible_heap(target) and target not in seen:
                    seen.add(target)
                    children[f'{name}+{offset:#x}'] = target
        heads = peek_many(children.values(), 8)
        frontier = {}
        for name, address in children.items():
            if heads.get(address):
                label = vtables.get(struct.unpack('<Q', heads[address])[0])
                length = min(sizes.get(label, REGION), MAX_REGION) if label else REGION
                frontier[f'{name}({label})' if label else name] = (address, length)
        found.update(frontier)
    return found


def snapshot(blocks):
    return read_blocks(blocks)


def wrap(angle):
    return (angle + math.pi) % (2 * math.pi) - math.pi


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--axis', choices=('yaw', 'pitch'), default='yaw')
    parser.add_argument('--amount', type=float, default=0.6, help='stick deflection')
    parser.add_argument('--ms', type=int, default=300, help='how long the stick is held')
    parser.add_argument('--static', action='store_true',
                        help='no stick: list floats equal to the current yaw/pitch (up to sign and 2*pi)')
    parser.add_argument('--depth', type=int, default=2, help='pointer hops below soldier and camera manager')
    args = parser.parse_args()

    blocks = regions(load_vtables(None), args.depth)
    print(f'{len(blocks)} blocks of {REGION:#x} bytes', file=sys.stderr)
    yaw0, pitch0 = camera_angles()
    if args.static:
        print(f'camera yaw {math.degrees(yaw0):.2f} pitch {math.degrees(pitch0):.2f} deg')
        targets = {'yaw': yaw0, 'pitch': pitch0}
        for name, blob in snapshot(blocks).items():
            for offset in range(0, len(blob) - 3, 4):
                value = struct.unpack_from('<f', blob, offset)[0]
                if not math.isfinite(value) or not 0.01 < abs(value) < 10:
                    continue
                for label, target in targets.items():
                    for sign in (1, -1):
                        if abs(wrap(value - sign * target)) < 0.002:
                            print(f'  {name} @ {blocks[name][0]:#x} +{offset:#x}: {value:.4f} = {"-" if sign < 0 else ""}{label}')
        return
    before = snapshot(blocks)
    stick = f'0 0 {args.amount} 0' if args.axis == 'yaw' else f'0 0 0 {args.amount}'
    print(command(f'pad {stick} {args.ms}'))
    time.sleep(args.ms / 1000 + 0.4)
    yaw1, pitch1 = camera_angles()
    after = snapshot(blocks)
    delta = wrap(yaw1 - yaw0) if args.axis == 'yaw' else pitch1 - pitch0
    print(f'camera yaw {math.degrees(yaw0):.2f} -> {math.degrees(yaw1):.2f}, '
          f'pitch {math.degrees(pitch0):.2f} -> {math.degrees(pitch1):.2f}; {args.axis} change {math.degrees(delta):.2f} deg')
    if abs(delta) < math.radians(1):
        raise SystemExit('camera did not turn; is a physical pad connected and the game in focus-free gameplay?')

    for name, old in before.items():
        new = after.get(name)
        if new is None:
            continue
        for offset in range(0, min(len(old), len(new)) - 3, 4):
            a = struct.unpack_from('<f', old, offset)[0]
            b = struct.unpack_from('<f', new, offset)[0]
            if not (math.isfinite(a) and math.isfinite(b)) or a == b:
                continue
            change = wrap(b - a) if args.axis == 'yaw' else b - a
            if abs(abs(change) - abs(delta)) < 0.02 * abs(delta) + 1e-4:
                sign = '+' if change * delta > 0 else '-'
                print(f'  {name} @ {blocks[name][0]:#x} +{offset:#x}: {a:.4f} -> {b:.4f} ({sign})')


if __name__ == '__main__':
    main()
