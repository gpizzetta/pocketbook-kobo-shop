#!/usr/bin/env python3
"""Génère les icônes PocketBook (BMP 8-bit palette) pour kobo_shop.

Sortie :
  assets/icons/kobo_shop.bmp      — lanceur (normal)
  assets/icons/kobo_shop_f.bmp    — lanceur (focus / invert)
  assets/icons/ui/*.bmp           — set UI in-app
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1] / "assets" / "icons"
UI = ROOT / "ui"
APP_SIZE = 128  # max courant lanceur PocketBook
UI_SIZE = 96


def to_pb_bmp(img: Image.Image) -> Image.Image:
    """8-bit grayscale palette BMP (format attendu par le lanceur PB)."""
    g = img.convert("L")
    pal = []
    for i in range(256):
        pal.extend([i, i, i])
    out = Image.new("P", g.size)
    out.putpalette(pal)
    out.paste(g)
    return out


def save_app(img: Image.Image, name: str, size: int = APP_SIZE) -> None:
    img = img.resize((size, size), Image.Resampling.LANCZOS)
    normal = to_pb_bmp(img)
    focused = to_pb_bmp(Image.eval(img.convert("L"), lambda p: 255 - p))
    ROOT.mkdir(parents=True, exist_ok=True)
    normal.save(ROOT / f"{name}.bmp")
    focused.save(ROOT / f"{name}_f.bmp")
    img.convert("L").save(ROOT / f"{name}.png")


def save_ui(img: Image.Image, name: str) -> None:
    UI.mkdir(parents=True, exist_ok=True)
    img.save(UI / f"{name}.png")
    to_pb_bmp(img).save(UI / f"{name}.bmp")


def make_app_icon(size: int = 256) -> Image.Image:
    """Grand « K » (Kobo) — contraste max e-ink / lanceur PB."""
    im = Image.new("L", (size, size), 255)
    d = ImageDraw.Draw(im)
    m = size // 16
    stroke = max(6, size // 18)

    # Cadre plein noir, K blanc (ou l'inverse via focus)
    d.rounded_rectangle(
        [m, m, size - m, size - m],
        radius=size // 7,
        fill=0,
        outline=0,
    )

    try:
        font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", int(size * 0.62)
        )
    except OSError:
        font = ImageFont.load_default()

    bbox = d.textbbox((0, 0), "K", font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx = (size - tw) // 2 - bbox[0]
    ty = (size - th) // 2 - bbox[1] - size // 40
    d.text((tx, ty), "K", fill=255, font=font)
    # léger contour interne pour netteté à petite taille
    d.rounded_rectangle(
        [m, m, size - m, size - m],
        radius=size // 7,
        outline=0,
        width=stroke // 2,
    )
    return im


def _blank(size: int = UI_SIZE) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    im = Image.new("L", (size, size), 255)
    return im, ImageDraw.Draw(im)


def icon_menu(size: int = UI_SIZE) -> Image.Image:
    im, d = _blank(size)
    m = size // 6
    t = max(4, size // 12)
    gap = (size - 2 * m - 3 * t) // 2
    for i in range(3):
        y = m + i * (t + gap)
        d.rectangle([m, y, size - m, y + t], fill=0)
    return im


def icon_search(size: int = UI_SIZE) -> Image.Image:
    im, d = _blank(size)
    m = size // 5
    w = max(4, size // 14)
    d.ellipse([m, m, size - m * 2, size - m * 2], outline=0, width=w)
    d.line(
        [int(size * 0.62), int(size * 0.62), size - m // 2, size - m // 2],
        fill=0,
        width=w + 1,
    )
    return im


def icon_download(size: int = UI_SIZE) -> Image.Image:
    im, d = _blank(size)
    m = size // 5
    w = max(4, size // 14)
    cx = size // 2
    d.line([(cx, m), (cx, size - m * 2)], fill=0, width=w)
    d.polygon(
        [
            (cx, size - int(m * 1.6)),
            (cx - m, size - int(m * 3.1)),
            (cx + m, size - int(m * 3.1)),
        ],
        fill=0,
    )
    d.arc([m, size - int(m * 2.4), size - m, size - m // 3], 20, 160, fill=0, width=w)
    return im


def icon_open(size: int = UI_SIZE) -> Image.Image:
    im, d = _blank(size)
    m = size // 6
    w = max(3, size // 18)
    cx = size // 2
    d.line([(cx, m), (cx, size - m)], fill=0, width=w)
    d.polygon(
        [(m, m * 2), (cx, m), (cx, size - m), (m, size - int(m * 1.4))],
        outline=0,
        width=w,
    )
    d.polygon(
        [(size - m, m * 2), (cx, m), (cx, size - m), (size - m, size - int(m * 1.4))],
        outline=0,
        width=w,
    )
    return im


def icon_sync(size: int = UI_SIZE) -> Image.Image:
    im, d = _blank(size)
    m = size // 5
    w = max(4, size // 14)
    d.arc([m, m, size - m, size - m], 45, 300, fill=0, width=w)
    # tête de flèche
    d.polygon(
        [
            (size - m, size // 2 - m // 2),
            (size - m // 3, size // 2 + m // 4),
            (size - int(m * 1.6), size // 2 + m // 2),
        ],
        fill=0,
    )
    return im


def icon_settings(size: int = UI_SIZE) -> Image.Image:
    im, d = _blank(size)
    c = size // 2
    r = size // 4
    w = max(3, size // 16)
    d.ellipse([c - r // 2, c - r // 2, c + r // 2, c + r // 2], outline=0, width=w)
    for i in range(8):
        a = i * math.pi / 4
        x0 = c + int((r + 2) * math.cos(a))
        y0 = c + int((r + 2) * math.sin(a))
        x1 = c + int((r + size // 7) * math.cos(a))
        y1 = c + int((r + size // 7) * math.sin(a))
        d.line([(x0, y0), (x1, y1)], fill=0, width=w)
    return im


def icon_sort(size: int = UI_SIZE) -> Image.Image:
    im, d = _blank(size)
    m = size // 5
    h = max(6, size // 10)
    for i, frac in enumerate((0.35, 0.55, 0.8)):
        y = m + i * int(m * 1.15)
        d.rectangle([m, y, m + int(size * frac), y + h], fill=0)
    return im


def main() -> None:
    save_app(make_app_icon(), "kobo_shop")
    for name, fn in (
        ("menu", icon_menu),
        ("search", icon_search),
        ("download", icon_download),
        ("open", icon_open),
        ("sync", icon_sync),
        ("settings", icon_settings),
        ("sort", icon_sort),
    ):
        save_ui(fn(), name)
    print(f"OK → {ROOT}")
    for p in sorted(ROOT.rglob("*.bmp")):
        print(f"  {p.relative_to(ROOT)} ({p.stat().st_size} B)")


if __name__ == "__main__":
    main()
