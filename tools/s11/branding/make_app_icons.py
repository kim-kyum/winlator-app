#!/usr/bin/env python3
"""The S11 app icons: a white symbol on the blue tile of the S11 mark (the same gradient and
corner radius as make_logo.py), one per built-in program.

Every symbol is drawn in units of 1/100 of the tile from a few shapes: white fills (some
see-through, for depth) and cut-outs that let the tile show through. The shell draws the
same shapes with GDI+ (draw_s11_tile_glyph in tools/s11/shell/s11ui.h); keep the two alike.

Usage: python -I make_app_icons.py ico <name> <out.ico>     (settings, explorer, gallery, ...)
       python -I make_app_icons.py sheet <out.png>          (every icon at several sizes)
       python -I make_app_icons.py header <s11tiles.h>      (the shapes for the shell, as C)
"""
import math
import sys

from PIL import Image, ImageDraw

TOP_LEFT = (0x5C, 0xC0, 0xFF)
BOTTOM_RIGHT = (0x56, 0x8C, 0xFF)
TILE_RADIUS = 23
SS = 4   # supersampling


def gradient(big):
    image = Image.new("RGBA", (big, big))
    px = image.load()
    for y in range(big):
        for x in range(big):
            t = (x + y) / (2 * (big - 1))
            px[x, y] = tuple(round(TOP_LEFT[i] * (1 - t) + BOTTOM_RIGHT[i] * t) for i in range(3)) + (255,)
    return image


# --- shapes, in tile units -------------------------------------------------------------------

def rrect(l, t, r, b, radius):
    return ("rrect", l, t, r, b, radius)


def poly(*points):
    return ("poly", points)


def circle(cx, cy, radius):
    return ("circle", cx, cy, radius)


def line(width, *points):
    """A polyline with round caps and joins"""
    return ("line", width, points)


def group(*shapes):
    """Shapes filled together, as one: no double alpha where they overlap"""
    return ("group", shapes)


def gear_points(cx, cy, r_out, r_root, teeth, top=0.19, base=0.29):
    step = 2 * math.pi / teeth
    points = []
    for i in range(teeth):
        a = i * step - math.pi / 2
        points += [(cx + r_root * math.cos(a - step * base), cy + r_root * math.sin(a - step * base)),
                   (cx + r_out * math.cos(a - step * top), cy + r_out * math.sin(a - step * top)),
                   (cx + r_out * math.cos(a + step * top), cy + r_out * math.sin(a + step * top)),
                   (cx + r_root * math.cos(a + step * base), cy + r_root * math.sin(a + step * base))]
        for k in range(1, 8):
            ang = a + step * base + (step - 2 * step * base) * k / 8
            points.append((cx + r_root * math.cos(ang), cy + r_root * math.sin(ang)))
    return points


# --- the symbols: (operation, alpha 0-255 or None for a cut-out, shape) ------------------------

WHITE, SOFT, FAINT = 255, 150, 95

ICONS = {
    # a gear with a lighter ring around its hole
    "settings": [
        ("fill", WHITE, ("gear", 50, 50, 31, 23.5, 8)),
        ("cut", None, circle(50, 50, 13)),
        ("fill", SOFT, circle(50, 50, 13)),
        ("cut", None, circle(50, 50, 7.5)),
    ],
    # a folder: the back sheet with its tab see-through, the front one solid
    "explorer": [
        ("fill", SOFT, group(rrect(20, 26, 47, 40, 4.5), rrect(20, 32, 80, 72, 6))),
        ("fill", WHITE, rrect(20, 40, 80, 75, 6)),
    ],
    # a photo: a white frame, mountains and the sun cut out
    "gallery": [
        ("fill", WHITE, rrect(20, 24, 80, 76, 8)),
        ("cut", None, poly((27, 69), (43, 47), (53, 58), (61, 50), (73, 69))),
        ("cut", None, circle(64, 37, 5.5)),
    ],
    # a console window with ">_"
    "terminal": [
        ("fill", WHITE, rrect(18, 25, 82, 75, 8)),
        ("cut", None, line(6, (31, 42), (40, 50), (31, 58))),
        ("cut", None, line(6, (47, 60), (62, 60))),
    ],
    # a page with a folded corner and lines of text
    "notepad": [
        ("fill", WHITE, poly((27, 20), (62, 20), (73, 31), (73, 80), (27, 80))),
        ("cut", None, poly((62, 20), (73, 31), (62, 31))),
        ("fill", SOFT, poly((62, 20), (73, 31), (62, 31))),
        ("cut", None, line(5, (35, 44), (65, 44))),
        ("cut", None, line(5, (35, 55), (65, 55))),
        ("cut", None, line(5, (35, 66), (55, 66))),
    ],
    # a page with a big letter and lines: a formatted document
    "wordpad": [
        ("fill", WHITE, rrect(25, 20, 75, 80, 6)),
        ("cut", None, poly((32, 52), (40, 30), (44, 30), (52, 52), (47, 52), (45, 46), (39, 46), (37, 52))),
        ("fill", WHITE, poly((40.5, 41.5), (42, 36.5), (43.5, 41.5))),
        ("cut", None, line(4.5, (57, 34), (67, 34))),
        ("cut", None, line(4.5, (57, 44), (67, 44))),
        ("cut", None, line(4.5, (33, 61), (67, 61))),
        ("cut", None, line(4.5, (33, 70), (58, 70))),
    ],
    # a monitor of activity: a panel with a pulse line
    "taskmgr": [
        ("fill", WHITE, rrect(18, 24, 82, 76, 8)),
        ("cut", None, line(5.5, (27, 53), (38, 53), (44, 39), (52, 64), (58, 46), (63, 53), (73, 53))),
    ],
    # keys in a tree
    "registry": [
        ("fill", WHITE, rrect(39, 18, 61, 38, 4)),
        ("fill", WHITE, line(5, (50, 38), (50, 47), (31, 47), (31, 57))),
        ("fill", WHITE, line(5, (50, 47), (69, 47), (69, 57))),
        ("fill", WHITE, rrect(20, 57, 42, 77, 4)),
        ("fill", WHITE, rrect(58, 57, 80, 77, 4)),
        ("cut", None, rrect(45, 24, 55, 32, 1.5)),
    ],
    # three sliders: Wine's own settings
    "winecfg": [
        ("fill", SOFT, line(6, (24, 32), (76, 32))),
        ("fill", SOFT, line(6, (24, 50), (76, 50))),
        ("fill", SOFT, line(6, (24, 68), (76, 68))),
        ("fill", WHITE, circle(60, 32, 8.5)),
        ("fill", WHITE, circle(37, 50, 8.5)),
        ("fill", WHITE, circle(55, 68, 8.5)),
    ],
    # a download into a tray: installers
    "installer": [
        ("fill", WHITE, line(7, (50, 20), (50, 50))),
        ("fill", WHITE, poly((35, 43), (65, 43), (50, 59))),
        ("fill", WHITE, line(7, (24, 56), (24, 74), (76, 74), (76, 56))),
    ],
    # a chip
    "gpu": [
        ("fill", WHITE, line(4.5, (36, 18), (36, 28))), ("fill", WHITE, line(4.5, (50, 18), (50, 28))),
        ("fill", WHITE, line(4.5, (64, 18), (64, 28))), ("fill", WHITE, line(4.5, (36, 72), (36, 82))),
        ("fill", WHITE, line(4.5, (50, 72), (50, 82))), ("fill", WHITE, line(4.5, (64, 72), (64, 82))),
        ("fill", WHITE, line(4.5, (18, 36), (28, 36))), ("fill", WHITE, line(4.5, (18, 50), (28, 50))),
        ("fill", WHITE, line(4.5, (18, 64), (28, 64))), ("fill", WHITE, line(4.5, (72, 36), (82, 36))),
        ("fill", WHITE, line(4.5, (72, 50), (82, 50))), ("fill", WHITE, line(4.5, (72, 64), (82, 64))),
        ("fill", WHITE, rrect(26, 26, 74, 74, 7)),
        ("cut", None, rrect(38, 38, 62, 62, 4)),
    ],
    # a cube: Direct3D
    "d3d": [
        ("fill", WHITE, poly((50, 18), (78, 33), (50, 48), (22, 33))),
        ("fill", SOFT, poly((22, 33), (50, 48), (50, 82), (22, 67))),
        ("fill", FAINT + 40, poly((78, 33), (78, 67), (50, 82), (50, 48))),
    ],
    # a mine
    "mines": [
        ("fill", WHITE, line(6, (50, 18), (50, 82))),
        ("fill", WHITE, line(6, (18, 50), (82, 50))),
        ("fill", WHITE, line(6, (28, 28), (72, 72))),
        ("fill", WHITE, line(6, (72, 28), (28, 72))),
        ("fill", WHITE, circle(50, 50, 22)),
        ("cut", None, circle(43, 43, 5.5)),
    ],
    # an archive: a box with a zip
    "archive": [
        ("fill", WHITE, rrect(22, 20, 78, 80, 7)),
        ("cut", None, rrect(45, 20, 55, 66, 0)),
        ("fill", WHITE, rrect(45, 24, 50, 29, 0)), ("fill", WHITE, rrect(50, 32, 55, 37, 0)),
        ("fill", WHITE, rrect(45, 40, 50, 45, 0)), ("fill", WHITE, rrect(50, 48, 55, 53, 0)),
        ("cut", None, rrect(42, 56, 58, 70, 3)),
        ("fill", WHITE, rrect(46.5, 60, 53.5, 66, 1.5)),
    ],
}


def draw_shape(draw, shape, scale, value):
    kind = shape[0]
    if kind == "group":
        for part in shape[1]:
            draw_shape(draw, part, scale, value)
    elif kind == "gear":
        _, cx, cy, r_out, r_root, teeth = shape
        draw.polygon([(x * scale, y * scale) for x, y in gear_points(cx, cy, r_out, r_root, teeth)], fill=value)
    elif kind == "rrect":
        _, l, t, r, b, radius = shape
        draw.rounded_rectangle([l * scale, t * scale, r * scale, b * scale], radius=radius * scale, fill=value)
    elif kind == "poly":
        draw.polygon([(x * scale, y * scale) for x, y in shape[1]], fill=value)
    elif kind == "circle":
        _, cx, cy, radius = shape
        draw.ellipse([(cx - radius) * scale, (cy - radius) * scale, (cx + radius) * scale, (cy + radius) * scale], fill=value)
    elif kind == "line":
        _, width, points = shape
        pts = [(x * scale, y * scale) for x, y in points]
        draw.line(pts, fill=value, width=round(width * scale))
        r = width * scale / 2
        for x, y in pts:
            draw.ellipse([x - r, y - r, x + r, y + r], fill=value)


def render(name, size, tile=True):
    big = size * SS
    scale = big / 100
    glyph = Image.new("L", (big, big), 0)
    for op, alpha, shape in ICONS[name]:
        layer = Image.new("L", (big, big), 0)
        draw_shape(ImageDraw.Draw(layer), shape, scale, 255)
        if op == "cut":
            glyph = Image.composite(Image.new("L", (big, big), 0), glyph, layer)
        else:
            # paint over: this shape's alpha where it is drawn
            glyph = Image.composite(Image.new("L", (big, big), alpha), glyph, layer)
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    if tile:
        mask = Image.new("L", (big, big), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, big - 1, big - 1], radius=round(big * TILE_RADIUS / 100), fill=255)
        image.paste(gradient(big), (0, 0), mask)
    white = Image.new("RGBA", (big, big), (255, 255, 255, 0))
    white.putalpha(glyph)
    image.alpha_composite(white)
    return image.resize((size, size), Image.LANCZOS)


C_CODES = {"rrect": 1, "poly": 2, "circle": 3, "line": 4, "group": 5}


def c_shape(shape, alpha):
    """A shape as numbers: code, alpha (-1 for a cut-out), then its parameters"""
    kind = shape[0]
    if kind == "gear":
        _, cx, cy, r_out, r_root, teeth = shape
        return c_shape(("poly", gear_points(cx, cy, r_out, r_root, teeth)), alpha)
    if kind == "rrect":
        return [1, alpha] + list(shape[1:])
    if kind == "poly":
        return [2, alpha, len(shape[1])] + [v for point in shape[1] for v in point]
    if kind == "circle":
        return [3, alpha] + list(shape[1:])
    if kind == "line":
        return [4, alpha, shape[1], len(shape[2])] + [v for point in shape[2] for v in point]
    if kind == "group":
        out = [5, alpha, len(shape[1])]
        for part in shape[1]:
            out += c_shape(part, alpha)
        return out
    raise ValueError(kind)


def c_number(value):
    value = round(value, 2)
    return "%d" % value if value == int(value) else ("%g" % value) + "f"


def header(out):
    lines = ["/* The S11 app tiles' symbols, generated by tools/s11/branding/make_app_icons.py from the",
             " * same shapes as the .ico files: do not edit, run \"make_app_icons.py header\" instead.",
             " * Units are 1/100 of the tile. Each shape: code (1 rounded rectangle l t r b radius,",
             " * 2 polygon n x y..., 3 circle cx cy r, 4 polyline width n x y..., 5 group n shapes filled",
             " * as one), alpha (-1: cut out, the tile shows through), parameters; 0 ends the list. */",
             "#ifndef S11TILES_H", "#define S11TILES_H", ""]
    names = []
    for name, ops in ICONS.items():
        values = []
        for op, alpha, shape in ops:
            values += c_shape(shape, -1 if op == "cut" else alpha)
        values.append(0)
        text = ", ".join(c_number(v) for v in values)
        wrapped, row = [], ""
        for item in text.split(", "):
            if len(row) + len(item) + 2 > 96:
                wrapped.append(row.rstrip())
                row = ""
            row += item + ", "
        wrapped.append(row.rstrip(", "))
        lines.append("static const float s11_tile_%s[] = {" % name)
        lines += ["    " + w for w in wrapped]
        lines.append("};")
        names.append(name)
    lines += ["", "#endif", ""]
    open(out, "w").write("\n".join(lines))
    print(out)


def main():
    kind = sys.argv[1]
    if kind == "header":
        header(sys.argv[2])
        return
    if kind == "ico":
        name, out = sys.argv[2], sys.argv[3]
        sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
        images = [render(name, s) for s in sizes]
        images[-1].save(out, format="ICO", sizes=[(s, s) for s in sizes], append_images=images[:-1])
        print(out)
    elif kind == "sheet":
        out = sys.argv[2]
        sizes = [128, 48, 32, 24, 16]
        names = list(ICONS)
        cell = sum(sizes) + 12 * len(sizes)
        sheet = Image.new("RGBA", (cell, len(names) * 140 + 20), (243, 244, 248, 255))
        for row, name in enumerate(names):
            x = 10
            for s in sizes:
                icon = render(name, s)
                sheet.alpha_composite(icon, (x, 10 + row * 140 + (128 - s) // 2))
                x += s + 12
        sheet.save(out)
        print(out)


if __name__ == "__main__":
    main()
