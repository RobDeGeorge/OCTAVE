"""One-off: turn the quote on the back of the board into outlines, written to art.json (silk.py draws it).

    python3 mkart.py /path/to/UncialAntiqua-Regular.ttf

The font is Uncial Antiqua (Astigmatic, SIL Open Font License 1.1; fonts.google.com/specimen/Uncial+Antiqua).
It is not kept in the repo: only the letters' outlines are, in art.json, so the build needs no font.
Rerun this to change the words. The wizard is wizard.txt, drawn cell by cell by silk.py.
"""
import json
import sys

from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.ttLib import TTFont

QUOTE = ["A wizard is never late,", "nor is he early.", "He arrives precisely", "when he means to."]
STEPS = 4       # line segments per curve


def _contours(font, glyph):
    """The glyph's closed contours as point lists, in font units (y up)."""
    pen = DecomposingRecordingPen(font.getGlyphSet())
    font.getGlyphSet()[glyph].draw(pen)
    out, cur = [], []
    for op, pts in pen.value:
        if op == "moveTo":
            cur = [pts[0]]
        elif op == "lineTo":
            cur.append(pts[0])
        elif op == "qCurveTo":          # TrueType: on-curve points implied between consecutive off-curve ones
            ctrl, end = list(pts[:-1]), pts[-1]
            p0 = cur[-1]
            for i, c in enumerate(ctrl):
                p2 = end if i == len(ctrl) - 1 else ((c[0] + ctrl[i + 1][0]) / 2, (c[1] + ctrl[i + 1][1]) / 2)
                for k in range(1, STEPS + 1):
                    t = k / STEPS
                    cur.append(((1 - t) ** 2 * p0[0] + 2 * t * (1 - t) * c[0] + t * t * p2[0],
                                (1 - t) ** 2 * p0[1] + 2 * t * (1 - t) * c[1] + t * t * p2[1]))
                p0 = p2
        elif op == "curveTo":
            p0, (c1, c2, p3) = cur[-1], pts
            for k in range(1, STEPS + 1):
                t = k / STEPS
                cur.append(tuple((1 - t) ** 3 * p0[j] + 3 * t * (1 - t) ** 2 * c1[j] + 3 * t * t * (1 - t) * c2[j] + t ** 3 * p3[j]
                                 for j in (0, 1)))
        elif op in ("closePath", "endPath"):
            if len(cur) > 2:
                out.append(cur)
            cur = []
    return out


def _area(c):
    return sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(c, c[1:] + c[:1], strict=True)) / 2


def _inside(p, c):
    n = False
    for (x1, y1), (x2, y2) in zip(c, c[1:] + c[:1], strict=True):
        if (y1 > p[1]) != (y2 > p[1]) and p[0] < x1 + (p[1] - y1) * (x2 - x1) / (y2 - y1):
            n = not n
    return n


def main(path):
    font = TTFont(path)
    cmap, hmtx, em = font.getBestCmap(), font["hmtx"], font["head"].unitsPerEm
    lines = []
    for text in QUOTE:
        x, shapes = 0.0, []       # a shape: [outline, hole, hole ...], in em (y up, x from the line's start)
        for ch in text:
            g = cmap[ord(ch)]
            cs = [[(round((px + x) / em, 4), round(py / em, 4)) for px, py in c] for c in _contours(font, g)]
            outer = [c for c in cs if _area(c) < 0]        # TrueType: outlines run clockwise
            for o in outer:
                shapes.append([o] + [h for h in cs if _area(h) > 0 and _inside(h[0], o)])
            x += hmtx[g][0]
        lines.append({"text": text, "width": round(x / em, 4), "shapes": shapes})
    with open("art.json", "w") as f:
        json.dump({"font": "Uncial Antiqua (SIL OFL 1.1)", "lines": lines}, f, separators=(",", ":"))
    print("art.json:", [(ln["text"], ln["width"], len(ln["shapes"])) for ln in lines])


if __name__ == "__main__":
    main(sys.argv[1])
