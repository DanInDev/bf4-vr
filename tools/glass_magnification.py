"""Measure the magnification seen through an optic's glass.

glass.png: eye shot with the sight at the eye; mark.png: same with pipe glassdebug 1 (the sight line's point magenta);
ref.png: same head pose with the gun lowered. Through a glass of power P, the eye pixel at d from the line's pixel shows
what the reference shows at d / P. Searches P and a small shift by normalised cross-correlation.
"""
import sys

import numpy as np
from PIL import Image


def grey(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32).mean(axis=2)


def line_point(mark_path):
    rgb = np.asarray(Image.open(mark_path).convert("RGB"), dtype=np.int32)
    magenta = (rgb[:, :, 0] > 235) & (rgb[:, :, 2] > 235) & (rgb[:, :, 1] < 40)
    ys, xs = np.nonzero(magenta)
    if len(xs) < 5:
        return None
    return float(np.median(xs)), float(np.median(ys))


def sample(img, xs, ys):
    h, w = img.shape
    xs = np.clip(xs, 0, w - 1.001)
    ys = np.clip(ys, 0, h - 1.001)
    x0 = np.floor(xs).astype(int)
    y0 = np.floor(ys).astype(int)
    fx = xs - x0
    fy = ys - y0
    return (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x0 + 1] * fx * (1 - fy) + img[y0 + 1, x0] * (1 - fx) * fy
            + img[y0 + 1, x0 + 1] * fx * fy)


def ncc(a, b):
    a = a - a.mean()
    b = b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 0 else -1.0


def main():
    glass_path, mark_path, ref_path = sys.argv[1:4]
    radius = int(sys.argv[4]) if len(sys.argv) > 4 else 60
    point = line_point(mark_path)
    if point is None:
        print("no magenta line point")
        return
    glass = grey(glass_path)
    ref = grey(ref_path)
    px, py = point
    # The glass's pixels: the debug mask paints the glass's lens draws in pure colours (quad 0 red, 1 green, 2 blue,
    # 0.6 bright), the line point magenta; keep those within the radius.
    rgb = np.asarray(Image.open(mark_path).convert("RGB"), dtype=np.int32)
    r, g, b = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    pure = (np.maximum(np.maximum(r, g), b) > 100) & (np.minimum(np.minimum(r, g), b) < 40) & ~((r > 235) & (b > 235) & (g < 40))
    ys, xs = np.nonzero(pure)
    keep = ((xs - px) ** 2 + (ys - py) ** 2 <= radius * radius) & (xs % 2 == 0) & (ys % 2 == 0)
    dx = (xs[keep] - px).astype(np.float32)
    dy = (ys[keep] - py).astype(np.float32)
    if len(dx) < 50:
        print(f"line at ({px:.0f}, {py:.0f}): only {len(dx)} glass pixels")
        return
    patch = sample(glass, px + dx, py + dy)
    if patch.std() < 2.0:
        print(f"line at ({px:.0f}, {py:.0f}): the glass is flat there (std {patch.std():.1f})")
        return
    best = (-2.0, 0.0, 0.0, 0.0)
    for power in np.concatenate([np.arange(1.0, 4.0, 0.05), np.arange(4.0, 16.0, 0.25)]):
        for sx in range(-45, 46, 3):
            for sy in range(-45, 46, 3):
                candidate = sample(ref, px + sx + dx / power, py + sy + dy / power)
                score = ncc(patch, candidate)
                if score > best[0]:
                    best = (score, float(power), sx, sy)
    score, power, sx, sy = best
    # Refine around the best.
    for p in np.arange(max(1.0, power * 0.9), power * 1.1, power * 0.01):
        for sx2 in range(sx - 3, sx + 4):
            for sy2 in range(sy - 3, sy + 4):
                s = ncc(patch, sample(ref, px + sx2 + dx / p, py + sy2 + dy / p))
                if s > best[0]:
                    best = (s, float(p), sx2, sy2)
    score, power, sx, sy = best
    print(f"line at ({px:.0f}, {py:.0f}) radius {radius}: magnification {power:.2f} (correlation {score:.2f}, shift {sx} {sy} px)")


if __name__ == "__main__":
    main()

