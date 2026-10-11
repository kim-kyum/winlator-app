#!/usr/bin/env python3
"""The Winlator S11 mark: a blue-violet tile with two light ribbons, like the wallpaper.

Usage: python -I make_logo.py ico <out.ico>
       python -I make_logo.py png <size> <out.png> [--no-tile]
       python -I make_logo.py android <res dir> <bold font.ttf>
The shell draws the same mark with GDI+ (draw_logo in tools/s11/shell/s11shell.c).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

TOP_LEFT = (0x2F, 0x7B, 0xFF)
BOTTOM_RIGHT = (0x7B, 0x5C, 0xFF)


def bezier(p0, p1, p2, p3, steps=600):
    points = []
    for i in range(steps + 1):
        t = i / steps
        a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t ** 2, t ** 3
        points.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return points


def ribbon(layer, size, points, width, alpha):
    """A round-capped stroke: stamp discs densely along the curve into a mask."""
    mask = Image.new("L", layer.size, 0)
    draw = ImageDraw.Draw(mask)
    r = max(0.5, width * size / 2)
    for x, y in points:
        x, y = x * size, y * size
        draw.ellipse([x - r, y - r, x + r, y + r], fill=255)
    color = Image.new("RGBA", layer.size, (255, 255, 255, 0))
    color.putalpha(mask.point(lambda v: v * alpha // 255))
    layer.alpha_composite(color)


def render(size, tile=True):
    ss = 4
    big = size * ss
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))

    if tile:
        gradient = Image.new("RGBA", (big, big))
        px = gradient.load()
        for y in range(big):
            for x in range(big):
                t = (x + y) / (2 * (big - 1))
                px[x, y] = tuple(round(TOP_LEFT[i] * (1 - t) + BOTTOM_RIGHT[i] * t) for i in range(3)) + (255,)
        mask = Image.new("L", (big, big), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, big - 1, big - 1], radius=round(big * 0.24), fill=255)
        image.paste(gradient, (0, 0), mask)

    layer = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    ribbon(layer, big, bezier((0.18, 0.50), (0.42, 0.82), (0.60, 0.22), (0.82, 0.52)), 0.06, 179)
    image = Image.alpha_composite(image, layer)
    layer = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    ribbon(layer, big, bezier((0.18, 0.72), (0.40, 0.30), (0.58, 0.78), (0.82, 0.30)), 0.085, 255)
    image = Image.alpha_composite(image, layer)
    return image.resize((size, size), Image.LANCZOS)


def circle(image):
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, image.size[0] - 1, image.size[1] - 1], fill=255)
    out = Image.new("RGBA", image.size, (0, 0, 0, 0))
    full = render_square(image.size[0])
    out.paste(full, (0, 0), mask)
    return out


def render_square(size):
    """The tile without rounded corners (for masks the launcher applies itself)."""
    ss = 4
    big = size * ss
    tile = render(size * 2, tile=True).resize((big, big), Image.LANCZOS)
    # fill the corners: draw the gradient full-bleed under the tile
    gradient = Image.new("RGBA", (big, big))
    px = gradient.load()
    for y in range(big):
        for x in range(big):
            t = (x + y) / (2 * (big - 1))
            px[x, y] = tuple(round(TOP_LEFT[i] * (1 - t) + BOTTOM_RIGHT[i] * t) for i in range(3)) + (255,)
    gradient.alpha_composite(tile)
    return gradient.resize((size, size), Image.LANCZOS)


def android(res, font_path):
    densities = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
    for name, scale in densities.items():
        folder = os.path.join(res, "mipmap-" + name)
        os.makedirs(folder, exist_ok=True)
        legacy = round(48 * scale)
        render(legacy).save(os.path.join(folder, "ic_launcher.png"))
        circle(Image.new("RGBA", (legacy, legacy))).save(os.path.join(folder, "ic_launcher_round.png"))
        # adaptive foreground: 108 dp canvas, the mark in the middle 72 dp
        canvas = round(108 * scale)
        inner = round(72 * scale)
        foreground = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
        art = render(inner, tile=False)
        offset = (canvas - inner) // 2
        foreground.paste(art, (offset, offset), art)
        foreground.save(os.path.join(folder, "ic_launcher_foreground.png"))

    # drawer header wordmark: the mark and "Winlator S11" in white
    height = 208
    mark = render(height)
    font = ImageFont.truetype(font_path, round(height * 0.42))
    text = "Winlator S11"
    width = height + round(height * 0.22) + round(font.getlength(text)) + 8
    logo = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    logo.paste(mark, (0, 0), mark)
    draw = ImageDraw.Draw(logo)
    bbox = draw.textbbox((0, 0), text, font=font)
    y = (height - (bbox[3] - bbox[1])) // 2 - bbox[1]
    draw.text((height + round(height * 0.22), y), text, font=font, fill=(255, 255, 255, 255))
    folder = os.path.join(res, "drawable-xxhdpi")
    os.makedirs(folder, exist_ok=True)
    logo.save(os.path.join(folder, "logo.png"))


def main():
    kind = sys.argv[1]
    if kind == "android":
        android(sys.argv[2], sys.argv[3])
        print("android icons written")
        return
    if kind == "ico":
        out = sys.argv[2]
        sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
        images = [render(s) for s in sizes]
        images[-1].save(out, format="ICO", sizes=[(s, s) for s in sizes], append_images=images[:-1])
    else:
        size, out = int(sys.argv[2]), sys.argv[3]
        render(size, tile="--no-tile" not in sys.argv).save(out)
    print(out)


if __name__ == "__main__":
    main()
