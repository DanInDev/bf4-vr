"""Find typed engine objects reachable from globals (game must run with the DLL).

    python -E -P tools\\global_scan.py                    everything found
    python -E -P tools\\global_scan.py --filter Soldier   only classes containing the text

Reads .data through the control pipe and names each pointed-at object by its vtable (typeinfo JSON
from typeinfo_dump.py). For untyped objects (unreflected managers and singletons) it also names
their typed members, one level deep. Result also saved to re/dumps/globals-live.json.
"""
import argparse
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import peek, peek_many  # noqa: E402
from object_probe import load_vtables, plausible_heap  # noqa: E402

DATA = (0x1422EF000, 0x1422EF000 + 0x72C4C0)  # BF4 179547 .data
CHUNK = 0x10000


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--filter', default='')
    parser.add_argument('--members', type=lambda s: int(s, 0), default=0x800,
                        help='bytes of each untyped global object to search for typed members (0 = off)')
    parser.add_argument('--json', help='typeinfo JSON (default: newest re/dumps/typeinfo-*.json)')
    args = parser.parse_args()
    vtables = load_vtables(args.json)

    started = time.time()
    pointers = {}
    for chunk in range(DATA[0], DATA[1], CHUNK):
        blob = peek(chunk, min(CHUNK, DATA[1] - chunk))
        for offset in range(0, len(blob) - 7, 8):
            value = struct.unpack_from('<Q', blob, offset)[0]
            if plausible_heap(value):
                pointers.setdefault(value, []).append(chunk + offset)
    print(f'{len(pointers)} distinct heap pointers in .data ({time.time() - started:.1f} s)', file=sys.stderr)

    def classes(addresses):
        heads = peek_many(addresses, 8)
        return {a: vtables.get(struct.unpack('<Q', b)[0]) if b else None for a, b in heads.items()}

    found = []  # (path, object, class); path = "[global]" or "[global]+0xNN"
    named = classes(pointers)
    untyped = [value for value in pointers if not named[value]]
    for value, globals_ in pointers.items():
        if named[value]:
            found.extend((f'[{g:#x}]', value, named[value]) for g in globals_)
    if args.members:
        # Untyped singletons (managers): name their typed members, one level deep.
        bodies = peek_many(untyped, args.members)
        members = {}
        for value, blob in bodies.items():
            for offset in range(0, len(blob or b'') - 7, 8):
                member = struct.unpack_from('<Q', blob, offset)[0]
                if plausible_heap(member):
                    members.setdefault(member, []).append((value, offset))
        member_names = classes(members)
        for member, owners in members.items():
            if member_names[member]:
                for value, offset in owners:
                    found.extend((f'[{g:#x}]+{offset:#x}', member, member_names[member]) for g in pointers[value])
    print(f'{len(found)} typed objects reachable ({time.time() - started:.1f} s)', file=sys.stderr)

    found.sort()
    out = os.path.join(HERE, '..', 're', 'dumps', 'globals-live.json')
    with open(out, 'w') as f:
        json.dump([{'path': p, 'object': f'{o:#x}', 'class': n} for p, o, n in found], f, indent=1)
    for p, o, n in found:
        if args.filter.lower() in n.lower():
            print(f'{p} -> {o:#x} {n}')


if __name__ == '__main__':
    main()
