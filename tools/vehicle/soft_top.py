"""Continuous soft-top envelope with shared roof/quarter/rear boundaries.

Dimensions are provisional. Fabric and vinyl share an edge-connected surface;
this intentionally models the installed top, not its future removal/folding rig.
"""
import math
import bmesh


def build_top(top_mesh, top_tube, fabric, vinyl, black):
    def roof(t, y):
        blend = min(1., max(0., (y + .275) / .30))
        return 1.792 + blend * (.018 + .045 * (1-t*t))

    xs = [-1 + i/16 for i in range(33)]
    ys = sorted(set([-.275 + 1.99*j/40 for j in range(41)] + [.61, .75, 1.60]))
    vdoor = (1.77 - 1.26)/(roof(1, .61)-1.26)
    vs = sorted(set([0., .18, .25, .45, .65, .80, vdoor, (1+vdoor)/2, 1.]))
    verts, faces, materials, smoothing = [], [], [], []

    def grid(rows, material_at, smooth=False):
        offset = len(verts)
        verts.extend(p for row in rows for p in row)
        n = len(rows[0])
        for j in range(len(rows)-1):
            for i in range(n-1):
                faces.append((offset+j*n+i, offset+j*n+i+1, offset+(j+1)*n+i+1, offset+(j+1)*n+i))
                materials.append(material_at(j,i))
                smoothing.append(smooth)

    grid([[(.705*t,y,roof(t,y)) for t in xs] for y in ys], lambda j,i:0, True)

    def side(s,y,v):
        return (s*(.75-.045*v), y + .055*(1-v)*(y-.61)/(1.715-.61),
                1.26 + v*(roof(1,y)-1.26))

    for s in (-1,1):
        side_ys = [y for y in ys if y >= .61]
        grid([[side(s,y,v) for v in vs] for y in side_ys],
             lambda j,i: int(.75 <= (side_ys[j]+side_ys[j+1])/2 <= 1.60
                             and .18 < (vs[i]+vs[i+1])/2 < .80))
        # Door-top valance uses the same roof edge and meets the quarter panel
        # exactly. It is fabric above an intentionally open door aperture.
        door_ys = [y for y in ys if y <= .61]
        grid([[(s*(.75-.045*(vdoor+(1-vdoor)*w)), y,
                1.77+w*(roof(1,y)-1.77)) for w in (0.,.5,1.)] for y in door_ys], lambda j,i:0)
        outline = [side(s,.75,.18),side(s,1.60,.18),side(s,1.60,.80),side(s,.75,.80)]
        top_tube("Side window zipper piping", [(x+s*.002,y,z) for x,y,z in outline+[outline[0]]], .0025)
        top_tube("Door surround rail", [(s*.697,-.275,1.772),(s*.697,.61,1.772),(s*.735,.61,1.27)], .009)
        top_tube("Soft top lower retainer", [(s*.749,.61,1.26),(s*.749,1.77,1.26)], .006)

    def rear(t,v):
        return (t*(.75-.045*v),1.77-.055*v,1.26+v*(roof(t,1.715)-1.26))

    grid([[rear(t,v) for t in xs] for v in vs],
         lambda j,i:int(abs((xs[i]+xs[i+1])/2)<.8125 and .18<(vs[j]+vs[j+1])/2<.80))
    # Rear window trim follows its curved top edge instead of cutting across it.
    loop = [rear(t,.18) for t in xs if abs(t)<=.8125]
    loop += [rear(.8125,v) for v in vs if .18<v<=.80]
    loop += [rear(t,.80) for t in reversed(xs) if -.8125<=t<.8125]
    loop += [rear(-.8125,v) for v in reversed(vs) if .18<v<.80]
    top_tube("Rear window zipper", [(x,y+.002,z) for x,y,z in loop+[loop[0]]], .0025)
    top_tube("Soft top tailgate retainer bar", [(-.65,1.77,1.29),(.65,1.77,1.29)], .010)

    shell = top_mesh("Soft top continuous shell", verts, faces, fabric)
    shell.data.materials.append(vinyl)
    for poly, mat, smooth in zip(shell.data.polygons, materials, smoothing):
        poly.material_index = mat
        poly.use_smooth = smooth
    bm = bmesh.new()
    bm.from_mesh(shell.data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(shell.data)
    bm.free()
    shell.modifiers["Fabric thickness"].offset = -1

    for y in (.64,1.66):
        # Actual bow centerline stays below the cloth; the old fixed-height
        # polygon pierced the roof and made a pointed seam float above it.
        crown = [( .67*t, y, roof(.67*t/.705,y)-.022) for t in xs]
        top_tube("Soft top tubular support bow",
                 [(-.715,.64,1.28),(-.685,y,1.74)] + crown + [(.685,y,1.74),(.715,.64,1.28)], .009)
        top_tube("Soft top transverse stitched seam", [(.705*t,y,roof(t,y)+.0007) for t in xs], .001, fabric)
    top_tube("Soft top windshield header", [(-.704,-.275,1.784),(.704,-.275,1.784)], .009)
    return shell
