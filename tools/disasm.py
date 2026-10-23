"""Disassemble the function containing an address in the memory dump (local analysis only).

    python -I tools\\disasm.py <dump.exe> 0x140b7973e [--context 40] [--full]

Function bounds come from the image's .pdata (x64 unwind table), so no Ghidra run is needed.
"""
import argparse
import struct

import capstone

BASE = 0x140000000


def load(path):
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    exception_dir_rva, exception_dir_size = struct.unpack_from('<II', data, pe + 24 + 112 + 3 * 8)
    return data, exception_dir_rva, exception_dir_size


def containing_function(data, pdata_rva, pdata_size, rva):
    """Exact .pdata range, or for leaf functions (no unwind entry) the gap between two entries
    with leading int3/nop padding skipped."""
    low, high = 0, pdata_size // 12 - 1
    while low <= high:
        mid = (low + high) // 2
        begin, end, _ = struct.unpack_from('<III', data, pdata_rva + mid * 12)
        if rva < begin:
            high = mid - 1
        elif rva >= end:
            low = mid + 1
        else:
            return begin, end, 'pdata'
    if high < 0 or low >= pdata_size // 12:
        return None
    previous_end = struct.unpack_from('<III', data, pdata_rva + high * 12)[1]
    next_begin = struct.unpack_from('<III', data, pdata_rva + low * 12)[0]
    start = previous_end
    while start < rva and data[start] in (0xCC, 0x90):
        start += 1
    return start, next_begin, 'gap (leaf function without unwind data; start inferred)'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('dump')
    parser.add_argument('address', type=lambda s: int(s, 0))
    parser.add_argument('--context', type=int, default=40, help='instructions shown around the address')
    parser.add_argument('--full', action='store_true', help='print the whole function')
    args = parser.parse_args()

    data, pdata_rva, pdata_size = load(args.dump)
    bounds = containing_function(data, pdata_rva, pdata_size, args.address - BASE)
    if not bounds:
        raise SystemExit('address is not inside a .pdata function')
    begin, end, source = bounds
    print(f'function {BASE + begin:#x}..{BASE + end:#x} ({end - begin} bytes, bounds from {source})')

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    instructions = list(md.disasm(data[begin:end], BASE + begin))
    index = next((i for i, ins in enumerate(instructions) if ins.address >= args.address), len(instructions))
    shown = instructions if args.full else instructions[max(0, index - args.context): index + args.context // 2]
    for ins in shown:
        marker = '>>' if ins.address == args.address else '  '
        print(f'{marker} {ins.address:#x}: {ins.mnemonic:8} {ins.op_str}')


if __name__ == '__main__':
    main()
