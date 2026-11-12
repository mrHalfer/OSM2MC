# -*- coding: utf-8 -*-
"""Playground structures (the owner's playground*.schem) -> playground_templates.bin (the PGP1 format).

  py -3 extract_playgrounds.py --src <dir with playground_*.schem> [--out ../../resources/templates]

Each structure is an EXACT copy of the donor block grid (the sign texts and the entities are not carried over),
stored in four rotations (0/90/180/270 degrees clockwise, the facing and connection properties of the states
turned accordingly). The grid is taken with its y=0 layer as the layer ABOVE the ground: the exporter puts it on
the pad's surface, so the grass tufts and the bases of the fences/logs stand on the ground, not inside it.

The PGP1 container follows the conventions of the other templates ("PGD2"/"CAR1"): 4 magic bytes, then a
Qt-qUncompress payload (a big-endian uncompressed length + a zlib stream):
  u32 count                       the number of structures
  for each structure:
    u16 len + name (utf8)         "play_tower", "log_fort", ...
    u16 sx, u16 sy, u16 sz        the native box (y up); rotation r%2 swaps sx and sz
    u16 palN; palN * (u16 len + "name|k=v|...")   the same state strings as in the PGD palettes
    u8 rotN (=4); rotN * {
      u32 n; n * (u8 x, u8 y, u8 z, u16 paletteIndex)   every non-air cell of the rotated structure
    }
"""
import argparse, glob, os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import extract_cars as ec                                  # the .schem reader and the rotation are shared with the cars

# Neutral template names: the container must not carry the donor's names.
NAMES = {'playground1': 'play_tower', 'playground2': 'log_fort', 'playground3': 'tower_bridge'}


def convert(path, name):
    (sx, sy, sz), cells = ec.schem_grid(ec.load_schem(path))
    if not cells:
        raise ValueError('empty structure: %s' % path)
    pal, palIdx, rots = [], {}, []
    b, c = (sx, sy, sz), cells
    for r in range(4):
        cl = sorted(((x, y, z, st) for (x, y, z), st in c.items()), key=lambda t: (t[1], t[2], t[0]))
        enc = []
        for (x, y, z, st) in cl:
            if st not in palIdx:
                palIdx[st] = len(pal); pal.append(st)
            assert x < 256 and y < 256 and z < 256 and palIdx[st] < 65536
            enc.append((x, y, z, palIdx[st]))
        rots.append(enc)
        if r < 3:
            b, c, _ = ec.rotate_once(b[0], b[1], b[2], c, (0, 0, 0))
    return name, (sx, sy, sz), pal, rots


def write_pgp1(fn, items):
    raw = struct.pack('>I', len(items))
    for name, (sx, sy, sz), pal, rots in items:
        nb = name.encode('utf-8'); raw += struct.pack('>H', len(nb)) + nb
        raw += struct.pack('>HHH', sx, sy, sz)
        raw += struct.pack('>H', len(pal))
        for st in pal:
            b = st.encode('utf-8'); raw += struct.pack('>H', len(b)) + b
        raw += struct.pack('>B', len(rots))
        for enc in rots:
            raw += struct.pack('>I', len(enc))
            for (x, y, z, p) in enc: raw += struct.pack('>BBBH', x, y, z, p)
    blob = b'PGP1' + struct.pack('>I', len(raw)) + zlib.compress(raw)
    open(fn, 'wb').write(blob)
    return len(blob)


def read_pgp1(fn):                                        # the self check of the produced file
    blob = open(fn, 'rb').read()
    assert blob[:4] == b'PGP1'
    raw = zlib.decompress(blob[4 + 4:])
    o = 0
    def rd(fmt):
        nonlocal o
        v = struct.unpack(fmt, raw[o:o + struct.calcsize(fmt)]); o += struct.calcsize(fmt); return v
    cnt = rd('>I')[0]
    items = []
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
            rots.append([rd('>BBBH') for _c in range(n)])
        items.append((name, (sx, sy, sz), pal, rots))
    assert o == len(raw), (o, len(raw))
    return items


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True, help='the folder with the playground_*.schem files')
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'resources', 'templates'))
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.src, 'playground*.schem')))
    if not files:
        print('no playground*.schem in', a.src)
        return 1
    items = []
    for p in files:
        base = os.path.basename(p)[:-len('.schem')]
        name = NAMES.get(base, base)
        nm, dims, pal, rots = convert(p, name)
        items.append((nm, dims, pal, rots))
        print('  %-12s %dx%dx%d, palette %d, cells %d'
              % (nm, dims[0], dims[1], dims[2], len(pal), len(rots[0])))
    assert len(set(nm for nm, _, _, _ in items)) == len(items), 'template name clash'
    os.makedirs(a.out, exist_ok=True)
    fn = os.path.join(a.out, 'playground_templates.bin')
    n = write_pgp1(fn, items)
    back = read_pgp1(fn)
    assert len(back) == len(items)
    for (nm, dims, pal, rots), (nm2, dims2, pal2, rots2) in zip(items, back):
        assert nm == nm2 and dims == dims2 and pal == pal2 and len(rots) == len(rots2)
        for c1, c2 in zip(rots, rots2):
            assert c1 == [tuple(t) for t in c2]
    print('%d structures -> %s (%d bytes, self check OK)' % (len(items), fn, n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
