"""Launcher artwork from the source images in assets/launcher/source (needs Pillow).

    python -E -P tools\\launcher_assets.py

  logo.png       -> launcher.ico   exe, window and taskbar icon; the white around the rounded square
                                   becomes transparent
  wallpaper.png  -> wallpaper.jpg  Play page backdrop, embedded in the exe (a JPEG keeps it small)
"""
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent / "assets" / "launcher"
ICON_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
WALLPAPER_WIDTH = 1600


def make_icon(source: Path, target: Path) -> None:
    image = Image.open(source).convert("RGB")

    # The background is the near-white area connected to the corners.
    marker = (255, 0, 255)
    filled = image.copy()
    for corner in [(0, 0), (image.width - 1, 0), (0, image.height - 1), (image.width - 1, image.height - 1)]:
        ImageDraw.floodfill(filled, corner, marker, thresh=60)
    difference = ImageChops.difference(filled, Image.new("RGB", image.size, marker))
    mask = difference.convert("L").point(lambda value: 0 if value == 0 else 255)
    mask = mask.filter(ImageFilter.GaussianBlur(1.5))

    icon = image.convert("RGBA")
    icon.putalpha(mask)
    box = mask.point(lambda value: 255 if value > 128 else 0).getbbox()
    icon = icon.crop(box)
    side = max(icon.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(icon, ((side - icon.width) // 2, (side - icon.height) // 2))
    square = square.resize((256, 256), Image.LANCZOS)
    square.save(target, sizes=[(size, size) for size in ICON_SIZES])


def make_wallpaper(source: Path, target: Path) -> None:
    image = Image.open(source).convert("RGB")
    height = round(image.height * WALLPAPER_WIDTH / image.width)
    image.resize((WALLPAPER_WIDTH, height), Image.LANCZOS).save(target, quality=85, optimize=True, progressive=False)


def main() -> None:
    make_icon(ROOT / "source" / "logo.png", ROOT / "launcher.ico")
    make_wallpaper(ROOT / "source" / "wallpaper.png", ROOT / "wallpaper.jpg")
    for name in ("launcher.ico", "wallpaper.jpg"):
        print(f"{name}: {(ROOT / name).stat().st_size // 1024} KiB")


if __name__ == "__main__":
    main()
