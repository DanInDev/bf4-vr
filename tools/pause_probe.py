"""Find a "game paused / menu open" flag by pausing and resuming with the pad's Start button and
diffing the game's .data section (and the objects its pointers lead to, one level). Campaign only;
the game must run with the DLL and take pad input (no headset session).

    python -E -P tools\\pause_probe.py

Prints addresses whose 8/32-bit value is equal in both playing snapshots, equal in both paused
snapshots, and different between the two states.
"""
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import command, peek, peek_many  # noqa: E402

DATA = (0x1422EF000, 0x1422EF000 + 0x72C4C0)  # BF4 179547 .data
CHUNK = 0x10000
OBJECT = 0x400


def heap(p):
    return 0x10000 < p < 0x7FF000000000 and p % 8 == 0 and not (0x140000000 <= p < 0x143000000)


def snapshot(objects=None):
    data = {}
    for a in range(DATA[0], DATA[1], CHUNK):
        data[a] = peek(a, min(CHUNK, DATA[1] - a))
    if objects is None:
        pointers = set()
        for a, b in data.items():
            for o in range(0, len(b) - 7, 8):
                p = struct.unpack_from('<Q', b, o)[0]
                if heap(p):
                    pointers.add(p)
        objects = sorted(pointers)
    data.update({a: b for a, b in peek_many(objects, OBJECT).items() if b})
    return data, objects


def toggle():
    command('pad 0 0 0 0 150 10')  # Start
    time.sleep(2.0)


def main():
    playing1, objects = snapshot()
    toggle()
    paused1, _ = snapshot(objects)
    toggle()
    playing2, _ = snapshot(objects)
    toggle()
    paused2, _ = snapshot(objects)
    toggle()  # back to playing
    hits = []
    for base, a in playing1.items():
        b, c, d = paused1.get(base), playing2.get(base), paused2.get(base)
        if not (b and c and d) or len({len(a), len(b), len(c), len(d)}) != 1:
            continue
        for o in range(len(a)):
            if a[o] == c[o] and b[o] == d[o] and a[o] != b[o]:
                hits.append((base, o, a[o], b[o]))
    region = lambda base: '.data' if DATA[0] <= base < DATA[1] else 'object'
    print(f'{len(hits)} bytes follow the pause state')
    # Flags first: one side 0, the other 1.
    hits.sort(key=lambda h: (not ({h[2], h[3]} == {0, 1}), h[0] + h[1]))
    for base, o, play, pause in hits[:60]:
        print(f'{region(base)} {base + o:#x} (base {base:#x} +{o:#x}): playing {play} paused {pause}')


if __name__ == '__main__':
    main()
