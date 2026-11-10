# -*- coding: utf-8 -*-
"""Donor city-map cars (the owner's .schem files) -> car_templates.bin (the CAR1 format).

  py -3 extract_cars.py --src <dir with car_*.schem> [--out ../../resources/templates]

Each car is an EXACT copy of the donor block grid (only the shape: the sign texts and the entities of the
donor files are not carried over), stored in four rotations (0/90/180/270 degrees clockwise, the facing and
connection properties of the states turned accordingly) and with a TNT block baked into the middle of the car.
The rotated core coordinates are stored next to the cells so the exporter can register the punch-trap check
without knowing the shape.

The CAR1 container follows the conventions of the other templates ("PGD2"): 4 magic bytes, then a
Qt-qUncompress payload (a big-endian uncompressed length + a zlib stream):
  u32 count                       the number of cars
  for each car:
    u16 len + name (utf8)         "sedan", "police", ...
    u16 sx, u16 sy, u16 sz        the native box (y up); rotation r%2 swaps sx and sz
    u16 palN; palN * (u16 len + "name|k=v|...")   the same state strings as in the PGD palettes
    u8 rotN (=4); rotN * {
      u32 n; n * (u8 x, u8 y, u8 z, u16 paletteIndex)   every non-air cell of the rotated car
      u8 cx, u8 cy, u8 cz                               the core cell (the TNT) of this rotation
    }
"""
import argparse, glob, gzip, io, os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
CW = {'north': 'east', 'east': 'south', 'south': 'west', 'west': 'north'}   # one step clockwise


# ---------------------------------------------------------------- the donor .schem reader (two container variants)

def rd_str(f):
    return f.read(struct.unpack('>H', f.read(2))[0]).decode('utf-8', 'replace')

def rd_val(f, t):
    if t == 1: return struct.unpack('>b', f.read(1))[0]
    if t == 2: return struct.unpack('>h', f.read(2))[0]
    if t == 3: return struct.unpack('>i', f.read(4))[0]
    if t == 4: return struct.unpack('>q', f.read(8))[0]
    if t == 5: return struct.unpack('>f', f.read(4))[0]
    if t == 6: return struct.unpack('>d', f.read(8))[0]
    if t == 7:
        n = struct.unpack('>i', f.read(4))[0]; return f.read(n)
    if t == 8: return rd_str(f)
    if t == 9:
        et = struct.unpack('>B', f.read(1))[0]; n = struct.unpack('>i', f.read(4))[0]
        if et == 0 and n: et = 10
        return [rd_val(f, et) for _ in range(n)]
    if t == 10:
        d = {}
        while True:
            tt = struct.unpack('>B', f.read(1))[0]
            if tt == 0: break
            nm = rd_str(f)                                     # careful: the name must be read BEFORE the value
            d[nm] = rd_val(f, tt)                              # (an assignment evaluates the right side first,
        return d                                               #  so d[rd_str(f)] = rd_val(f, tt) would misalign the stream)
    if t == 11:
        n = struct.unpack('>i', f.read(4))[0]; return list(struct.unpack('>%di' % n, f.read(4 * n)))
    if t == 12:
        n = struct.unpack('>i', f.read(4))[0]; return list(struct.unpack('>%dq' % n, f.read(8 * n)))
    raise ValueError('tag type %d' % t)

def load_schem(path):
    f = io.BytesIO(gzip.open(path).read())
    assert struct.unpack('>B', f.read(1))[0] == 10, path
    rd_str(f)                                                  # the root's name
    s = rd_val(f, 10)
    return s.get('Schematic', s)

def state_str(schem_state):
    """"name[a=1,b=2]" -> "name|a=1|b=2" (the keys sorted, like extract_ruins.state_str)."""
    if '[' not in schem_state:
        return schem_state
    nm, rest = schem_state[:-1].split('[', 1)
    props = dict(kv.split('=', 1) for kv in rest.split(','))
    return nm + ''.join('|%s=%s' % kv for kv in sorted(props.items()))

def schem_grid(s):
    """(sx, sy, sz), {(x,y,z): 'name|k=v|...'}."""
    if 'Width' not in s:
        raise ValueError('unsupported .schem layout')
    sx, sy, sz = s['Width'], s['Height'], s['Length']
    blk = s['Blocks'] if isinstance(s.get('Blocks'), dict) else {}
    pal = blk.get('Palette') or s.get('Palette')
    if blk.get('Data'):                                        # direct palette indices (classic .schem)
        seq = blk['Data']
    elif len(s.get('BlockData') or []) == sx * sy * sz:        # byte palette: one palette index per cell (a byte)
        seq = list(s['BlockData'])                             #   these writers keep it uncompressed when palN < 256
    else:                                                      # bit-packed BlockData (sponge v2)
        bits = max(1, (len(pal) - 1).bit_length())
        seq, acc, nacc, total = [], 0, 0, sx * sy * sz
        for byte in s['BlockData']:
            acc = (acc << 8) | byte; nacc += 8
            while nacc >= bits and len(seq) < total:
                nacc -= bits
                seq.append((acc >> nacc) & ((1 << bits) - 1))
                acc &= (1 << nacc) - 1
    inv = {v: k for k, v in pal.items()}
    out = {}
    for i, p in enumerate(seq):
        if p == 0: continue
        nm = inv.get(p)
        if nm and nm != 'minecraft:air':
            out[(i % sx, i // (sx * sz), (i // sx) % sz)] = state_str(nm)
    return (sx, sy, sz), out


# ---------------------------------------------------------------- the rotation

def rotate_state(state):
    """One quarter clockwise: facing and connections turn, the x/z axis swaps, the numeric rotation turns.
    The slab halves and the stair corners keep their value (both are read against the turned facing)."""
    parts = state.split('|')
    out = {}
    for kv in parts[1:]:
        k, v = kv.split('=', 1)
        if k in CW:                                            # a connection (pane, wall, bars...)
            out[CW[k]] = v
        elif k == 'facing' and v in CW:
            out[k] = CW[v]
        elif k == 'axis':
            out[k] = {'x': 'z', 'z': 'x', 'y': 'y'}[v]
        elif k == 'rotation':
            out[k] = str((int(v) + 4) % 16)
        else:
            out[k] = v
    return parts[0] + ''.join('|%s=%s' % kv for kv in sorted(out.items()))

def rotate_once(sx, sy, sz, cells, core):
    """(x,z) -> (sz-1-z, x) - a quarter clockwise."""
    return (sz, sy, sx), {(sz - 1 - z, y, x): rotate_state(st) for (x, y, z), st in cells.items()}, \
           (sz - 1 - core[2], core[1], core[0])


# ---------------------------------------------------------------- the car

def add_tnt_core(sx, sy, sz, cells):
    """The middle column: the TNT stands on the lowest block of the middle when the middle is hollow (the cabin
    or the bed) and replaces the block above the floor when the middle is packed solid (a van box) - the core of
    the punch trap, as in the donor city maps it is not seen from outside anyway. An open-frame scheme (a bike,
    a digger) has an EMPTY middle column, then the nearest occupied column takes it - an air cell would leave
    the TNT block visible from the side."""
    cx, cz = sx // 2, sz // 2
    for rad in range(0, max(sx, sz)):                          # Chebyshev rings around the middle
        cand = None
        for dz in (-rad, 0, rad) if rad else (0,):
            for dx in (-rad, 0, rad) if rad else (0,):
                x, z = cx + dx, cz + dz
                if not (0 <= x < sx and 0 <= z < sz):
                    continue
                col = sorted(y for (a, y, b) in cells if a == x and b == z)
                if col:
                    cand = (x, z, col)
                    break
            if cand:
                break
        if cand:
            cx, cz, col = cand
            ty = (col[0] + 1) if col[0] + 1 < sy else col[0]
            break
    else:
        col = []
        ty = 0
    cells[(cx, ty, cz)] = 'minecraft:tnt'
    return (cx, ty, cz)

# the parking-lot fit: the exporter rejects a car wider than 7 cells across the spot and one longer than
# the spot's 12 cells along it (exportrun.cpp, trySlot), so the pick below keeps only the road-vehicle shapes
# whose min(sx, sz) <= 7 and max(sx, sz) <= 12. Neutral template names: the container must not carry brand marks.
PICKS = [
    ('CivilianBike', 'bike'),          ('Bike02', 'bike2'),        ('Bike03', 'bike3'),
    ('Bike04', 'bike4'),               ('PoliceBike', 'police_bike'), ('GoKart', 'gokart'),
    ('ATV', 'atv'),                    ('GolfCar', 'cart'),
    ('BlackCivilian', 'sedan_black'),  ('BlueCivilian', 'sedan_blue'), ('RedCivilian', 'sedan_red'),
    ('WhiteCivilian', 'sedan_white'),  ('GreenCivilian', 'sedan_green'), ('YellowCivilian', 'sedan_yellow'),
    ('GrayCivilian', 'sedan_gray'),    ('StripedCivilian', 'sedan_stripe'),
    ('MuscleCar', 'muscle'),           ('SportsCar', 'sports'),    ('ExpensiveCar', 'luxury'),
    ('VintageCar', 'vintage'),         ('ClassicCar', 'classic'),  ('ClassicCar02', 'classic2'),
    ('HotRod', 'hotrod2'),             ('Taxi', 'taxi'),           ('PoliceCar', 'police2'),
    ('F1', 'racer'),                   ('YellowF1', 'racer2'),
    ('WhitePickup', 'pickup_white'),   ('RedPickup', 'pickup_red'),
    ('WhiteVan', 'van_white'),         ('BlackVan', 'van_black'),
    ('CivilianJeep', 'jeep'),          ('ArmyJeep', 'jeep2'),
    ('Ambulance', 'ambulance'),        ('SmallTruck', 'truck_small'), ('MediumTruck', 'truck_medium'),
    ('MiniCamper', 'camper_mini'),     ('GarbageTruck', 'garbage'),    ('Firetruck', 'firetruck'),
    ('IceCreamTruck', 'icecream'),     ('TacoTruck', 'food_truck'),  ('SWATTruck', 'swat'),
    ('PoliceVan', 'police_van'),       ('ContainerTruck', 'container'), ('DumpTruck', 'dumper'),
    ('Tractor', 'tractor'),            ('Tractor02', 'tractor2'),  ('Bulldozer', 'dozer'),
    ('SmallDigger', 'digger'),
]

RENAME = {'fedex': 'courier', 'uhaul': 'rental'}        # the first donor folder's brand-marked names


def convert(path, name=None):
    (sx, sy, sz), cells = schem_grid(load_schem(path))
    if name is None:
        name = os.path.basename(path)[len('car_'):-len('.schem')]
        name = RENAME.get(name, name)
    assert min(sx, sz) <= 7 and max(sx, sz) <= 12, (name, sx, sy, sz)   # must fit a parking spot
    core = add_tnt_core(sx, sy, sz, cells)
    pal, palIdx, rots = [], {}, []
    b, c, cr = (sx, sy, sz), cells, core
    for r in range(4):
        cl = sorted(((x, y, z, st) for (x, y, z), st in c.items()), key=lambda t: (t[1], t[2], t[0]))
        enc = []
        for (x, y, z, st) in cl:
            if st not in palIdx:
                palIdx[st] = len(pal); pal.append(st)
            assert x < 256 and y < 256 and z < 256 and palIdx[st] < 65536
            enc.append((x, y, z, palIdx[st]))
        rots.append((enc, cr))
        if r < 3:
            b, c, cr = rotate_once(b[0], b[1], b[2], c, cr)
    return name, (sx, sy, sz), pal, rots

def write_car1(fn, cars):
    raw = struct.pack('>I', len(cars))
    for name, (sx, sy, sz), pal, rots in cars:
        nb = name.encode('utf-8'); raw += struct.pack('>H', len(nb)) + nb
        raw += struct.pack('>HHH', sx, sy, sz)
        raw += struct.pack('>H', len(pal))
        for st in pal:
            b = st.encode('utf-8'); raw += struct.pack('>H', len(b)) + b
        raw += struct.pack('>B', len(rots))
        for enc, cr in rots:
            raw += struct.pack('>I', len(enc))
            for (x, y, z, p) in enc: raw += struct.pack('>BBBH', x, y, z, p)
            raw += struct.pack('>BBB', cr[0], cr[1], cr[2])
    blob = b'CAR1' + struct.pack('>I', len(raw)) + zlib.compress(raw)
    open(fn, 'wb').write(blob)
    return len(blob)

def read_car1(fn):                                       # the self check of the produced file
    blob = open(fn, 'rb').read()
    assert blob[:4] == b'CAR1'
    raw = zlib.decompress(blob[4 + 4:])
    o = 0
    def rd(fmt):
        nonlocal o
        v = struct.unpack(fmt, raw[o:o + struct.calcsize(fmt)]); o += struct.calcsize(fmt); return v
    cnt = rd('>I')[0]
    cars = []
    for _ in range(cnt):
        l = rd('>H')[0]; name = raw[o:o + l].decode('utf-8'); o += l
        sx, sy, sz = rd('>HHH')
        pn = rd('>H')[0]
        pal = []
        for _i in range(pn):
            l = rd('>H')[0]; pal.append(raw[o:o + l].decode('utf-8')); o += l
        rn = rd('>B')[0]
        rots = []
        for _r in range(rn):
            n = rd('>I')[0]
            cells = [rd('>BBBH') for _c in range(n)]
            cr = rd('>BBB')
            rots.append((cells, cr))
        cars.append((name, (sx, sy, sz), pal, rots))
    assert o == len(raw), (o, len(raw))
    return cars


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True, help='the folder with the donor car_*.schem')
    ap.add_argument('--pack', help='a second donor folder, the PICKS list picks the schemes from it')
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'resources', 'templates'))
    a = ap.parse_args()
    files = [(p, None) for p in sorted(glob.glob(os.path.join(a.src, 'car_*.schem')))]
    if not files:
        print('no car_*.schem in', a.src)
        return 1
    if a.pack:
        files += [(os.path.join(a.pack, base + '.schem'), name) for (base, name) in PICKS]
    names = [n if n is not None else RENAME.get(os.path.basename(p)[4:-6], os.path.basename(p)[4:-6])
             for (p, n) in files]
    assert len(set(names)) == len(files), 'template name clash'
    os.makedirs(a.out, exist_ok=True)
    cars = []
    for (p, nm) in files:
        name, (sx, sy, sz), pal, rots = convert(p, nm)
        cars.append((name, (sx, sy, sz), pal, rots))
        print('  %-12s %dx%dx%d, palette %d, cells %d, core %s'
              % (name, sx, sy, sz, len(pal), len(rots[0][0]), rots[0][1]))
    fn = os.path.join(a.out, 'car_templates.bin')
    n = write_car1(fn, cars)
    back = read_car1(fn)
    assert len(back) == len(cars)
    for (nm, dims, pal, rots), (nm2, dims2, pal2, rots2) in zip(cars, back):
        assert nm == nm2 and dims == dims2 and pal == pal2 and len(rots) == len(rots2)
        for (c1, k1), (c2, k2) in zip(rots, rots2):
            assert c1 == [tuple(t) for t in c2] and k1 == k2
    print('%d cars -> %s (%d bytes, self check OK)' % (len(cars), fn, n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
