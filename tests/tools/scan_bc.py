# Diagnostics of aviation beacons in a noise archive: the high-rise outline at its "own" y + torches outside the outline.
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test_noise.zip')
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

def cell(x, z, y):
    ent = chunks.get((x >> 4, z >> 4), {}).get(y >> 4)
    if ent is None: return 'minecraft:air'
    entries, data = ent
    names = [e[0] for e in entries]
    if data is None: return names[0]
    bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
    i = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15)
    v = (data[i // per] >> ((i % per) * bits)) & mask
    return names[v] if v < len(names) else 'minecraft:air'

for fy in [2] + list(range(62, 75)):                 # where is the facade slab of the high-rise at all?
    n = 0
    for x in range(160, 196):
        for z in range(-110, -84):
            if cell(x, z, fy) == 'minecraft:light_gray_concrete': n += 1
    if n: print(f'concrete row y={fy}: {n}')
# torches in the extended window around the high-rise
n = 0
for x in range(160, 196):
    for z in range(-110, -84):
        for y in range(30, 260):
            if cell(x, z, y) == 'minecraft:wall_torch':
                n += 1
                if n <= 30: print('torch', x, y, z)
print('wall_torch in tower window:', n)
