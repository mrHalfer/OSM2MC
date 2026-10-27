# A smoke test of batch4 on mc_test.zip: beacons on the ring, the ceiling above stairs, no
# caves in flat, shopping center panorama runs, a pedestrian road at z=-12. Run: py -3 check4.py
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

zf = zipfile.ZipFile(os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip'))
chunks = {}
secAir = {}
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

# 1) beacons: wall_torch strictly outside the outline 170..184 x -104..-90
ring = []
for x in range(167, 188):
    for z in range(-107, -87):
        if 170 <= x <= 184 and -104 <= z <= -90: continue
        for y in range(30, 240):
            if cell(x, y, z) == 'minecraft:wall_torch': ring.append((x, y, z))
print('BEACON ring torches:', len(ring), ring[:12])

# 2) stairs: the cell above each one — air? (a walk over the section palettes 0..15)
bad = 0
for (cx, cz), secs in chunks.items():
    for sy, (entries, data) in secs.items():
        if sy < 0 or sy > 15 or data is None: continue
        names = [e[0] for e in entries]
        st = {pi for pi, n in enumerate(names) if n.endswith('stone_stairs')}
        if not st: continue
        bits = max(4, (len(names) - 1).bit_length()); per = 64 // bits; mask = (1 << bits) - 1
        for i in range(4096):
            v = (data[i // per] >> ((i % per) * bits)) & mask
            if v not in st: continue
            x = cx * 16 + (i & 15); y = sy * 16 + ((i >> 8) & 15); z = cz * 16 + ((i >> 4) & 15)
            up = cell(x, y + 1, z)
            if up != 'minecraft:air':
                bad += 1
                if bad <= 6: print('HEAD_BAD', (x, y, z), up)
print('HEAD_BAD total:', bad)

# 3) flat without caves: deep air y<-32 (sections <=-3) is absent — a decode by palettes.
# (y=-1/-2 — the legal chamfer of the map edge, below -32 there are no cave templates.)
deepAir = 0
for (cx, cz), secs in chunks.items():
    for sy, (entries, data) in secs.items():
        if sy > -3: continue
        names = [e[0] for e in entries]
        if data is None:
            deepAir += 4096 if names[0] == 'minecraft:air' else 0
            continue
        ai = {pi for pi, n in enumerate(names) if n == 'minecraft:air'}
        if not ai: continue
        bits = max(4, (len(names) - 1).bit_length()); per = 64 // bits; mask = (1 << bits) - 1
        for i in range(4096):
            if ((data[i // per] >> ((i % per) * bits)) & mask) in ai: deepAir += 1
print('FLAT deep air y<-32 (want 0):', deepAir)

# 4) the shopping center panorama: glass runs
gset = set()
for (cx, cz), secs in chunks.items():
    for sy, (entries, data) in secs.items():
        if sy < 0 or sy > 1 or data is None: continue
        names = [e[0] for e in entries]
        gl = {pi for pi, n in enumerate(names) if 'glass' in n}
        if not gl: continue
        bits = max(4, (len(names) - 1).bit_length()); per = 64 // bits; mask = (1 << bits) - 1
        for i in range(4096):
            v = (data[i // per] >> ((i % per) * bits)) & mask
            if v not in gl: continue
            x = cx * 16 + (i & 15); y = sy * 16 + ((i >> 8) & 15); z = cz * 16 + ((i >> 4) & 15)
            if 277 <= x <= 312 and -213 <= z <= -187 and 1 <= y <= 12: gset.add((x, y, z))
def runs(line):
    r = best = 0
    for on in line: best, r = max(best, r + 1 if on else 0), r + 1 if on else 0
    return best
mx = 0
for y in range(1, 13):
    for z in range(-213, -187): mx = max(mx, runs([(x, y, z) in gset for x in range(277, 313)]))
    for x in range(277, 313): mx = max(mx, runs([(x, y, z) in gset for z in range(-213, -187)]))
print('PANO max run:', mx)

# 5) a pedestrian road
n = sum(1 for x in range(75, 82) if cell(x, 0, -12) == 'minecraft:light_gray_wool')
print('PED z=-12 x75..81:', n)
# the highway marking — along the strip center?
for z in range(-18, -12):
    c = sum(1 for x in range(60, 101) if cell(x, 1, z) == 'minecraft:light_gray_carpet')
    if c: print('MARK z=', z, 'n=', c)
