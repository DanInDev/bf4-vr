"""Find every copy of the current camera rotation in live memory (game must run with the DLL).

    python -E -P tools\\matrix_search.py                       renderer, camera manager, soldier; 2 hops
    python -E -P tools\\matrix_search.py --depth 3 --save a.json
    python -E -P tools\\matrix_search.py --compare a.json b.json   which copies differ between two runs

A copy is three 16-byte-aligned float4 rows whose xyz equal the camera's right/up/forward rows
(`rows`) or their transpose (`view`, as in a view matrix). Use it to find the first-person camera:
change the world camera with a pipe override and see which copies do not follow.
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from aim_probe import CAMERA_MANAGER, GAME_RENDERER, LOCAL_SOLDIER, REGION, class_sizes, read_blocks, regions  # noqa: E402
from camera_probe import peek, pointer  # noqa: E402
from object_probe import load_vtables  # noqa: E402


def camera_rows():
    view = pointer(pointer(GAME_RENDERER) + 0x60)
    floats = struct.unpack('<16f', peek(view + 0x40, 64))
    return [floats[0:3], floats[4:7], floats[8:11]], floats[12:15]


def close(a, b, tolerance):
    return all(abs(x - y) < tolerance for x, y in zip(a, b))


def search(blocks, data, rows, tolerance):
    transposed = [tuple(rows[r][c] for r in range(3)) for c in range(3)]
    hits = []
    for name, blob in data.items():
        address = blocks[name][0]
        start = (-address) % 16  # rows are 16-byte aligned in absolute memory
        for offset in range(start, len(blob) - 47, 16):
            m = [struct.unpack_from('<3f', blob, offset + 16 * r) for r in range(3)]
            for label, target in (('rows', rows), ('view', transposed)):
                if all(close(m[r], target[r], tolerance) for r in range(3)):
                    fourth = struct.unpack_from('<3f', blob, offset + 48) if offset + 60 <= len(blob) else ()
                    hits.append({'block': name, 'address': address + offset, 'offset': offset, 'form': label,
                                 'fourth': [round(v, 3) for v in fourth]})
    return hits


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--depth', type=int, default=2)
    parser.add_argument('--tolerance', type=float, default=2e-3)
    parser.add_argument('--save')
    parser.add_argument('--compare', nargs=2, metavar=('A', 'B'))
    args = parser.parse_args()

    if args.compare:
        a, b = (json.load(open(path)) for path in args.compare)
        key = lambda h: (h['address'], h['form'], tuple(h['fourth']))  # noqa: E731
        in_b = {key(h) for h in b['hits']}
        in_a = {key(h) for h in a['hits']}
        print('only in A:'); [print(f"  {h['address']:#x} {h['form']} {h['block']}+{h['offset']:#x} fourth={h['fourth']}") for h in a['hits'] if key(h) not in in_b]
        print('only in B:'); [print(f"  {h['address']:#x} {h['form']} {h['block']}+{h['offset']:#x} fourth={h['fourth']}") for h in b['hits'] if key(h) not in in_a]
        print(f"in both: {len(in_a & in_b)}")
        return

    sizes = class_sizes()
    renderer = pointer(GAME_RENDERER)
    roots = {
        'renderer': (renderer, 0x2000),
        'view params': (pointer(renderer + 0x60), 0x1100),
        'cameras': (pointer(CAMERA_MANAGER), REGION),
        'soldier': (pointer(LOCAL_SOLDIER), sizes['ClientSoldierEntity']),
    }
    blocks = regions(load_vtables(None), args.depth, roots)
    rows, position = camera_rows()
    data = read_blocks(blocks)
    hits = search(blocks, data, rows, args.tolerance)
    print(f'camera position {[round(p, 3) for p in position]}; {len(blocks)} blocks; {len(hits)} copies', file=sys.stderr)
    for h in hits:
        print(f"{h['address']:#x} {h['form']:4} {h['block']}+{h['offset']:#x} fourth={h['fourth']}")
    if args.save:
        with open(args.save, 'w') as f:
            json.dump({'rows': rows, 'position': position, 'hits': hits}, f, indent=1)


if __name__ == '__main__':
    main()
