"""Find the local soldier's stance field (stand 0 / crouch 1 / prone 2) by pressing the pad's crouch
button (B: tap = crouch, hold = prone) and diffing memory (local analysis, campaign only).

    python -E -P tools\\pose_probe.py

Snapshots the soldier (global 0x14230F260) and every object it points to (one level, 0x1000 bytes
each), presses B for crouch, then holds it for prone, then taps it again, and prints u32/u8 fields
whose values follow 0 -> 1 -> 2 (or any stance-like sequence) across the snapshots.
"""
import struct
import time

from camera_probe import command, peek, peek_many

SOLDIER_GLOBAL = 0x14230F260
SPAN = 0x1000
B_BUTTON = 0x2000
A_BUTTON = 0x1000


def snapshot():
    soldier = struct.unpack('<Q', peek(SOLDIER_GLOBAL, 8))[0]
    body = peek(soldier, SPAN)
    pointers = {struct.unpack_from('<Q', body, o)[0] for o in range(0, SPAN, 8)}
    pointers = [p for p in pointers if 0x10000 < p < 0x7FF000000000 and p % 8 == 0]
    objects = peek_many([soldier] + pointers, SPAN)
    return soldier, {a: b for a, b in objects.items() if b}


def press(buttons, ms):
    command(f'pad 0 0 0 0 {ms} {buttons:x}')
    time.sleep(ms / 1000.0 + 1.5)


def main():
    soldier, standing = snapshot()
    print(f'soldier {soldier:#x}, {len(standing)} objects')
    press(B_BUTTON, 120)
    _, crouched = snapshot()
    press(B_BUTTON, 900)
    _, prone = snapshot()
    press(A_BUTTON, 120)  # jump stands up from prone/crouch
    time.sleep(1.0)
    _, standing_again = snapshot()
    hits = []
    for base, data in standing.items():
        snaps = [crouched.get(base), prone.get(base), standing_again.get(base)]
        if any(s is None for s in snaps):
            continue
        for offset in range(0, SPAN - 3, 4):
            values = [struct.unpack_from('<I', d, offset)[0] for d in [data] + snaps]
            if values[0] != values[1] and values[1] != values[2] and values[0] == values[3] and max(values) < 16:
                hits.append((base, offset, values))
    for base, offset, values in hits:
        tag = 'soldier' if base == soldier else 'object'
        print(f'{tag} {base:#x} +{offset:#x}: stand {values[0]} crouch {values[1]} prone {values[2]} stand {values[3]}')
    if not hits:
        print('no stance-like field found')


if __name__ == '__main__':
    main()
