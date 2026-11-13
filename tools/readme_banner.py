"""README banner from the launcher wallpaper (needs Pillow and the Bahnschrift font that ships with Windows).

    python -E -P tools\\readme_banner.py

  assets/launcher/source/wallpaper.png -> docs/img/banner.jpg   1600x520, title on the right in the launcher's
                                                                 colours (background RGB 18,20,22, accent 245,135,31)
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets" / "launcher" / "source" / "wallpaper.png"
TARGET = ROOT / "docs" / "img" / "banner.jpg"
FONT = Path("C:/Windows/Fonts/bahnschrift.ttf")

WIDTH, HEIGHT = 1600, 520
BACKGROUND = (18, 20, 22)
ACCENT = (245, 135, 31)
TEXT = (236, 236, 236)
MUTED = (178, 184, 190)


def font(size: int, variation: bytes) -> ImageFont.FreeTypeFont:
    face = ImageFont.truetype(str(FONT), size)
    face.set_variation_by_name(variation)
    return face


def main() -> None:
    wallpaper = Image.open(SOURCE).convert("RGB")
    scaled = wallpaper.resize((WIDTH, round(wallpaper.height * WIDTH / wallpaper.width)), Image.LANCZOS)
    top = round((scaled.height - HEIGHT) * 0.42)  # keeps the soldier's visor and the skyline
    banner = scaled.crop((0, top, WIDTH, top + HEIGHT))

    # Darken towards the right so the title reads on the fire and smoke.
    shade = Image.new("L", (WIDTH, HEIGHT))
    pixels = shade.load()
    for x in range(WIDTH):
        t = max(0.0, (x - WIDTH * 0.40) / (WIDTH * 0.22))
        alpha = int(228 * min(1.0, t) ** 1.4)
        for y in range(HEIGHT):
            pixels[x, y] = alpha
    banner = Image.composite(Image.new("RGB", banner.size, BACKGROUND), banner, shade)

    draw = ImageDraw.Draw(banner)
    left = 1010
    draw.rectangle((left, 160, left + 7, 336), fill=ACCENT)
    draw.text((left + 32, 148), "BF4-VR", font=font(124, b"Bold Condensed"), fill=TEXT)
    draw.text((left + 36, 280), "The Battlefield 4 campaign in VR", font=font(34, b"SemiBold"), fill=MUTED)
    draw.text((left + 36, 326), "HEAD TRACKING  \u00b7  BOTH EYES  \u00b7  HANDS ON THE GUN", font=font(21, b"SemiBold"), fill=ACCENT)
    draw.rectangle((0, HEIGHT - 6, WIDTH, HEIGHT), fill=ACCENT)

    TARGET.parent.mkdir(parents=True, exist_ok=True)
    banner.save(TARGET, quality=88, optimize=True, progressive=True)
    print(f"{TARGET.relative_to(ROOT)}: {WIDTH}x{HEIGHT}, {TARGET.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
