# Where stairs (roofs/staircases) really stand in an archive: cluster borders by XZ.
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
exec(open(os.path.join(HERE, 'verify_mc_export.py'), encoding='utf-8').read().split('def check(')[0])

path = sys.argv[1]
f = zip = zipfile.ZipFile(path)
nch = 0
pts = []
for cx, cz, ch in chunk_iter(f):
    nch += 1
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
    for Y, (pal, data) in secs.items():
        names = [e[0] for e in pal]
        if not any('stairs' in n for n in names): continue
        if data is None:
            if any('stairs' in n for n in names):
                pts.append((cx, cz, Y, 'uniform'))
            continue
        bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
        for i in range(4096):
            v = (data[i // per] >> ((i % per) * bits)) & mask
            if v < len(names) and 'stairs' in names[v]:
                ly = (i >> 8) & 15; lz = (i >> 4) & 15; lx = i & 15
                pts.append((cx * 16 + lx, Y * 16 + ly, cz * 16 + lz, names[v].split(':')[-1]))
print('chunks:', nch, 'stair cells:', len(pts))
xs = [p[0] for p in pts]; ys = [p[1] for p in pts]; zs = [p[2] for p in pts]
if pts:
    print('x', min(xs), '..', max(xs), 'y', min(ys), '..', max(ys), 'z', min(zs), '..', max(zs))
    # rough 16x16 clusters by XZ
    from collections import Counter
    c = Counter((p[0] >> 4, p[2] >> 4) for p in pts)
    for k, v in sorted(c.items(), key=lambda kv: -kv[1])[:20]:
        print('  cluster chx {0:5d} chz {1:5d} n={2}'.format(k[0], k[1], v))
if len(sys.argv) > 4 and sys.argv[2] == '--xb':
    xa, xb = int(sys.argv[3]), int(sys.argv[4])
    sel = [p for p in pts if xa <= p[0] <= xb]
    if sel:
        print('x-band bbox: x {0}..{1}  y {2}..{3}  z {4}..{5}  n={6}'.format(
            min(p[0] for p in sel), max(p[0] for p in sel),
            min(p[1] for p in sel), max(p[1] for p in sel),
            min(p[2] for p in sel), max(p[2] for p in sel), len(sel)))
# exact cluster borders, if given: scan_fs.py <zip> --bbox <cx0> <cz0>
if len(sys.argv) > 4 and sys.argv[2] == '--bbox':
    cx0, cz0 = int(sys.argv[3]), int(sys.argv[4])
    sel = [p for p in pts if (p[0] >> 4, p[2] >> 4) == (cx0, cz0)]
    if sel:
        print('cluster bbox: x {0}..{1}  y {2}..{3}  z {4}..{5}'.format(
            min(p[0] for p in sel), max(p[0] for p in sel),
            min(p[1] for p in sel), max(p[1] for p in sel),
            min(p[2] for p in sel), max(p[2] for p in sel)))
        kinds = Counter(p[3] for p in sel)
        print('kinds:', dict(kinds))
