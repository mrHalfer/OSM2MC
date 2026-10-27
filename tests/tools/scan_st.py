# Clusters of columns with stone_stairs (inter-floor staircases): each cluster is one staircase.
# Run: py -3 scan_st.py [zip]
import os, sys, zipfile
from collections import deque
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')
cols = set()
for cx, cz, ch in chunk_iter(zipfile.ZipFile(path)):
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
        names = [e[0] for e in entries]
        idx = {pi for pi, n in enumerate(names) if n.endswith('stone_stairs')}
        if not idx: continue
        data = bs.get('data')
        sy = s['Y']
        if data is None:
            for i in range(4096):
                cols.add((cx * 16 + (i & 15), cz * 16 + ((i >> 4) & 15)))
            continue
        bits = max(4, (len(names) - 1).bit_length()); per = 64 // bits; mask = (1 << bits) - 1
        for i in range(4096):
            v = (data[i // per] >> ((i % per) * bits)) & mask
            if v in idx:
                cols.add((cx * 16 + (i & 15), cz * 16 + ((i >> 4) & 15)))
seen, clusters = set(), []
for x, z in cols:
    if (x, z) in seen: continue
    q, comp = deque([(x, z)]), []
    seen.add((x, z))
    while q:
        a, b = q.popleft(); comp.append((a, b))
        for da, db in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (a + da, b + db) in cols and (a + da, b + db) not in seen:
                seen.add((a + da, b + db)); q.append((a + da, b + db))
    clusters.append(comp)
print('stone_stairs columns:', len(cols), 'clusters:', len(clusters))
for comp in sorted(clusters, key=len, reverse=True):
    xs = [p[0] for p in comp]; zs = [p[1] for p in comp]
    print(f'  n={len(comp)} x{min(xs)}..{max(xs)} z{min(zs)}..{max(zs)}')
