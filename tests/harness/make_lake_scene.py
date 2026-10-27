# -*- coding: utf-8 -*-
# A synthetic scene for mc_scene_run.exe: a big lake (an ellipse) + a pond + two houses on the shore. A check of the water depth, seaweed and ships.
# Usage: py -3 make_lake_scene.py scene.bin [radius_X=300] [radius_Z=220]
import math, struct, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else 'lake_scene.bin'
RX = float(sys.argv[2]) if len(sys.argv) > 2 else 300.0
RZ = float(sys.argv[3]) if len(sys.argv) > 3 else 220.0
K = dict(Building=1, Vegetation=2, Plate=5, Water=6)


class Obj:
    def __init__(self, kind):
        self.kind = kind; self.code = 0; self.closed = 0; self.linear = 0; self.lift = 0; self.intra = 0
        self.extrude = 0.0; self.roadScale = 1.0
        self.material = ''; self.street = ''; self.streetType = ''; self.number = ''
        self.color = (74, 134, 232)
        self.v = []; self.i = []; self.paths = []


def ellipse(cx, cz, rx, rz, n=96):
    pts = [(cx + rx * math.cos(2 * math.pi * k / n) * (1 + 0.06 * math.sin(3 * k)), cz + rz * math.sin(2 * math.pi * k / n) * (1 + 0.05 * math.cos(5 * k))) for k in range(n)]
    o = Obj(K['Water'])
    o.v = [(x, z, 100.0) for x, z in pts] + [(cx, cz, 100.0)]
    for k in range(n):
        o.i += [n, k, (k + 1) % n]
    return o


def box(x0, z0, x1, z1, h=9.0, code=44400000):
    o = Obj(K['Building']); o.closed = 1; o.code = code; o.color = (180, 180, 180); o.extrude = h
    base = [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]
    o.v = [(x, z, 100.0) for x, z in base] + [(x, z, 100.0 + h) for x, z in base]
    o.i = [0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6]
    for a in range(4):
        b = (a + 1) % 4
        o.i += [a, b, 4 + b, a, 4 + b, 4 + a]
    return o


def veg(x0, z0, x1, z1, code, h):
    o = Obj(K['Vegetation']); o.code = code; o.extrude = h; o.color = (115, 166, 89)
    o.v = [(x0, z0, 100.0), (x1, z0, 100.0), (x1, z1, 100.0), (x0, z1, 100.0)]
    o.i = [0, 1, 2, 0, 2, 3]
    return o


def plate(x0, z0, x1, z1, code):
    o = Obj(K['Plate']); o.code = code; o.color = (246, 255, 169)
    o.v = [(x0, z0, 100.0), (x1, z0, 100.0), (x1, z1, 100.0), (x0, z1, 100.0)]
    o.i = [0, 1, 2, 0, 2, 3]
    return o


import os
def poly_plate(pts, code, dx=0.0, dz=0.0):
    cx = sum(p[0] for p in pts) / len(pts); cz = sum(p[1] for p in pts) / len(pts)
    o = Obj(K['Plate']); o.code = code; o.color = (246, 255, 169)
    o.v = [(x + dx, z + dz, 100.0) for x, z in pts] + [(cx + dx, cz + dz, 100.0)]
    n = len(pts)
    for k in range(n):
        o.i += [n, k, (k + 1) % n]
    return o


def lonlat(ring, lon0=38.4218, lat0=55.7692):
    import math
    return [((lo - lon0) * 111320.0 * math.cos(math.radians(lat0)), (la - lat0) * 110574.0) for lo, la in ring]


if os.environ.get('SCENE') == 'sports':
    r1 = [(38.4218815564211, 55.7689399948774), (38.4217635510383, 55.7689830134412), (38.4216834464233, 55.7690490318145), (38.4214804121007, 55.7695840970316), (38.4214524491508, 55.769661143082), (38.4214784735472, 55.7697401511839), (38.4215554244066, 55.7698061024896), (38.4216723795296, 55.7698511286946), (38.4218104025862, 55.76986714397), (38.4219494143961, 55.7698521667205), (38.4220674220967, 55.7698091479681), (38.4221463447157, 55.7697431237363), (38.4221724544632, 55.7696580724435), (38.4223554063741, 55.769191104784), (38.4223785290599, 55.7691300283888), (38.4223514359648, 55.7690520936253), (38.4222746181172, 55.7689860536353), (38.4221575335453, 55.7689410274261), (38.42201951363, 55.7689250125739)]
    r2 = [(38.4284574104678, 55.7689844478096), (38.4285723467242, 55.7687316626541), (38.4282967510546, 55.7686918404124), (38.4281818147982, 55.7689446258271)]
    objs = [poly_plate(lonlat(r1), 53700000, -20, 0), poly_plate(lonlat(r2, 38.4284, 55.7689), 53700000, 60, 0),
            plate(100, -30, 190, 30, 53700000), plate(100, 50, 140, 72, 53700000), plate(0, 70, 22, 82, 53700000), plate(40, 70, 70, 86, 53700000), box(-90, -90, -80, -80, 6.0)]
elif os.environ.get('SCENE') == 'csr2':
    objs = [plate(0, 0, 80, 60, 51111000), plate(100, 0, 170, 60, 51123000), plate(0, 80, 60, 104, 71323000), box(-30, -30, -20, -20, 6.0)]
elif os.environ.get('SCENE') == 'stairs':
    objs = [box(0, 0, 40, 18, 9.0, 44310000), box(40, 0, 80, 18, 13.5, 44310000), box(80, 0, 120, 18, 9.0, 44310000)]
elif os.environ.get('SCENE') == 'csr':
    codes = [53510000, 51220000, 53530000, 52400000, 51460000, 52100000, 51130000, 53110000, 53600000, 53400000]
    objs = [plate(-60 + 60 * (i % 5), -60 + 60 * (i // 5), -60 + 60 * (i % 5) + (50 if i != 7 else 56), -60 + 60 * (i // 5) + (40 if i != 7 else 20), c) for i, c in enumerate(codes)] + [box(-90, -90, -80, -80, 6.0)]
elif os.environ.get('SCENE') == 'meadow':
    big = float(os.environ.get('MEADOW', '12000'))
    side = int(big ** 0.5)
    objs = [veg(0, 0, 55, 55, 71111110, 6.0), veg(100, 0, 100 + side, side, 71314000, 0.0), box(-40, -40, -25, -28, 9.0)]
elif os.environ.get('SCENE') == 'resid':
    objs = [box(0, 0, 30, 18, 24.0), box(50, 0, 64, 26, 18.0), box(0, 40, 22, 56, 15.0)]
else:
    objs = [ellipse(0, 0, RX, RZ), ellipse(RX + 140, 0, 50, 40), box(RX + 40, 100, RX + 60, 120), box(-RX - 60, -80, -RX - 40, -60)]


def s(b):
    bb = b.encode('utf-8')
    return struct.pack('<H', len(bb)) + bb


with open(OUT, 'wb') as f:
    f.write(struct.pack('<IId', 0x53434E31, len(objs), 1.0))
    for o in objs:
        f.write(struct.pack('<BiBBBBfd', o.kind, o.code, o.closed, o.linear, o.lift, o.intra, o.extrude, o.roadScale))
        f.write(struct.pack('<BBB', *o.color))
        f.write(s(o.material) + s(o.street) + s(o.streetType) + s(o.number))
        f.write(struct.pack('<I', len(o.v)))
        for v in o.v: f.write(struct.pack('<fff', *v))
        f.write(struct.pack('<I', len(o.i)))
        f.write(struct.pack('<%dI' % len(o.i), *o.i))
        f.write(struct.pack('<I', len(o.paths)))
print('written', OUT, len(objs), 'objects')
