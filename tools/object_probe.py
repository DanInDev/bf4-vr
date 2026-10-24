"""Name live engine objects through the control pipe (game must run with the DLL).

    python -E -P tools\\object_probe.py 0x142670d80 --deref          object behind a global pointer
    python -E -P tools\\object_probe.py <address> --size 0x400 --depth 1

Prints the object's class (from its vtable, see typeinfo_dump.py) and every 8-byte slot that points
at another typed object. Needs the JSON written by `typeinfo_dump.py <dump>`.
"""
import argparse
import glob
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import peek, pointer  # noqa: E402


def load_vtables(path):
    if not path:
        candidates = sorted(glob.glob(os.path.join(HERE, '..', 're', 'dumps', 'typeinfo-*.json')))
        if not candidates:
            raise SystemExit('no typeinfo JSON; run tools\\typeinfo_dump.py <dump> first')
        path = candidates[-1]
    with open(path) as f:
        return {int(k, 16): v for k, v in json.load(f)['vtables'].items()}


def class_of(address, vtables):
    try:
        return vtables.get(pointer(address))
    except RuntimeError:
        return None


def plausible_heap(value):
    return 0x10000 <= value < 0x7FF000000000 and value % 8 == 0 and not 0x140000000 <= value < 0x143000000


def probe(address, size, vtables, depth, indent=''):
    print(f'{indent}{address:#x}: {class_of(address, vtables) or "(untyped)"}')
    blob = peek(address, size)
    for offset in range(0, len(blob) - 7, 8):
        value = struct.unpack_from('<Q', blob, offset)[0]
        if not plausible_heap(value):
            continue
        name = class_of(value, vtables)
        if name:
            print(f'{indent}  +{offset:#05x} -> {value:#x} {name}')
            if depth > 0:
                probe(value, size, vtables, depth - 1, indent + '      ')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('address', type=lambda s: int(s, 0))
    parser.add_argument('--deref', action='store_true', help='address is a global holding the object pointer')
    parser.add_argument('--size', type=lambda s: int(s, 0), default=0x200)
    parser.add_argument('--depth', type=int, default=0)
    parser.add_argument('--json', help='typeinfo JSON (default: newest re/dumps/typeinfo-*.json)')
    args = parser.parse_args()
    vtables = load_vtables(args.json)
    address = pointer(args.address) if args.deref else args.address
    probe(address, args.size, vtables, args.depth)


if __name__ == '__main__':
    main()
