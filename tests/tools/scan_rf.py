# A vertical roof section: a slice of the XZ plane (fixed z), x horizontally, y from top to bottom.
# Run: py -3 scan_rf.py <zip> <x0> <x1> <z> <yTop> <yBot>
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

path, x0, x1, z, yTop, yBot = sys.argv[1], *map(int, sys.argv[2:7])
chunks = {}
for cx, cz, ch in chunk_iter(zipfile.ZipFile(path)):
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

def cell(x, zz, yy):
    ent = chunks.get((x >> 4, zz >> 4), {}).get(yy >> 4)
    if ent is None: return 'minecraft:air'
    entries, data = ent
    names = [e[0] for e in entries]
    if data is None: return names[0]
    bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
    i = ((yy & 15) << 8) | ((zz & 15) << 4) | (x & 15)
    v = (data[i // per] >> ((i % per) * bits)) & mask
    return names[v] if v < len(names) else 'minecraft:air'

def sym(n):
    s = n.split(':')[-1]
    if s in ('air', ''): return '.'
    if 'stairs' in s: return '^'
    if 'slab' in s: return '_'
    if 'glass' in s: return 'G'
    if 'planks' in s or 'log' in s or 'wood' in s or 'fence' in s: return 'W'
    if 'lantern' in s or 'torch' in s: return 'O'
    if 'terracotta' in s or 'concrete' in s or 'wool' in s: return 'C'
    if 'brick' in s or 'stone' in s or 'deepslate' in s: return 'S'
    if 'grass' in s or 'dirt' in s: return 'E'
    return '?'

legend = {}
for y in range(yTop, yBot - 1, -1):
    row = ''
    for x in range(x0, x1 + 1):
        n = cell(x, z, y)
        c = sym(n)
        row += c
        if c not in '.':
            legend[n] = legend.get(n, 0) + 1
    print(f'y{y:4d} {row}')
print('legend:', sorted(legend.items(), key=lambda kv: -kv[1])[:12])
