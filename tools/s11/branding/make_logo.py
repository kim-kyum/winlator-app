#!/usr/bin/env python3
"""The Winlator S11 mark: three windows rising from the lower left to the upper right on a
blue-violet tile (the colours of the wallpaper). The back two are see-through, the front
one is solid with a title bar: Windows programs moving forward onto the tablet.

Usage: python -I make_logo.py ico <out.ico>
       python -I make_logo.py png <size> <out.png> [--no-tile]
       python -I make_logo.py android <res dir> <bold font.ttf>
The shell draws the same mark with GDI+ (draw_logo in tools/s11/shell/s11ui.h). The icons of
the built-in programs (Settings, File Explorer, ...) come from make_app_icons.py.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

TOP_LEFT = (0x5C, 0xC0, 0xFF)
BOTTOM_RIGHT = (0x56, 0x8C, 0xFF)


# The panes in units of 1/100 of the tile: (left, top, right, bottom), fill colour
PANES = [
    ((14, 44, 50, 84), (0x55, 0xA7, 0xFF, 255)),
    ((28, 31, 66, 71), (0x9A, 0xD5, 0xFF, 255)),
    ((44, 16, 86, 58), (0xFF, 0xFF, 0xFF, 255)),
]
PANE_RADIUS = 8
TITLE_BAR = (11, 3.2)   # the gap under the front pane's title bar: offset from its top, height


def render(size, tile=True):
    ss = 4
    big = size * ss
    u = big / 100
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))

    if tile:
        mask = Image.new("L", (big, big), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, big - 1, big - 1], radius=round(big * 0.23), fill=255)
        image.paste(gradient_image(big), (0, 0), mask)

    for i, ((left, top, right, bottom), color) in enumerate(PANES):
        layer = Image.new("RGBA", (big, big), (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        draw.rounded_rectangle([left * u, top * u, right * u, bottom * u], radius=PANE_RADIUS * u,
                               fill=color)
        if i == len(PANES) - 1:
            y = top + TITLE_BAR[0]
            draw.rectangle([left * u, y * u, right * u, (y + TITLE_BAR[1]) * u], fill=(0, 0, 0, 0))
        image = Image.alpha_composite(image, layer)
    return image.resize((size, size), Image.LANCZOS)


def gradient_image(big):
    image = Image.new("RGBA", (big, big))
    px = image.load()
    for y in range(big):
        for x in range(big):
            t = (x + y) / (2 * (big - 1))
            px[x, y] = tuple(round(TOP_LEFT[i] * (1 - t) + BOTTOM_RIGHT[i] * t) for i in range(3)) + (255,)
    return image


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

    # drawer header wordmark: the mark and "S11 Workspace" in white
    height = 208
    mark = render(height)
    font = ImageFont.truetype(font_path, round(height * 0.42))
    text = "S11 Workspace"
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
