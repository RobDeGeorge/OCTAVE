#!/usr/bin/env python3
"""Plain 3D shapes (VRML) for the parts KiCad has no model of: the cell, the module, the encoder, the LEDs.

They are for the 3D view only (a look at the stack, clearances by eye), not mechanical data.
KiCad reads VRML in units of 0.1 inch; footprint +y is model -y.
"""

import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib", "3dmodels")
U = 1 / 2.54    # mm -> VRML units


def _mat(rgb, shiny=0.3):
    return (f"appearance Appearance {{ material Material {{ diffuseColor {rgb[0]} {rgb[1]} {rgb[2]} "
            f"specularColor 0.5 0.5 0.5 shininess {shiny} }} }}")


def box(x, y, z, w, d, h, rgb):
    """A box w x d x h (mm) centred at x, y with its base at z."""
    return (f"Transform {{ translation {x * U:.4f} {y * U:.4f} {(z + h / 2) * U:.4f} children [ Shape {{ {_mat(rgb)} "
            f"geometry Box {{ size {w * U:.4f} {d * U:.4f} {h * U:.4f} }} }} ] }}\n")


def cyl(x, y, z, dia, h, rgb, axis="z", n=32):
    """A cylinder as a faceted prism (KiCad's VRML reader has no Cylinder). axis z: standing, its
    base at z. axis x: lying, centred at x, y, its axis at height z."""
    import math
    r = dia / 2
    pts = []
    for end in (0, 1):
        for k in range(n):
            a = 2 * math.pi * k / n
            if axis == "z":
                pts.append((x + r * math.cos(a), y + r * math.sin(a), z + end * h))
            else:
                pts.append((x - h / 2 + end * h, y + r * math.cos(a), z + r * math.sin(a)))
    faces = []
    for k in range(n):
        k2 = (k + 1) % n
        faces.append((k, k2, n + k2, n + k))
    faces.append(tuple(reversed(range(n))))
    faces.append(tuple(range(n, 2 * n)))
    coord = ", ".join(f"{px * U:.4f} {py * U:.4f} {pz * U:.4f}" for px, py, pz in pts)
    index = ", ".join(" ".join(str(v) for v in f) + " -1" for f in faces)
    return (f"Shape {{ {_mat(rgb, 0.6)} geometry IndexedFaceSet {{ solid FALSE creaseAngle 1.0 "
            f"coord Coordinate {{ point [ {coord} ] }} coordIndex [ {index} ] }} }}\n")


def write(name, body):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name), "w") as f:
        f.write("#VRML V2.0 utf8\n" + body)


if __name__ == "__main__":
    # Nichicon SLB12400: 12.5 x 40 mm can from 1.5 mm behind its holes, lying on the board
    write("Nichicon_SLB12400.wrl",
          cyl(21.5, 0, 6.25, 12.5, 40, (0.10, 0.25, 0.55), axis="x")
          + cyl(0.75, 2.5, 6.25, 0.8, 1.5, (0.8, 0.8, 0.8), axis="x")
          + cyl(0.75, -2.5, 6.25, 0.8, 1.5, (0.8, 0.8, 0.8), axis="x")
          # the leads bent down into the holes and through the board
          + cyl(0, 2.5, -2.4, 0.8, 9.05, (0.8, 0.8, 0.8))
          + cyl(0, -2.5, -2.4, 0.8, 9.05, (0.8, 0.8, 0.8)))
    # WS2812B-2020: 2.0 x 2.0 x 0.84 mm, white body, clear window
    write("WS2812B-2020.wrl",
          box(0, 0, 0, 2.0, 2.0, 0.7, (0.93, 0.93, 0.9))
          + cyl(0, 0, 0.7, 1.5, 0.14, (0.98, 0.95, 0.7)))
    # ESP32-C3-MINI-1: 13.2 x 16.6 board (footprint y -11.0 .. 5.6), shield over the pads' end
    write("ESP32-C3-MINI-1.wrl",
          box(0, 2.7, 0, 13.2, 16.6, 0.8, (0.05, 0.05, 0.05))
          + box(0, 0.2, 0.8, 11.6, 10.6, 1.6, (0.75, 0.75, 0.78))
          + box(0, 8.3, 0.8, 11.0, 0.3, 0.05, (0.85, 0.65, 0.2)))
    # Alps EC11J: frame 11.7 x 13 x 4.5, bushing, 6 mm shaft to 24.5 mm above the board
    write("Alps_EC11J.wrl",
          box(0, 0, 0, 11.7, 13.0, 4.5, (0.55, 0.57, 0.6))
          + box(0, 0, 0, 15.0, 3.0, 1.0, (0.55, 0.57, 0.6))
          + cyl(0, 0, 4.5, 9.0, 5.0, (0.7, 0.7, 0.72))
          + cyl(0, 0, 9.5, 6.0, 15.0, (0.8, 0.8, 0.82)))
    print("3D shapes in", os.path.normpath(OUT))
