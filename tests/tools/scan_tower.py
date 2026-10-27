# Where the REAL facade of the high-rise is in mc_test.zip (the light_gray_concrete outline at y=2 and y=74) —
# we fit the aviation beacon anchors to it. Run: py -3 scan_tower.py [zip]
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

for yy in (2, 74):
    pts = [(x, z) for x in range(160, 196) for z in range(-110, -84)
           if cell(x, yy, z).endswith('concrete')]
    if pts:
        xs = [p[0] for p in pts]; zs = [p[1] for p in pts]
        print(f'y={yy}: concrete x {min(xs)}..{max(xs)}  z {min(zs)}..{max(zs)}  n={len(pts)}')
    else:
        print(f'y={yy}: no concrete')
    gl = [(x, z) for x in range(160, 196) for z in range(-110, -84)
          if 'glass' in cell(x, yy, z)]
    if gl:
        xs = [p[0] for p in gl]; zs = [p[1] for p in gl]
        print(f'y={yy}: glass x {min(xs)}..{max(xs)}  z {min(zs)}..{max(zs)}  n={len(gl)}')
# a slice: the row y=74 through the center (z=-97): what follows what along x — we show the "air" edge at the wall
print('row y=74 z=-97:', [f'{x}:{cell(x,74,-97).split(":")[-1][:6]}' for x in range(166, 190)])
