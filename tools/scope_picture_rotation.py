"""The scope picture's orientation and scale against the eye image.

lens.png (pipe scopeshot: the picture's disc, uv 0..1 = +-lens_tan on the picture's own axes, v down), ref.png (the eye's
image, same head, gun lowered), uv_per_tangent, the eye's projection x / y scales. Searches the picture camera's
rotation relative to the eye (yaw right positive, pitch up positive, roll right positive) and the scale factor k.
"""
import sys

import numpy as np
from PIL import Image


def grey(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32).mean(axis=2)


def sample(img, xs, ys):
    h, w = img.shape
    ok = (xs >= 0) & (xs < w - 1) & (ys >= 0) & (ys < h - 1)
    xs = np.clip(xs, 0, w - 1.001)
    ys = np.clip(ys, 0, h - 1.001)
    x0 = np.floor(xs).astype(int)
    y0 = np.floor(ys).astype(int)
    fx = xs - x0
    fy = ys - y0
    v = (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x0 + 1] * fx * (1 - fy) + img[y0 + 1, x0] * (1 - fx) * fy + img[y0 + 1, x0 + 1] * fx * fy)
    return v, ok


def ncc(a, b):
    a = a - a.mean()
    b = b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 0 else -1.0


def rotation(yaw, pitch, roll):
    y, p, r = np.radians([yaw, pitch, roll])
    # View space: x right, y up, -z ahead. Yaw right = about -y; pitch up = about +x; roll right = about -z... as matrices
    # taking picture-frame directions into eye-frame directions.
    ry = np.array([[np.cos(y), 0, -np.sin(y)], [0, 1, 0], [np.sin(y), 0, np.cos(y)]])
    rx = np.array([[1, 0, 0], [0, np.cos(p), np.sin(p)], [0, -np.sin(p), np.cos(p)]])
    rz = np.array([[np.cos(r), np.sin(r), 0], [-np.sin(r), np.cos(r), 0], [0, 0, 1]])
    return ry @ rx @ rz


def main():
    lens_path, ref_path = sys.argv[1:3]
    uvpt, sx, sy = (float(v) for v in sys.argv[3:6])
    lens = grey(lens_path)
    ref = grey(ref_path)
    lh, lw = lens.shape
    rh, rw = ref.shape
    us, vs = np.meshgrid(np.linspace(0.04, 0.96, 70), np.linspace(0.04, 0.96, 70))
    inside = (us - 0.5) ** 2 + (vs - 0.5) ** 2 <= 0.45 ** 2
    us = us[inside]
    vs = vs[inside]
    values, _ = sample(lens, us * (lw - 1), vs * (lh - 1))

    def score(yaw, pitch, roll, k):
        tx = (us - 0.5) / uvpt * k
        ty = -(vs - 0.5) / uvpt * k
        d = np.stack([tx, ty, -np.ones_like(tx)])
        e = rotation(yaw, pitch, roll) @ d
        ahead = e[2] < -1e-3
        xs = rw / 2 + e[0] / -e[2] * sx * rw / 2
        ys = rh / 2 - e[1] / -e[2] * sy * rh / 2
        cand, ok = sample(ref, xs, ys)
        ok &= ahead
        if ok.mean() < 0.6:
            return -2.0
        return ncc(values[ok], cand[ok])

    best = (-2.0,)
    for yaw in np.arange(-40, 40.1, 2.5):
        for pitch in np.arange(-25, 25.1, 2.5):
            for k in (0.8, 1.0, 1.25):
                s = score(yaw, pitch, 0.0, k)
                if s > best[0]:
                    best = (s, yaw, pitch, 0.0, k)
    s, yaw, pitch, roll, k = best
    for yaw2 in np.arange(yaw - 2.5, yaw + 2.6, 0.5):
        for pitch2 in np.arange(pitch - 2.5, pitch + 2.6, 0.5):
            for roll2 in np.arange(-30, 30.1, 2.5):
                for k2 in np.arange(k * 0.8, k * 1.25, k * 0.025):
                    s2 = score(yaw2, pitch2, roll2, k2)
                    if s2 > best[0]:
                        best = (s2, yaw2, pitch2, roll2, k2)
    s, yaw, pitch, roll, k = best
    print(f"picture vs eye: yaw {yaw:+.1f} pitch {pitch:+.1f} roll {roll:+.1f} deg, scale k {k:.3f}, ncc {s:.3f}")


if __name__ == "__main__":
    main()
