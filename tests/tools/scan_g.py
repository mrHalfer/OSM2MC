# Column profiles of the L-house: the cornice/gable above the cut-out (armor?) + air in the cut-out itself.
# Run: py -3 scan_g.py [zip]
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')
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

def prof(x, z, hi=14):
    col = [(yy, cell(x, yy, z).split(':')[-1][:9]) for yy in range(1, hi)]
    col = [p for p in col if p[1] != 'air']
    return f'({x},z{z}): ' + ','.join(f'{y}:{m}' for y, m in col)

for x, z in [(70, -70), (71, -70), (72, -70), (72, -72), (72, -74), (70, -72),
             (70, -73), (69, -72), (72, -80), (70, -81), (66, -81)]:
    print(prof(x, z))
