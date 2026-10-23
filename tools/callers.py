"""Find direct call/jmp rel32 sites targeting an address in the memory dump (local analysis only).

    python -E -P tools\\callers.py <dump.exe> 0x140b79380

Only direct E8/E9 rel32 branches are found; calls through pointers or vtables are not.
"""
import struct
import sys

BASE = 0x140000000


def main():
    data = open(sys.argv[1], 'rb').read()
    target = int(sys.argv[2], 0)
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    for i in range(count):
        o = pe + 24 + optional + 40 * i
        if data[o:o + 8].rstrip(b'\0') != b'.text':
            continue
        size, rva = struct.unpack_from('<II', data, o + 8)
        for pos in range(rva, rva + size - 5):
            if data[pos] in (0xE8, 0xE9):
                rel = struct.unpack_from('<i', data, pos + 1)[0]
                if BASE + pos + 5 + rel == target:
                    kind = 'call' if data[pos] == 0xE8 else 'jmp '
                    print(f'{kind} at {BASE + pos:#x}')


if __name__ == '__main__':
    main()
