"""Build "S11 Sans" (Winlator S11's UI font) from Microsoft's Selawik UFO sources.

Selawik (https://github.com/microsoft/Selawik, SIL OFL 1.1) is the open-source,
metric-compatible stand-in for Segoe UI. Its repository only ships UFO sources, so
this does the minimum fontmake would: cubic -> quadratic outlines (reversed to
TrueType direction), glyf/cmap/hmtx, and naming that lets Windows and Wine pick the
right face. No OpenType features or hinting (the sources have no kerning; FreeType
autohints).

Vertical metrics follow Segoe UI (winAscent 2210, winDescent 514 at 2048 UPM), so
text set in "Segoe UI" keeps its line height when this font stands in for it.

Because the result is a Modified Version under the OFL, it does not use the Reserved
Font Name "Selawik": the family is "S11 Sans". Wine maps "Segoe UI" to it.

Usage: python -I build_s11sans.py <Selawik "Source files/UFO" dir> <out dir>
"""
import os
import plistlib
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.pointPen import AbstractPointPen, PointToSegmentPen
from fontTools.pens.recordingPen import RecordingPointPen
from fontTools.pens.transformPen import TransformPointPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ufoLib import UFOReader

WIN_ASCENT, WIN_DESCENT = 2210, 514

# (ufo, family for GDI, style for GDI, typographic subfamily, weight, bold flag)
FAMILY = "S11 Sans"
FACES = [
    ("Selawik-Regular.ufo", FAMILY, "Regular", "Regular", 400, False, "s11sans-regular.ttf"),
    ("Selawik-Bold.ufo", FAMILY, "Bold", "Bold", 700, True, "s11sans-bold.ttf"),
    ("Selawik-Light.ufo", FAMILY + " Light", "Regular", "Light", 300, False, "s11sans-light.ttf"),
]


class GlyphData:
    pass


def compile_face(ufo_path, family, style, typo_style, weight, bold, out_path):
    reader = UFOReader(ufo_path, validate=False)
    glyph_set = reader.getGlyphSet()
    info = plistlib.load(open(os.path.join(ufo_path, "fontinfo.plist"), "rb"))
    upm = info.get("unitsPerEm", 2048)

    names = sorted(glyph_set.keys())
    if ".notdef" not in names:
        raise SystemExit(f"{ufo_path}: no .notdef")
    order = [".notdef"] + [n for n in names if n != ".notdef"]

    # Read every glyph once into a recording
    recordings, widths, cmap = {}, {}, {}
    for name in order:
        data = GlyphData()
        rec = RecordingPointPen()
        glyph_set.readGlyph(name, data, rec)
        recordings[name] = rec
        widths[name] = int(round(getattr(data, "width", 0)))
        for u in getattr(data, "unicodes", []) or []:
            cmap[u] = name

    class Decomposer(AbstractPointPen):
        """Forwards outlines; draws components inline (for glyphs mixing both)."""
        def __init__(self, out):
            self.out = out

        def beginPath(self, identifier=None, **kwargs):
            self.out.beginPath()

        def addPoint(self, pt, segmentType=None, smooth=False, name=None, identifier=None, **kwargs):
            self.out.addPoint(pt, segmentType, smooth, name)

        def endPath(self):
            self.out.endPath()

        def addComponent(self, baseGlyphName, transformation, identifier=None, **kwargs):
            recordings[baseGlyphName].replay(TransformPointPen(self, transformation))

    glyphs = {}
    for name in order:
        rec = recordings[name]
        methods = {m for m, _, _ in rec.value}
        mixed = "addComponent" in methods and "beginPath" in methods
        tt_pen = TTGlyphPen(glyph_set, handleOverflowingTransforms=False)
        # max_err 1 unit at 2048 UPM, as fontmake does; reverse for TrueType winding
        point_pen = PointToSegmentPen(Cu2QuPen(tt_pen, max_err=1.0, reverse_direction=True))
        # TrueType glyphs hold contours or components, not both: decompose the mixed ones
        rec.replay(Decomposer(point_pen) if mixed else point_pen)
        glyphs[name] = tt_pen.glyph()

    # No-break space shares the space glyph, as in Segoe UI
    if 0x00A0 not in cmap and "space" in glyphs:
        cmap[0x00A0] = "space"

    fb = FontBuilder(upm, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    glyf = fb.font["glyf"]
    metrics = {}
    for name in order:
        g = glyf[name]
        g.recalcBounds(glyf)
        metrics[name] = (widths[name], getattr(g, "xMin", 0))
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=WIN_ASCENT, descent=-WIN_DESCENT, lineGap=0)

    version = f"{info.get('versionMajor', 1)}.{info.get('versionMinor', 0):03d}"
    full = family if style == "Regular" else f"{family} {style}"
    ps = (family.replace(" ", "") + "-" + style).replace("-Regular", "-Regular")
    names_table = {
        "copyright": info.get("copyright", ""),
        "familyName": family,
        "styleName": style,
        "uniqueFontIdentifier": f"Winlator S11: {full} {version}",
        "fullName": full,
        "version": f"Version {version}",
        "psName": ps,
        "description": "S11 Sans is a modified version of Selawik by Microsoft, built for Winlator S11.",
        "manufacturer": info.get("openTypeNameManufacturer", ""),
        "designer": info.get("openTypeNameDesigner", ""),
        "vendorURL": info.get("openTypeNameManufacturerURL", ""),
        "designerURL": info.get("openTypeNameDesignerURL", ""),
        "licenseDescription": info.get("openTypeNameLicense", ""),
        "licenseInfoURL": info.get("openTypeNameLicenseURL", ""),
    }
    if family != FAMILY:
        names_table["typographicFamily"] = FAMILY
        names_table["typographicSubfamily"] = typo_style
    fb.setupNameTable(names_table)

    fs_selection = 0x20 if bold else 0x40  # BOLD or REGULAR; USE_TYPO_METRICS off, so GDI uses win metrics
    typo_asc, typo_desc = info.get("ascender", 1491), info.get("descender", -431)
    fb.setupOS2(
        version=4,
        usWeightClass=weight,
        usWidthClass=5,
        fsType=0,
        fsSelection=fs_selection,
        achVendID="WS11",
        sTypoAscender=typo_asc,
        sTypoDescender=typo_desc,
        sTypoLineGap=WIN_ASCENT + WIN_DESCENT - (typo_asc - typo_desc),
        usWinAscent=WIN_ASCENT,
        usWinDescent=WIN_DESCENT,
        sxHeight=info.get("xHeight", 974),
        sCapHeight=info.get("capHeight", 1434),
        ulUnicodeRange1=0x00000007,  # Basic Latin, Latin-1 Supplement, Latin Extended-A
        ulCodePageRange1=0x00000001,  # Latin 1
    )
    fb.setupPost(keepGlyphNames=True)
    fb.font["head"].macStyle = 1 if bold else 0
    fb.font["head"].fontRevision = float(version)
    fb.save(out_path)
    return len(order), len(cmap)


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for ufo, family, style, typo_style, weight, bold, file_name in FACES:
        n_glyphs, n_chars = compile_face(os.path.join(src, ufo), family, style, typo_style, weight, bold,
                                         os.path.join(out, file_name))
        print(f"{file_name}: {n_glyphs} glyphs, {n_chars} characters")


if __name__ == "__main__":
    main()
