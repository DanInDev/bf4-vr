"""Weapon slots and switching (docs/tasks/weapon-swap.md stage 0; game must run with the DLL, campaign).

    python -E -P tools\\slot_probe.py                 slots, names, the current slot
    python -E -P tools\\slot_probe.py --press 8000    press pad buttons (hex XINPUT) and time the switch

The current slot is the client weapons component's index (re/notes/aim.md); a switch is timed from the
press until that index changes.
"""
import argparse
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import command, peek  # noqa: E402

SOLDIER_GLOBAL = 0x14230F260


def q(address):
    return struct.unpack('<Q', peek(address, 8))[0]


def slots():
    weapons = q(q(SOLDIER_GLOBAL) + 0x570)
    begin, end = q(weapons + 0x890), q(weapons + 0x898)
    names = {}
    for slot in range((end - begin) // 8):
        weapon = q(begin + 8 * slot)
        if weapon:
            try:
                names[slot] = peek(q(q(weapon + 0x4850) + 0x20), 48).split(b'\0')[0].decode()
            except RuntimeError:
                names[slot] = '?'
    return names


def current():
    weapons = q(q(SOLDIER_GLOBAL) + 0x570)
    flags = struct.unpack('<I', peek(weapons + 0x28, 4))[0]
    holder = q(weapons + (0x8E0 if flags & 4 else 0x8D8))
    return struct.unpack('<I', peek(holder + 8, 4))[0]


def press(buttons, hold_ms=120, watch_s=2.5):
    before = current()
    command(f'pad 0 0 0 0 {hold_ms} {buttons:x}')
    t0 = time.time()
    while time.time() - t0 < watch_s:
        now = current()
        if now != before:
            return before, now, time.time() - t0
        time.sleep(0.02)
    return before, before, None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--press', help='hex XINPUT buttons')
    parser.add_argument('--hold', type=int, default=120)
    args = parser.parse_args()
    names = slots()
    print('slots:', ', '.join(f'{s} {n}' for s, n in names.items()), '| current', current())
    if args.press:
        before, after, seconds = press(int(args.press, 16), args.hold)
        took = f'{seconds:.2f}s' if seconds is not None else 'no switch'
        print(f'press {args.press}: slot {before} {names.get(before)} -> {after} {names.get(after)} ({took})')


if __name__ == '__main__':
    main()
