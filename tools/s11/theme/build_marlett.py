#!/usr/bin/env python3
"""Thin, Windows 10-style caption glyphs for Wine's Marlett font.

Wine draws the title bar buttons (minimize, maximize, restore, close) as Marlett
characters '0', '1', '2' and 'r', centred in the button and sized to its height.
Wine's own Marlett has heavy Windows 95 shapes; this replaces just those four glyphs
with 1-2 px line drawings and leaves the rest of the font (check marks, arrows,
radio parts) alone. The font revision goes up so this copy wins over any other
Marlett that Wine finds.

Usage: build_marlett.py <wine marlett.ttf> <out.ttf>
"""
import sys

from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

EM = 2048
CX, CY = 1024, 1023      # Wine centres the em box (advance 2048, height 2050) in the button
SIZE = 760               # glyph box, ~1/3 of the caption height like Windows 10
STROKE = 92              # ~1.6 px at the 144 dpi caption size


def rect(pen, x0, y0, x1, y1, hole=False):
    """A rectangle contour: clockwise for ink, counter-clockwise for a hole (y up)."""
    points = [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]
    if hole:
        points.reverse()
    pen.moveTo(points[0])
    for p in points[1:]:
        pen.lineTo(p)
    pen.closePath()


def frame(pen, x0, y0, x1, y1, w):
    rect(pen, x0, y0, x1, y1)
    rect(pen, x0 + w, y0 + w, x1 - w, y1 - w, hole=True)


def bar(pen, a, b, w):
    """A straight bar of width w from point a to point b (clockwise)."""
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    length = (dx * dx + dy * dy) ** 0.5
    nx, ny = -dy / length * w / 2, dx / length * w / 2
    points = [(ax + nx, ay + ny), (bx + nx, by + ny), (bx - nx, by - ny), (ax - nx, ay - ny)]
    # make it clockwise in a y-up system
    area = sum(points[i][0] * points[(i + 1) % 4][1] - points[(i + 1) % 4][0] * points[i][1] for i in range(4))
    if area > 0:
        points.reverse()
    pen.moveTo(tuple(round(v) for v in points[0]))
    for p in points[1:]:
        pen.lineTo(tuple(round(v) for v in p))
    pen.closePath()


def glyph_minimize(pen):
    h = SIZE // 2
    rect(pen, CX - h, CY - STROKE // 2, CX + h, CY + STROKE - STROKE // 2)


def glyph_maximize(pen):
    h = SIZE // 2
    frame(pen, CX - h, CY - h, CX + h, CY + h, STROKE)


def glyph_restore(pen):
    h = SIZE // 2
    off = SIZE // 5
    # front window, lower left
    fx0, fy0, fx1, fy1 = CX - h, CY - h, CX + h - off, CY + h - off
    frame(pen, fx0, fy0, fx1, fy1, STROKE)
    # back window, upper right: only the parts not hidden behind the front one
    bx0, by0, bx1, by1 = fx0 + off, fy0 + off, fx1 + off, fy1 + off
    rect(pen, bx0, by1 - STROKE, bx1, by1)              # top edge
    rect(pen, bx1 - STROKE, by0, bx1, by1 - STROKE)     # right edge
    rect(pen, bx0, fy1, bx0 + STROKE, by1 - STROKE)     # left stub down to the front window
    rect(pen, fx1, by0, bx1 - STROKE, by0 + STROKE)     # bottom stub across to the front window


def glyph_close(pen):
    h = SIZE // 2
    w = round(STROKE * 1.05)  # diagonals look thinner than straight lines at the same width
    bar(pen, (CX - h, CY - h), (CX + h, CY + h), w)
    bar(pen, (CX - h, CY + h), (CX + h, CY - h), w)


def main():
    src, out = sys.argv[1], sys.argv[2]
    font = TTFont(src)
    glyf = font["glyf"]
    for name, draw in (("zero", glyph_minimize), ("one", glyph_maximize), ("two", glyph_restore),
                       ("r", glyph_close)):
        pen = TTGlyphPen(None)
        draw(pen)
        glyph = pen.glyph()
        glyph.recalcBounds(glyf)
        glyf[name] = glyph
        width, _ = font["hmtx"][name]
        font["hmtx"][name] = (width, glyph.xMin)
        # no hinting instructions: let FreeType antialias the thin lines
    font["head"].fontRevision = 2.0
    for record in font["name"].names:
        if record.nameID in (3, 5):
            text = "Version 2.00 Winlator S11" if record.nameID == 5 else "Winlator S11 Marlett 2.00"
            record.string = text
    font.save(out)
    print(out)


if __name__ == "__main__":
    main()
