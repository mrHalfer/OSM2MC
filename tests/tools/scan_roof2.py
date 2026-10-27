# Vertical profiles of columns around stair roofs (is there a stone-and-earth "armor"
# under a cornice hanging OVER THE VOID a block outward from the walls). Run: py -3 scan_roof2.py [zip]
import os, sys, zipfile
from collections import deque
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

unseen = set(cells); clusters = []
while unseen:
    seed = min(unseen); q = deque([seed]); comp = []
    while q:
        c = q.popleft()
        if c not in unseen: continue
        unseen.discard(c); comp.append(c)
        x, y, z = c
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                for dz in range(-2, 3):
                    n = (x+dx, y+dy, z+dz)
                    if n in unseen: q.append(n)
    if len(comp) >= 15: clusters.append(comp)

def prof(x, z):
    col = []
    for yy in range(1, 41):
        n = cell(x, yy, z).split(':')[-1]
        if n != 'air': col.append((yy, n[:9]))
    return f'({x},z{z})=' + ','.join(f'{y}:{m}' for y, m in col[-9:])

for comp in sorted(clusters, key=lambda c: -len(c))[:4]:
    xs = [c[0] for c in comp]; ys = [c[1] for c in comp]; zs = [c[2] for c in comp]
    x0, x1, y0, y1, z0, z1 = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    print(f'CLUSTER n={len(comp)} x{x0}..{x1} y{y0}..{y1} z{z0}..{z1}')
    xm, zm = (x0+x1)//2, (z0+z1)//2
    # the cornice row (y0): stairs on the bbox; profiles: the bbox line, a block INWARD, a block OUTWARD
    print('  OUT  ' + prof(xm, z0-1))
    print('  RING ' + prof(xm, z0))
    print('  IN   ' + prof(xm, z0+1))
    print('  OUT  ' + prof(x0-1, zm))
    print('  RING ' + prof(x0, zm))
    print('  IN   ' + prof(x0+1, zm))
    print('  IN2  ' + prof(xm, zm))
