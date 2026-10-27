# A floor plan of a building (walls '#', air '.') by the given rectangle and the height Y.
# Run: py -3 scan_pm.py <zip> <x0> <x1> <z0> <z1> <y>
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

path, x0, x1, z0, z1, y = sys.argv[1], *map(int, sys.argv[2:7])
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

def cell(x, z, yy=y):
    ent = chunks.get((x >> 4, z >> 4), {}).get(yy >> 4)
    if ent is None: return 'minecraft:air'
    entries, data = ent
    names = [e[0] for e in entries]
    if data is None: return names[0]
    bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
    i = ((yy & 15) << 8) | ((z & 15) << 4) | (x & 15)
    v = (data[i // per] >> ((i % per) * bits)) & mask
    return names[v] if v < len(names) else 'minecraft:air'

for z in range(z0, z1 + 1):
    row = ''
    for x in range(x0, x1 + 1):
        n = cell(x, z).split(':')[-1]
        row += '#' if n not in ('air', '') else ('.' if n == 'air' else '?')
    print(f'z{z:4d} {row}')
# the wall material legend
mats = {}
for z in range(z0, z1 + 1):
    for x in range(x0, x1 + 1):
        n = cell(x, z)
        if n != 'minecraft:air':
            mats[n] = mats.get(n, 0) + 1
print('mats:', sorted(mats.items(), key=lambda kv: -kv[1])[:6])
