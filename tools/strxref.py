"""Find code that references a string in the decrypted BF4 dump (local analysis only).

    python -E -P tools/strxref.py <dump.exe> <text> [--max N]

Finds every occurrence of <text> (ASCII, NUL-terminated or not) in the image, then every
RIP-relative instruction in .text whose displacement points at it (lea/mov r, [rip+disp32]),
and prints the containing function from .pdata. Cheap stand-in for a full Ghidra xref pass.
"""
import argparse
import bisect
import re
import struct


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    exception_rva, exception_size = struct.unpack_from("<II", data, pe + 24 + 112 + 3 * 8)
    table = pe + 24 + optional
    result = []
    for index in range(count):
        name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", data, table + 40 * index)
        result.append((name.rstrip(b"\0").decode(), va, vsize, raw, rsize))
    return base, result, exception_rva, exception_size


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dump")
    parser.add_argument("text")
    parser.add_argument("--max", type=int, default=40)
    args = parser.parse_args()
    data = open(args.dump, "rb").read()
    base, secs, exc_rva, exc_size = sections(data)

    def rva_to_off(rva):
        for _, va, vsize, raw, rsize in secs:
            if va <= rva < va + max(vsize, rsize):
                return raw + rva - va if raw else rva
        return None

    def off_to_rva(off):
        for _, va, vsize, raw, rsize in secs:
            if raw and raw <= off < raw + rsize:
                return va + off - raw
        return None

    # Memory dumps are often written with raw == va; handle both layouts.
    targets = []
    for match in re.finditer(re.escape(args.text.encode()), data):
        rva = off_to_rva(match.start())
        if rva is not None:
            # start of the containing C string
            start = match.start()
            while start > 0 and 0x20 <= data[start - 1] < 0x7F:
                start -= 1
            end = data.find(b"\0", match.start())
            targets.append((off_to_rva(start), data[start:end].decode("latin1")))
    targets = sorted(set(targets))
    print(f"{len(targets)} strings contain {args.text!r}")

    functions = []
    off = rva_to_off(exc_rva)
    for index in range(exc_size // 12):
        begin, end, _ = struct.unpack_from("<III", data, off + 12 * index)
        if begin:
            functions.append((begin, end))
    functions.sort()
    starts = [f[0] for f in functions]

    text = next(s for s in secs if s[0] == ".text")
    _, tva, tvsize, traw, trsize = text
    code = data[traw:traw + trsize] if traw else data[tva:tva + tvsize]
    wanted = {rva: s for rva, s in targets}
    hits = 0
    for i in range(len(code) - 7):
        # REX.W? 8D/8B modrm with mod=00 rm=101 (RIP-relative)
        op = code[i]
        if op in (0x8D, 0x8B) and (code[i + 1] & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", code, i + 2)[0]
            target = tva + i + 6 + disp
            if target in wanted:
                k = bisect.bisect_right(starts, tva + i) - 1
                func = functions[k][0] + base if k >= 0 and functions[k][1] > tva + i else 0
                start = i - 1 if i and 0x40 <= code[i - 1] <= 0x4F else i
                print(f"0x{base + tva + start:X} in fn 0x{func:X}: {wanted[target]!r}")
                hits += 1
                if hits >= args.max:
                    return


if __name__ == "__main__":
    main()
