"""Find live objects of a reflected class behind a root pointer (game must run with the DLL).

    python -E -P tools\\find_typed.py SkeletonAsset --root soldier+0x1f0 --depth 4
    python -E -P tools\\find_typed.py WorldRenderSettings --root [0x14261e930] --depth 3

Each object's class is read live from its getType() (vtable slot 0: `lea rax,[ClassInfo]; ret`),
so this also names data objects whose vtables the static map in typeinfo-*.json misses.
Roots: `soldier+<off>` (pointer at local soldier + off), `[<global>]`, or a plain address.
"""
import argparse
import glob
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import peek_many, pointer  # noqa: E402
from object_probe import plausible_heap  # noqa: E402

LOCAL_SOLDIER = 0x14230F260


def class_addresses():
    path = sorted(glob.glob(os.path.join(HERE, '..', 're', 'dumps', 'typeinfo-*.json')))[-1]
    with open(path) as f:
        return {t['address']: t['name'] for t in json.load(f)['types'] if t['kind'] == 'Class'}


def resolve_root(text):
    if text.startswith('soldier+'):
        return pointer(pointer(LOCAL_SOLDIER) + int(text[len('soldier+'):], 0))
    if text.startswith('[') and text.endswith(']'):
        return pointer(int(text[1:-1], 0))
    return int(text, 0)


def live_classes(objects, classes):
    """{object: class name} from each object's getType() code."""
    heads = peek_many(objects, 8)
    vtables = {a: struct.unpack('<Q', b)[0] for a, b in heads.items() if b}
    slots = peek_many({v for v in vtables.values() if 0x140000000 <= v < 0x143000000}, 8)
    functions = {v: struct.unpack('<Q', b)[0] for v, b in slots.items() if b}
    code = peek_many(set(functions.values()), 8)
    named = {}
    for obj, vtable in vtables.items():
        body = code.get(functions.get(vtable))
        if body and body[:3] == b'\x48\x8d\x05' and body[7] == 0xC3:
            target = functions[vtable] + 7 + struct.unpack_from('<i', body, 3)[0]
            if target in classes:
                named[obj] = classes[target]
    return named


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('class_name', help='class to find (substring match unless --exact)')
    parser.add_argument('--root', required=True)
    parser.add_argument('--depth', type=int, default=3)
    parser.add_argument('--region', type=lambda s: int(s, 0), default=0x400, help='bytes scanned per object for pointers')
    parser.add_argument('--exact', action='store_true')
    args = parser.parse_args()

    classes = class_addresses()
    root = resolve_root(args.root)
    paths = {root: args.root}
    frontier = [root]
    for depth in range(1, args.depth + 1):
        bodies = peek_many(frontier, args.region)
        children = []
        for parent, blob in bodies.items():
            for offset in range(0, len(blob or b'') - 7, 8):
                value = struct.unpack_from('<Q', blob, offset)[0]
                if plausible_heap(value) and value not in paths:
                    paths[value] = f'{paths[parent]}+{offset:#x}'
                    children.append(value)
        named = live_classes(children, classes)
        for obj, name in named.items():
            if (name == args.class_name) if args.exact else (args.class_name.lower() in name.lower()):
                print(f'{obj:#x} {name:40} via {paths[obj]}')
        frontier = children
        print(f'depth {depth}: {len(children)} objects', file=sys.stderr)


if __name__ == '__main__':
    main()
