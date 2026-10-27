# The "armor" of the private sector: where exactly the support/stone sticks out around the stair roofs.
# oak_stairs clusters (the roof) -> the profile y=the cornice bottom around a cluster (a material map).
# Run: py -3 scan_roof.py [zip]
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')
zf = zipfile.ZipFile(path)
chunks = {}
for cx, cz, ch in chunk_iter(zf):
    secs = {}
    for s in ch['sections']:
        bs = s.get('block_states', {})
        entries = []
        for e in bs.get('palette') or []:
            if isinstance(e, dict):
                if 'Name' in e: entries.append((e['Name'], e.get('Properties')))
                elif 'id' in e: entries.append((e['id'], e.get('properties')))
                else: entries.append((e[''], None))
            else:
                entries.append((e, None))
        secs[s['Y']] = (entries, bs.get('data'))
    chunks[(cx, cz)] = secs

def cell(x, y, z):
    ent = chunks.get((x >> 4, z >> 4), {}).get(y >> 4)
    if ent is None: return 'minecraft:air'
    entries, data = ent
    names = [e[0] for e in entries]
    if data is None: return names[0]
    bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
    i = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15)
    v = (data[i // per] >> ((i % per) * bits)) & mask
    return names[v] if v < len(names) else 'minecraft:air'

def cls(n):
    n = n.split(':')[-1]
    if n == 'air': return '.'
    if 'stairs' in n: return '^'
    if 'glass' in n or 'pane' in n: return 'o'
    if 'torch' in n or 'lantern' in n or 'spawner' in n or 'sign' in n or 'pot' in n: return '~'
    if 'log' in n or 'planks' in n or 'wood' in n or 'fence' in n: return 'W'
    if 'dirt' in n or 'grass' in n or 'coarse' in n or 'podzol' in n: return 'D'
    if 'stone' in n or 'cobble' in n or 'deepslate' in n or 'granite' in n: return 'S'
    if 'concrete' in n or 'brick' in n or 'terracotta' in n or 'sandstone' in n or 'quartz' in n: return '#'
    return '?'

# all oak_stairs
cells = set()
for (cx, cz), secs in chunks.items():
    for sy, (entries, data) in secs.items():
        names = [e[0] for e in entries]
        if data is None:
            if any('oak_stairs' in n for n in names):
                for i in range(4096):
                    cells.add((cx*16+(i & 15), sy*16+((i >> 8) & 15), cz*16+((i >> 4) & 15)))
            continue
        idx = {pi for pi, n in enumerate(names) if n.endswith('oak_stairs')}
        if not idx: continue
        bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
        for i in range(4096):
            v = (data[i // per] >> ((i % per) * bits)) & mask
            if v in idx: cells.add((cx*16+(i & 15), sy*16+((i >> 8) & 15), cz*16+((i >> 4) & 15)))
print('oak_stairs cells:', len(cells))

# clusters (Chebyshev<=2, BFS over a 3D grid with a coarse step — first by the (x,z) projections of the tiers)
from collections import deque
unseen = set(cells); clusters = []
while unseen:
    seed = min(unseen); q = deque([seed]); comp = []
    while q:
        c = q.popleft()
        if c not in unseen: continue
        unseen.discard(c); comp.append(c)
        x, y, z = c
        for dx in range(-3, 4):
            for dy in range(-3, 4):
                for dz in range(-3, 4):
                    n = (x+dx, y+dy, z+dz)
                    if n in unseen: q.append(n)
    if len(comp) >= 15: clusters.append(comp)
print('clusters(>=15):', len(clusters))
for comp in sorted(clusters, key=lambda c: -len(c))[:6]:
    xs = [c[0] for c in comp]; ys = [c[1] for c in comp]; zs = [c[2] for c in comp]
    x0, x1, y0, y1, z0, z1 = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    print(f'\nCLUSTER n={len(comp)} x{x0}..{x1} y{y0}..{y1} z{z0}..{z1}')
    # a map at the level of the cornice bottom y0 and at y0+2
    for yy in (y0, y0+2):
        print(f' y={yy}:')
        for z in range(z0-4, z1+5):
            row = ''
            for x in range(x0-4, x1+5):
                row += cls(cell(x, yy, z))
            print('   ' + row)
    # the column profile at a "suspicious" corner: y from 0 to y0+1
    wx, wz = x0-2, z0-2
    prof = [(yy, cell(wx, yy, wz).split(':')[-1]) for yy in range(0, min(y0+2, 40))]
    prof = [p for p in prof if p[1] != 'air']
    print(f' col({wx},z{wz}) y<40:', prof[-14:])
