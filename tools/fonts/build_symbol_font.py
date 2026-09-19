#!/usr/bin/env python3
"""Rebuild frontend/assets/fonts/symbols/OCTAVESymbols-Regular.ttf.

Qt on Android has no usable symbol-font fallback (its hardcoded fallback list
names Droid fonts that modern Android no longer ships), so any glyph missing
from the UI font renders as a box. Every icon-like glyph the QML draws with a
Text element therefore uses App.Style.symbolFont, backed by this small OFL
subset of Noto Sans Symbols 2 (+ the music notes from Noto Sans Symbols),
renamed "OCTAVE Symbols" as the OFL's Reserved Font Name clause requires.

Add a code point to GLYPHS whenever a QML Text starts using a new symbol,
then rerun:  venv/bin/pip install fonttools && python tools/fonts/build_symbol_font.py
"""
import os, sys, urllib.request, tempfile
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools.merge import Merger

GLYPHS = [0x00D7, 0x2316, 0x2328, 0x232B, 0x25B2, 0x25B6, 0x25BC, 0x25C9,
          0x25CF, 0x25EC, 0x2600, 0x26A0, 0x2713, 0x2717, 0x2B07, 0x2B6E, 0x1F3A7]
NOTES = [0x2194, 0x2195, 0x2669, 0x266A, 0x266B]   # arrows + music notes live in Noto Sans Symbols (not 2)
BASE = "https://raw.githubusercontent.com/google/fonts/main/ofl/"
SRC2 = BASE + "notosanssymbols2/NotoSansSymbols2-Regular.ttf"
SRC1 = BASE + "notosanssymbols/NotoSansSymbols%5Bwght%5D.ttf"
OUT = os.path.join(os.path.dirname(__file__), "..", "..", "frontend", "assets", "fonts", "symbols", "OCTAVESymbols-Regular.ttf")

def sub(src, dst, unicodes):
    opts = subset.Options(); opts.name_IDs = ["*"]; opts.notdef_outline = True; opts.layout_features = []
    f = subset.load_font(src, opts); s = subset.Subsetter(opts); s.populate(unicodes=unicodes); s.subset(f); subset.save_font(f, dst, opts)

with tempfile.TemporaryDirectory() as td:
    s2, s1 = os.path.join(td, "s2.ttf"), os.path.join(td, "s1var.ttf")
    urllib.request.urlretrieve(SRC2, s2); urllib.request.urlretrieve(SRC1, s1)
    static = os.path.join(td, "s1.ttf")
    instancer.instantiateVariableFont(TTFont(s1), {"wght": 400}).save(static)
    a, b = os.path.join(td, "a.ttf"), os.path.join(td, "b.ttf")
    sub(s2, a, GLYPHS); sub(static, b, NOTES)
    merged = Merger().merge([a, b])
    for rec in merged["name"].names:
        if rec.nameID in (1, 4, 16): rec.string = "OCTAVE Symbols"
        elif rec.nameID == 6: rec.string = "OCTAVESymbols-Regular"
        elif rec.nameID == 3: rec.string = "OCTAVESymbols-Regular;subset of Noto Sans Symbols 2 + Noto Sans Symbols"
    merged.save(OUT)
cm = TTFont(OUT).getBestCmap()
missing = [hex(c) for c in GLYPHS + NOTES if c not in cm]
print("wrote", os.path.normpath(OUT), os.path.getsize(OUT), "bytes; missing:", missing or "none")
sys.exit(1 if missing else 0)
