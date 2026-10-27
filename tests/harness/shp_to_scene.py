# -*- coding: utf-8 -*-
# A Minecraft export scene from the project shape files (.tqt) without the 3D window: an approximate replacement of the export from the tqt 3D window.
# Each layer -> McObject (kind/code/REL/MAT/LIFT/address/road strips), the terrain is flat (z0 = 100). The output is a binary file for mc_scene_run.exe.
# Usage: py -3 shp_to_scene.py scene.bin shape_directory [second_directory]
import math, struct, sys
from osgeo import ogr
ogr.UseExceptions()

OUT = sys.argv[1] if len(sys.argv) > 1 else 'scene.bin'
if len(sys.argv) < 3: sys.exit('Usage: shp_to_scene.py scene.bin shape_directory [second_directory]')
D2 = sys.argv[2]
D1 = sys.argv[3] if len(sys.argv) > 3 else D2
import os
PFX = os.environ.get('SHP_PFX', 'Moskovskaya_r50r77.')
BOX = tuple(float(v) for v in os.environ['SHP_BOX'].split(',')) if 'SHP_BOX' in os.environ else (4277319.4282452, 7479219.46085933, 4280534.35860021, 7481032.58085066)    # SpatialFilter from the .tqt
CX, CY = (BOX[0] + BOX[2]) / 2, (BOX[1] + BOX[3]) / 2
U2M = math.cos(math.atan(math.sinh(CY / 6378137.0)))
Z0 = 100.0
K = dict(Generic=0, Building=1, Vegetation=2, Road=3, Railway=4, Plate=5, Water=6, ParkingLot=7, BlockCover=8)


def area2(r):
    return sum(r[i][0] * r[(i + 1) % len(r)][1] - r[(i + 1) % len(r)][0] * r[i][1] for i in range(len(r)))


def ear_clip(pts):
    """a simple polygon without holes -> a list of index triples"""
    n = len(pts)
    idx = list(range(n))
    if area2(pts) < 0: idx.reverse()
    tris = []
    def cross(a, b, c): return (pts[b][0] - pts[a][0]) * (pts[c][1] - pts[a][1]) - (pts[b][1] - pts[a][1]) * (pts[c][0] - pts[a][0])
    def inside(p, a, b, c):
        return cross(a, b, p) >= 0 and cross(b, c, p) >= 0 and cross(c, a, p) >= 0
    guard = 0
    while len(idx) > 3 and guard < 100000:
        guard += 1
        m = len(idx); clipped = False
        for i in range(m):
            a, b, c = idx[(i - 1) % m], idx[i], idx[(i + 1) % m]
            if cross(a, b, c) <= 1e-12: continue
            ok = True
            for j in idx:
                if j in (a, b, c): continue
                if inside(j, a, b, c): ok = False; break
            if ok:
                tris.append((a, b, c)); idx.pop(i); clipped = True; break
        if not clipped:
            tris.append((idx[0], idx[1], idx[2])); idx.pop(1)
    if len(idx) == 3: tris.append(tuple(idx))
    return tris


def triangulate(poly):
    """ogr Polygon -> (points [(x,y)], triples). Simple outlines — ear clipping, complex ones/with holes — Delaunay + a filter by the centroid."""
    rings = []
    for i in range(poly.GetGeometryCount()):
        r = poly.GetGeometryRef(i)
        pts = [(r.GetX(k) - CX, r.GetY(k) - CY) for k in range(r.GetPointCount())]
        if len(pts) > 1 and pts[0] == pts[-1]: pts.pop()
        rings.append(pts)
    if not rings or len(rings[0]) < 3: return [], []
    if len(rings) == 1 and len(rings[0]) <= 140:
        return rings[0], ear_clip(rings[0])
    mp = ogr.Geometry(ogr.wkbMultiPoint)
    allp = []
    for r in rings:
        for p in r:
            allp.append(p)
            pt = ogr.Geometry(ogr.wkbPoint); pt.AddPoint_2D(p[0], p[1]); mp.AddGeometry(pt)
    shifted = ogr.Geometry(ogr.wkbPolygon)
    for r in rings:
        lr = ogr.Geometry(ogr.wkbLinearRing)
        for p in r: lr.AddPoint_2D(p[0], p[1])
        lr.AddPoint_2D(r[0][0], r[0][1])
        shifted.AddGeometry(lr)
    dt = mp.DelaunayTriangulation(0, False)
    key = {p: i for i, p in enumerate(allp)}
    tris = []
    if dt is None: return allp, tris
    for t in range(dt.GetGeometryCount()):
        tg = dt.GetGeometryRef(t); rr = tg.GetGeometryRef(0)
        v = [(rr.GetX(k), rr.GetY(k)) for k in range(3)]
        if any(q not in key for q in v): continue
        c = ogr.Geometry(ogr.wkbPoint); c.AddPoint_2D(sum(q[0] for q in v) / 3, sum(q[1] for q in v) / 3)
        if shifted.Contains(c): tris.append((key[v[0]], key[v[1]], key[v[2]]))
    return allp, tris


class Obj:
    def __init__(self, kind):
        self.kind = kind; self.code = 0; self.closed = 0; self.linear = 0; self.lift = 0
        self.extrude = 0.0; self.roadScale = 1.0
        self.material = ''; self.street = ''; self.streetType = ''; self.number = ''
        self.color = (140, 153, 166)
        self.v = []; self.i = []; self.paths = []


def num(ft, name, d=0.0):
    try:
        v = ft.GetField(name)
        return float(v) if v not in (None, '') else d
    except Exception:
        return d


def txt(ft, name):
    try:
        v = ft.GetField(name)
        if v is None: return ''
        v = str(v).strip()
        try:
            f = float(v)
            if f == int(f): v = str(int(f))
        except ValueError:
            pass
        return '' if v == '0' else v
    except Exception:
        return ''


def polys(g):
    t = ogr.GT_Flatten(g.GetGeometryType())
    if t == ogr.wkbPolygon: return [g]
    if t == ogr.wkbMultiPolygon: return [g.GetGeometryRef(i) for i in range(g.GetGeometryCount())]
    return []


def lines(g):
    t = ogr.GT_Flatten(g.GetGeometryType())
    if t == ogr.wkbLineString: return [g]
    if t == ogr.wkbMultiLineString: return [g.GetGeometryRef(i) for i in range(g.GetGeometryCount())]
    return []


def add_poly(o, poly, top):
    pts, tris = triangulate(poly)
    if not pts or not tris: return False
    n = len(pts)
    o.v = [(p[0], p[1], Z0) for p in pts] + ([(p[0], p[1], top) for p in pts] if top is not None else [])
    for a, b, c in tris:
        o.i += [a, b, c]
        if top is not None: o.i += [n + a, n + c, n + b]
    if top is not None:
        # walls: by the outer ring
        ext = poly.GetGeometryRef(0)
        m = ext.GetPointCount() - 1
        ring = [(ext.GetX(k) - CX, ext.GetY(k) - CY) for k in range(m)]
        base = len(o.v)
        for k in range(m):
            a = ring[k]; b = ring[(k + 1) % m]
            o.v += [(a[0], a[1], Z0), (b[0], b[1], Z0), (b[0], b[1], top), (a[0], a[1], top)]
            o.i += [base, base + 1, base + 2, base, base + 2, base + 3]
            base += 4
    return True


def ribbon(o, ln, width):
    pts = [(ln.GetX(k) - CX, ln.GetY(k) - CY) for k in range(ln.GetPointCount())]
    o.paths.append(pts)
    for k in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[k], pts[k + 1]
        dx, dy = x1 - x0, y1 - y0
        L = math.hypot(dx, dy)
        if L < 1e-6: continue
        nx, ny = -dy / L * width / 2, dx / L * width / 2
        b = len(o.v)
        o.v += [(x0 + nx, y0 + ny, Z0), (x1 + nx, y1 + ny, Z0), (x1 - nx, y1 - ny, Z0), (x0 - nx, y0 - ny, Z0)]
        o.i += [b, b + 1, b + 2, b, b + 2, b + 3]


objs = []
_keep = []
def layer(path):
    try:
        ds = ogr.Open(path); _keep.append(ds)
        return ds.GetLayer(0)
    except Exception as e:
        print('no layer', path, e); return None

cnt = {}
# --- bldpol
L = layer(D2 + '/' + PFX + 'bldpol.shp')
for ft in L or []:
    g = ft.GetGeometryRef()
    if g is None: continue
    rel = num(ft, 'REL', 0.0)
    for p in polys(g):
        o = Obj(K['Building']); o.closed = 1
        o.code = int(num(ft, 'CODE', 0)); o.material = txt(ft, 'MAT'); o.lift = 1 if int(num(ft, 'LIFT', 0)) == 1 else 0
        o.streetType = txt(ft, 'TYSTR'); o.street = txt(ft, 'STREET'); o.number = txt(ft, 'NUMBR') + txt(ft, 'ADDNUM')
        h = rel if rel > 0 else 3.0
        o.extrude = h
        if add_poly(o, p, Z0 + h): objs.append(o); cnt['bldpol'] = cnt.get('bldpol', 0) + 1
# --- vegpol, vgbpol
for nm, intra in (('vegpol', 0), ('vgbpol', 1)):
    L = layer(D2 + '/' + PFX + nm + '.shp')
    for ft in L or []:
        g = ft.GetGeometryRef()
        if g is None: continue
        for p in polys(g):
            o = Obj(K['Vegetation']); o.code = int(num(ft, 'CODE', 0)); o.extrude = num(ft, 'REL', 0.0); o.color = (115, 166, 89)
            o.intra = intra
            if add_poly(o, p, None): objs.append(o); cnt[nm] = cnt.get(nm, 0) + 1
# --- hdrpol
L = layer(D2 + '/' + PFX + 'hdrpol.shp')
for ft in L or []:
    g = ft.GetGeometryRef()
    if g is None: continue
    for p in polys(g):
        o = Obj(K['Water']); o.code = int(num(ft, 'CODE', 0)); o.color = (74, 134, 232)
        if add_poly(o, p, None): objs.append(o); cnt['hdrpol'] = cnt.get('hdrpol', 0) + 1
# --- rdslin
L = layer(D2 + '/' + PFX + 'rdslin.shp')
for ft in L or []:
    g = ft.GetGeometryRef()
    if g is None: continue
    code = int(num(ft, 'CODE', 0))
    sc = 2.0 if code in (45220000, 45230000, 45240000, 61231000, 61232000) else 2.5 if code in (45210000, 61210000, 61220000, 61230000) else 1.0
    o = Obj(K['Road']); o.code = code; o.roadScale = sc; o.color = (255, 255, 255)
    o.streetType = txt(ft, 'TYSTR'); o.street = txt(ft, 'STREET')
    for ln in lines(g):
        if ln.GetPointCount() >= 2: ribbon(o, ln, 4.5 * sc)
    if o.v: objs.append(o); cnt['rdslin'] = cnt.get('rdslin', 0) + 1
# --- csrpol
L = layer(D2 + '/' + PFX + 'csrpol.shp')
for ft in L or []:
    g = ft.GetGeometryRef()
    if g is None: continue
    rel = num(ft, 'REL', 0.0)
    for p in polys(g):
        o = Obj(K['Plate']); o.code = int(num(ft, 'CODE', 0)); o.material = txt(ft, 'MAT'); o.color = (246, 255, 169)
        if rel > 0: o.closed = 1; o.extrude = rel
        if add_poly(o, p, (Z0 + rel) if rel > 0 else None): objs.append(o); cnt['csrpol'] = cnt.get('csrpol', 0) + 1
# --- blkpol, rdcpol (the directory stal_kult)
L = layer(D1 + '/' + PFX + 'blkpol.shp')
for ft in L or []:
    g = ft.GetGeometryRef()
    code = int(num(ft, 'CODE', 0))
    if g is None or code not in (62231000, 45300000): continue
    for p in polys(g):
        o = Obj(K['BlockCover']); o.code = code; o.color = (94, 102, 111)
        if add_poly(o, p, None): objs.append(o); cnt['blkpol'] = cnt.get('blkpol', 0) + 1
L = layer(D1 + '/' + PFX + 'rdcpol.shp')
for ft in L or []:
    g = ft.GetGeometryRef()
    if g is None: continue
    for p in polys(g):
        o = Obj(K['ParkingLot']); o.code = int(num(ft, 'CODE', 0)); o.color = (94, 102, 111)
        ext = p.GetGeometryRef(0)
        o.paths.append([(ext.GetX(k) - CX, ext.GetY(k) - CY) for k in range(ext.GetPointCount())])
        if add_poly(o, p, None): objs.append(o); cnt['rdcpol'] = cnt.get('rdcpol', 0) + 1
print('objects', len(objs), cnt, 'unitsToMeters %.4f' % U2M)

def s(b): bb = b.encode('utf-8'); return struct.pack('<H', len(bb)) + bb
with open(OUT, 'wb') as f:
    f.write(struct.pack('<IId', 0x53434E31, len(objs), U2M))
    for o in objs:
        f.write(struct.pack('<BiBBBBfd', o.kind, o.code, o.closed, o.linear, o.lift, getattr(o, 'intra', 0), o.extrude, o.roadScale))
        f.write(struct.pack('<BBB', *o.color))
        f.write(s(o.material) + s(o.street) + s(o.streetType) + s(o.number))
        f.write(struct.pack('<I', len(o.v)))
        for v in o.v: f.write(struct.pack('<fff', *v))
        f.write(struct.pack('<I', len(o.i)))
        f.write(struct.pack('<%dI' % len(o.i), *o.i))
        f.write(struct.pack('<I', len(o.paths)))
        for pa in o.paths:
            f.write(struct.pack('<I', len(pa)))
            for p in pa: f.write(struct.pack('<dd', *p))
print('written', OUT)
