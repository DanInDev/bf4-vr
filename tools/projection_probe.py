"""Finds projection-like 4x4 matrices in the RenderView and prints their implied FOVs.

    python -E -P tools\\projection_probe.py

A perspective projection has m00 = 1/tan(hfov/2), m11 = 1/tan(vfov/2), zeros off the diagonal
in the first two rows, and m23 or m32 = +-1.
"""
import math
import struct

from camera_probe import peek, pointer


def main():
    renderer = pointer(0x142672378)
    view = pointer(renderer + 0x60)
    blob = peek(view, 0x9c0)
    print(f'RenderView {view:#x}: fov field +0xB4 = {struct.unpack_from("<f", blob, 0xb4)[0]:.4f}, '
          f'+0xB8 = {struct.unpack_from("<f", blob, 0xb8)[0]:.4f}, aspect +0xC4 = {struct.unpack_from("<f", blob, 0xc4)[0]:.4f}')
    for offset in range(0, len(blob) - 64, 16):
        m = struct.unpack_from('<16f', blob, offset)
        m00, m11 = m[0], m[5]
        if not (0.05 < m00 < 20 and 0.05 < m11 < 20):
            continue
        off_diagonal = [m[1], m[2], m[3], m[4], m[6], m[7]]
        if any(abs(v) > 1e-4 for v in off_diagonal):
            continue
        if not (abs(abs(m[11]) - 1) < 1e-3 or abs(abs(m[14]) - 1) < 1e-3):
            continue
        hfov = math.degrees(2 * math.atan(1 / m00))
        vfov = math.degrees(2 * math.atan(1 / m11))
        print(f'+{offset:#05x}: m00={m00:.4f} m11={m11:.4f} -> hfov {hfov:.1f} deg, vfov {vfov:.1f} deg, '
              f'aspect {m11 / m00:.3f}, vfov rad {math.radians(vfov):.4f}')


if __name__ == '__main__':
    main()
