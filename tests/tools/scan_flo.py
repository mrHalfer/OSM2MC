# A base for verify: floating oak_stairs (air under them) and the ground under them (armor) in all 4 archives.
# Run: py -3 scan_flo.py [zip ...]
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

paths = sys.argv[1:] or [os.path.join(__import__('tcfg').WORLDS, n) for n in
                         ('mc_test.zip', 'mc_test_legacy.zip', 'mc_test_noise.zip', 'mc_test_legacy_noise.zip')]
for path in paths:
    if not os.path.exists(path):
        print(os.path.basename(path), 'MISSING'); continue
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

    stair = []
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            names = [e[0] for e in entries]
            idx = {pi for pi, n in enumerate(names) if n.endswith('oak_stairs')}
            if data is None:
                if idx:
                    for i in range(4096):
                        stair.append((cx*16+(i & 15), sy*16+((i >> 8) & 15), cz*16+((i >> 4) & 15)))
                continue
            if not idx: continue
            bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
            for i in range(4096):
                v = (data[i // per] >> ((i % per) * bits)) & mask
                if v in idx:
                    stair.append((cx*16+(i & 15), sy*16+((i >> 8) & 15), cz*16+((i >> 4) & 15)))
    air_below = dirt_below = other_below = 0
    bad_samples = []
    for x, y, z in stair:
        b = cell(x, y - 1, z)
        if b == 'minecraft:air':
            air_below += 1
            if len(bad_samples) < 8: bad_samples.append(('air', x, y, z))
        elif b.split(':')[-1] in ('grass_block', 'dirt', 'coarse_dirt', 'podzol', 'grass'):
            dirt_below += 1
            if len(bad_samples) < 16: bad_samples.append((b.split(':')[-1], x, y, z))
        else:
            other_below += 1
    print(os.path.basename(path), f'stairs={len(stair)} air_below={air_below} dirt_below={dirt_below}',
          'samples=', bad_samples[:8])
