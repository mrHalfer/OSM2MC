# Columns under the aviation beacon torches: under a hanging torch there must NOT be an earth/stone
# pillar — only ordinary ground up to genGroundTop (y<=0 on a flat one).
# Run: py -3 scan_col.py [zip] [x z ...]
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')
pts = [(int(sys.argv[i]), int(sys.argv[i + 1])) for i in range(2, len(sys.argv), 2)] or \
      [(169, -104), (169, -90), (185, -104), (185, -90), (170, -105), (184, -105)]
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

for x, z in pts:
    col = [(y, cell(x, y, z).split(':')[-1][:10]) for y in range(1, 80)]
    col = [p for p in col if p[1] != 'air']
    print(f'({x},z{z}): ' + (','.join(f'{y}:{m}' for y, m in col) or 'EMPTY'))
