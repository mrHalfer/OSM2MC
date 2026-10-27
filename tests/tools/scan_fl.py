# "Hanging" roofs: oak_stairs with air under them (or no section). A flat archive.
# Run: py -3 scan_fl.py <zip>
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

path = sys.argv[1]
f = zipfile.ZipFile(path)

def unpack(pal, data):
    """section -> 4096 names (list)"""
    names = [e[0] for e in pal]
    if data is None:
        return [names[0]] * 4096
    bits = max(4, (len(names)-1).bit_length()); per = 64 // bits; mask = (1 << bits)-1
    out = [None] * 4096
    for i in range(4096):
        v = (data[i // per] >> ((i % per) * bits)) & mask
        out[i] = names[v] if v < len(names) else 'minecraft:air'
    return out

floats = []
for cx, cz, ch in chunk_iter(f):
    secs = {}
    has_oak = False
    for s in ch['sections']:
        bs = s.get('block_states', {})
        pal = []
        for e in bs.get('palette') or []:
            if isinstance(e, dict):
                pal.append((e.get('Name') or e.get('id') or 'minecraft:air', e.get('Properties')))
            else:
                pal.append((e, None))
        if any('oak_stairs' in p[0] for p in pal): has_oak = True
        secs[s['Y']] = pal if bs.get('data') is None else (pal, bs.get('data'))
    if not has_oak: continue
    grids = {}
    for Y, v in secs.items():
        p, d = v if isinstance(v, tuple) else (v, None)
        grids[Y] = unpack(p, d)
    def at(lx, lz, y):
        Y = y >> 4
        g = grids.get(Y)
        if g is None: return 'minecraft:air'
        return g[((y & 15) << 8) | (lz << 4) | lx]
    for Y, g in grids.items():
        if Y < -4 or Y > 20: continue
        for i, n in enumerate(g):
            if n != 'minecraft:oak_stairs': continue
            ly = (i >> 8) & 15; lz = (i >> 4) & 15; lx = i & 15
            y = Y * 16 + ly
            below = at(lx, lz, y - 1)
            if below == 'minecraft:air':
                floats.append((cx * 16 + lx, y, cz * 16 + lz))
print('oak_stairs with air below:', len(floats))
for p in floats[:40]:
    print('   ', p)
