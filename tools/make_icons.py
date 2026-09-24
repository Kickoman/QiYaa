#!/usr/bin/env python3
"""Draws the QiYaa application icon (an original design: spectrum bars on a
dark rounded panel) and writes it in the formats the platforms want.

    python3 tools/make_icons.py      # needs Pillow

Outputs to resources/icons/: qiyaa.png (256), qiyaa-<n>.png, qiyaa.ico, qiyaa.icns
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent.parent / "resources" / "icons"
S = 1024  # drawn large, scaled down for smooth edges


def lerp(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def draw() -> Image.Image:
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # Panel: rounded square with a vertical gradient and a thin light rim.
    margin, radius = 64, 200
    panel = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    grad = Image.new("RGBA", (S, S))
    top, bottom = (58, 64, 92), (18, 20, 32)
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        gd.line([(0, y), (S, y)], fill=lerp(top, bottom, y / S) + (255,))
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([margin, margin, S - margin, S - margin], radius, fill=255)
    panel.paste(grad, (0, 0), mask)

    # Soft drop shadow under the panel.
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle([margin, margin + 24, S - margin, S - margin + 24], radius, fill=(0, 0, 0, 120))
    shadow = shadow.filter(ImageFilter.GaussianBlur(28))
    img.alpha_composite(shadow)
    img.alpha_composite(panel)

    d = ImageDraw.Draw(img)
    d.rounded_rectangle([margin + 6, margin + 6, S - margin - 6, S - margin - 6], radius - 6,
                        outline=(140, 150, 190, 110), width=10)

    # Display well.
    well = [margin + 110, margin + 150, S - margin - 110, S - margin - 130]
    d.rounded_rectangle(well, 60, fill=(6, 8, 12, 255), outline=(0, 0, 0, 255), width=6)

    # Spectrum bars with peak caps (green -> yellow -> red, bottom to top).
    heights = [0.45, 0.78, 0.62, 0.92, 0.70, 0.52, 0.34]
    x0, x1 = well[0] + 70, well[2] - 70
    base, ceil = well[3] - 60, well[1] + 90
    n = len(heights)
    gap = 26
    bw = (x1 - x0 - gap * (n - 1)) / n
    low, mid, high = (40, 220, 90), (240, 220, 40), (240, 70, 40)
    for i, h in enumerate(heights):
        left = x0 + i * (bw + gap)
        top_y = base - (base - ceil) * h
        # Draw in thin slices so the colour follows the height.
        steps = 40
        for k in range(steps):
            ya = base - (base - top_y) * k / steps
            yb = base - (base - top_y) * (k + 1) / steps
            t = (base - ya) / (base - ceil)
            col = lerp(low, mid, t / 0.6) if t < 0.6 else lerp(mid, high, (t - 0.6) / 0.4)
            d.rectangle([left, yb, left + bw, ya], fill=col + (255,))
        peak = top_y - 36
        d.rectangle([left, peak - 16, left + bw, peak], fill=(210, 220, 255, 255))

    # Gloss over the top half of the panel.
    gloss = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gm = Image.new("L", (S, S), 0)
    ImageDraw.Draw(gm).rounded_rectangle([margin + 16, margin + 16, S - margin - 16, S // 2 - 40], radius - 20, fill=38)
    gloss.putalpha(gm)
    white = Image.new("RGBA", (S, S), (255, 255, 255, 255))
    white.putalpha(gm)
    img.alpha_composite(white)
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    big = draw()
    for n in (16, 24, 32, 48, 64, 128, 256, 512):
        big.resize((n, n), Image.LANCZOS).save(OUT / f"qiyaa-{n}.png", optimize=True)
    big.resize((256, 256), Image.LANCZOS).save(OUT / "qiyaa.png", optimize=True)
    big.resize((256, 256), Image.LANCZOS).save(OUT / "qiyaa.ico", sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
    big.resize((1024, 1024), Image.LANCZOS).save(OUT / "qiyaa.icns")


if __name__ == "__main__":
    main()
