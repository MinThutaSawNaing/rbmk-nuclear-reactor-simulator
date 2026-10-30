"""Derive every application, executable and installer icon from the master logo.

The master artwork (``logos/rbmk_research_logo_500.png``) is a square canvas with
a circular emblem on solid black. Windows needs the emblem cropped to its circle
with transparent corners, exported as a multi-resolution ``.ico`` (Explorer,
taskbar, shortcuts, uninstall entry) plus a PNG set that is embedded in the Qt
resource system so the window/taskbar icon stays crisp at every DPI scale.

Run from the repository root (requires Pillow):

    python tools/make_icons.py

Outputs:
    assets/icons/rbmk-logo-<size>.png   runtime window/taskbar icon set
    assets/icons/rbmk.ico              executable + installer icon
    installer/wizard-large.bmp         Inno Setup welcome/finish bitmap (164x314)
    installer/wizard-small.bmp         Inno Setup header bitmap (55x55)
"""

from __future__ import annotations

import io
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "logos" / "rbmk_research_logo_500.png"
ICON_DIR = ROOT / "assets" / "icons"
INSTALLER_DIR = ROOT / "installer"

#: Sizes Qt is allowed to pick from when painting the window and taskbar icon.
PNG_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
#: Sizes stored inside the Windows .ico (256 must come last for Explorer).
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)

#: Backdrop used by the installer bitmaps, matching the control-room palette.
WIZARD_BACKDROP_TOP = (10, 15, 26)
WIZARD_BACKDROP_BOTTOM = (5, 7, 12)
WIZARD_WIDE = (164, 314)
WIZARD_SMALL = (55, 55)


def find_emblem_box(image: Image.Image, threshold: int = 40) -> tuple[int, int, int, int]:
    """Bounding box of the bright emblem, ignoring the near-black square canvas."""
    rgb = image.convert("RGB")
    width, height = rgb.size
    mask = Image.new("L", rgb.size, 0)
    pixels = mask.load()
    data = rgb.load()
    for y in range(height):
        for x in range(width):
            r, g, b = data[x, y]
            if max(r, g, b) > threshold:
                pixels[x, y] = 255
    box = mask.getbbox()
    if box is None:  # pragma: no cover - the master artwork is never blank
        raise SystemExit(f"No emblem found in {SOURCE}")
    return box


def circular_emblem(image: Image.Image, box: tuple[int, int, int, int]) -> Image.Image:
    """Crop the emblem and make everything outside its circle transparent."""
    left, top, right, bottom = box
    emblem = image.convert("RGBA").crop((left, top, right, bottom))

    # Square the crop so the inscribed circle stays centred, and pad by 1 % so
    # the outer ring is not clipped by the antialiased mask.
    side = max(emblem.size)
    pad = max(1, round(side * 0.01))
    canvas = Image.new("RGBA", (side + 2 * pad, side + 2 * pad), (0, 0, 0, 0))
    offset = ((canvas.width - emblem.width) // 2, (canvas.height - emblem.height) // 2)
    canvas.paste(emblem, offset)

    # Supersample the circle, then let the LANCZOS downscale produce a clean
    # antialiased rim instead of a jagged one.
    supersample = 4
    mask = Image.new("L", (canvas.width * supersample, canvas.height * supersample), 0)
    draw = ImageDraw.Draw(mask)
    draw.ellipse(
        (
            offset[0] * supersample,
            offset[1] * supersample,
            (offset[0] + side) * supersample - 1,
            (offset[1] + side) * supersample - 1,
        ),
        fill=255,
    )
    mask = mask.resize(canvas.size, Image.LANCZOS)
    canvas.putalpha(mask)
    return canvas


def resized(emblem: Image.Image, size: int) -> Image.Image:
    """Render one icon size, sharpening the tiny variants so they stay legible."""
    image = emblem.resize((size, size), Image.LANCZOS)
    if size <= 48:
        rgb = image.convert("RGB").filter(
            ImageFilter.UnsharpMask(radius=0.6, percent=70, threshold=0)
        )
        image = Image.merge("RGBA", (*rgb.split(), image.getchannel("A")))
    return image


def write_ico(path: Path, images: list[Image.Image]) -> None:
    """Write a PNG-compressed, multi-resolution .ico file."""
    blobs = []
    for image in images:
        buffer = io.BytesIO()
        image.save(buffer, format="PNG", optimize=True)
        blobs.append(buffer.getvalue())

    entries = b""
    offset = 6 + 16 * len(images)
    for image, data in zip(images, blobs):
        entries += struct.pack(
            "<BBBBHHII",
            0 if image.width >= 256 else image.width,
            0 if image.height >= 256 else image.height,
            0,  # palette colour count (true colour)
            0,  # reserved
            1,  # colour planes
            32,  # bits per pixel
            len(data),
            offset,
        )
        offset += len(data)

    with path.open("wb") as handle:
        handle.write(struct.pack("<HHH", 0, 1, len(images)))
        handle.write(entries)
        for data in blobs:
            handle.write(data)


def backdrop(size: tuple[int, int], logo: Image.Image, logo_size: int,
             logo_y: int) -> Image.Image:
    """Installer wizard bitmap: the emblem on the control-room backdrop."""
    width, height = size
    sheet = Image.new("RGB", size, WIZARD_BACKDROP_TOP)
    draw = ImageDraw.Draw(sheet)
    for y in range(height):
        ratio = y / max(1, height - 1)
        colour = tuple(
            round(top + (bottom - top) * ratio)
            for top, bottom in zip(WIZARD_BACKDROP_TOP, WIZARD_BACKDROP_BOTTOM)
        )
        draw.line([(0, y), (width, y)], fill=colour)

    scaled = logo.resize((logo_size, logo_size), Image.LANCZOS)
    sheet.paste(scaled, ((width - logo_size) // 2, logo_y), scaled)

    # Thin cyan rule under the emblem ties the bitmap to the app accent colour.
    rule_y = min(height - 12, logo_y + logo_size + 10)
    draw.line([(width // 4, rule_y), (width - width // 4, rule_y)],
              fill=(56, 189, 248), width=1)
    return sheet


def main() -> None:
    if not SOURCE.is_file():
        raise SystemExit(f"Master logo not found: {SOURCE}")

    ICON_DIR.mkdir(parents=True, exist_ok=True)
    INSTALLER_DIR.mkdir(parents=True, exist_ok=True)

    master = Image.open(SOURCE).convert("RGBA")
    emblem = circular_emblem(master, find_emblem_box(master))
    rendered = {size: resized(emblem, size) for size in sorted(set(PNG_SIZES) | set(ICO_SIZES))}

    for size in PNG_SIZES:
        target = ICON_DIR / f"rbmk-logo-{size}.png"
        rendered[size].save(target, format="PNG", optimize=True)
        print(f"wrote {target.relative_to(ROOT)}")

    ico = ICON_DIR / "rbmk.ico"
    write_ico(ico, [rendered[size] for size in ICO_SIZES])
    print(f"wrote {ico.relative_to(ROOT)}")

    large_path = INSTALLER_DIR / "wizard-large.bmp"
    backdrop(WIZARD_WIDE, rendered[256], 140, 62).save(large_path, format="BMP")
    print(f"wrote {large_path.relative_to(ROOT)}")

    small_path = INSTALLER_DIR / "wizard-small.bmp"
    backdrop(WIZARD_SMALL, rendered[64], 49, 3).save(small_path, format="BMP")
    print(f"wrote {small_path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
