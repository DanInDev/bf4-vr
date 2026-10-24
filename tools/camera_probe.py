"""Camera structure research through the bf-vr control pipe (game must run with the DLL).

    python -I tools\\camera_probe.py                 scan RenderView for camera matrices
    python -I tools\\camera_probe.py --renderer 0x142672378 --view-offset 0x60

Hypothesis under test (community knowledge, unverified for this build): a global GameRenderer
pointer at 0x142672378, GameRenderer+0x60 -> RenderView, which holds the camera transform.
"""
import argparse
import math
import struct
import time

PIPE = r'\\.\pipe\bfvr'


def command(line: str) -> str:
    # The single pipe instance is briefly absent while the server re-creates it; retry for ~2 s.
    for attempt in range(40):
        try:
            with open(PIPE, 'r+b', buffering=0) as pipe:
                pipe.write(line.encode())
                chunks = []
                while chunk := pipe.read(1 << 20):  # large replies arrive in several reads
                    chunks.append(chunk)
                return b''.join(chunks).decode().strip()
        except OSError:  # not found, busy (EINVAL) or access denied while the server re-creates it
            time.sleep(0.05)
    raise RuntimeError('bf-vr control pipe not available')


def peek(address: int, length: int) -> bytes:
    reply = command(f'peek {address:x} {length}')
    if reply.startswith('error'):
        raise RuntimeError(f'{address:#x}: {reply}')
    return bytes.fromhex(reply)


def peek_many(addresses, length: int):
    """{address: bytes or None}; batched (peekmany) when the DLL supports it."""
    addresses = list(dict.fromkeys(addresses))
    result = {}
    batch = max(1, min(3000, (900_000 // (length * 2 + 1))))
    for start in range(0, len(addresses), batch):
        group = addresses[start:start + batch]
        reply = command(f'peekmany {length} ' + ' '.join(f'{a:x}' for a in group))
        if reply.startswith('error'):  # older DLL: one round trip per read
            for address in group:
                try:
                    result[address] = peek(address, length)
                except RuntimeError:
                    result[address] = None
            continue
        for address, line in zip(group, reply.split('\n')):
            result[address] = None if line.strip() == '-' else bytes.fromhex(line.strip())
    return result


def pointer(address: int) -> int:
    return struct.unpack('<Q', peek(address, 8))[0]


def unit(v):
    return abs(math.sqrt(sum(c * c for c in v)) - 1.0) < 1e-3


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def matrices(blob: bytes):
    """Offsets where 3 consecutive float4 rows form an orthonormal basis (a rotation)."""
    floats = struct.unpack(f'<{len(blob) // 4}f', blob[: len(blob) // 4 * 4])
    for i in range(0, len(floats) - 16, 4):  # rows are 16-byte aligned in Frostbite LinearTransforms
        rows = [floats[i + r * 4: i + r * 4 + 3] for r in range(3)]
        if all(unit(r) for r in rows) and all(abs(dot(rows[a], rows[b])) < 1e-3 for a, b in ((0, 1), (0, 2), (1, 2))):
            translation = floats[i + 12: i + 15]
            yield i * 4, rows, translation


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--renderer', type=lambda s: int(s, 0), default=0x142672378)
    parser.add_argument('--view-offset', type=lambda s: int(s, 0), default=0x60)
    parser.add_argument('--size', type=lambda s: int(s, 0), default=0x800)
    args = parser.parse_args()

    renderer = pointer(args.renderer)
    print(f'GameRenderer [{args.renderer:#x}] = {renderer:#x}')
    view = pointer(renderer + args.view_offset)
    print(f'RenderView   [GameRenderer+{args.view_offset:#x}] = {view:#x}')
    blob = peek(view, args.size)
    print(f'vtable {struct.unpack_from("<Q", blob, 0)[0]:#x}')
    for offset, rows, translation in matrices(blob):
        print(f'+{offset:#05x} rotation rows ' + ' | '.join(' '.join(f'{c:+.3f}' for c in r) for r in rows)
              + '   translation ' + ' '.join(f'{c:+.2f}' for c in translation))
    floats = struct.unpack(f'<{args.size // 4}f', blob)
    candidates = [(i * 4, f) for i, f in enumerate(floats) if 0.5 < f < 2.2 and i * 4 < 0x200]
    print('fov-like floats (radians, first 0x200 bytes): ' + ', '.join(f'+{o:#x}={f:.3f}' for o, f in candidates[:12]))


if __name__ == '__main__':
    main()
