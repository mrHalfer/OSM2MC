# Climate-zone regression: tiny two-building scenes at real city coordinates - prints the
# "Climate zone:" line of every export and checks it against the expected Kooppen zone.
# Usage: python check_zones.py          (the tMC exe comes from tcfg: OSMC_EXE env or local_config.json)
# The list covers the monsoon and semi-arid regions of 2026 plus three NOT controls.
import math, os, subprocess, sys, tempfile
import tcfg

M0 = 111320.0


def scene(path, lon0, lat0):
    m = M0 * math.cos(math.radians(lat0))
    nodes = {}

    def nid(p):
        if p not in nodes:
            nodes[p] = len(nodes) + 1
        return nodes[p]

    ways = []
    for (dx, dy, w, h) in ((0, 0, 30, 30), (400, 300, 40, 25)):     # two buildings: the world bounds
        pts = [(lon0 + (dx + i * w) / m, lat0 + (dy + j * h) / m) for i, j in ((0, 0), (1, 0), (1, 1), (0, 1))]
        pts.append(pts[0])
        ways.append(([nid(p) for p in pts], {'building': 'yes'}))
    with open(path, 'w', encoding='utf-8') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<osm version="0.6" generator="zcheck">\n')
        for (x, y), i in sorted(nodes.items(), key=lambda kv: kv[1]):
            f.write(f'  <node id="{i}" lat="{y:.7f}" lon="{x:.7f}" version="1"/>\n')
        for k, (ids, tags) in enumerate(ways, start=1001):
            f.write(f'  <way id="{k}" version="1">\n')
            for i in ids:
                f.write(f'    <nd ref="{i}"/>\n')
            for kk, vv in tags.items():
                f.write(f'    <tag k="{kk}" v="{vv}"/>\n')
            f.write('  </way>\n')
        f.write('</osm>\n')


CITIES = [
    ('Savannah GA',      -81.099, 32.081, 'monsoon'),
    ('Charleston SC',    -79.930, 32.776, 'monsoon'),
    ('Houston TX',       -95.370, 29.760, 'semi-arid'),
    ('New Orleans LA',   -90.072, 29.951, 'semi-arid'),
    ('Corpus Christi TX', -97.396, 27.801, 'semi-arid'),
    ('Guangzhou',        113.260, 23.130, 'monsoon'),
    ('Taipei',           121.560, 25.033, 'monsoon'),
    ('Naha Okinawa',     127.681, 26.212, 'monsoon'),
    ('Fukuoka Kyushu',   130.402, 33.590, 'monsoon'),
    ('Miami FL',         -80.194, 25.762, 'monsoon'),
    ('Dallas TX (ctl)',  -96.797, 32.777, 'NOT'),
    ('Tokyo (ctl)',      139.691, 35.687, 'NOT'),
    ('Beijing (ctl)',    116.407, 39.904, 'NOT'),
]

if not os.path.isfile(tcfg.OSMC_EXE):
    print('tMC exe not found:', tcfg.OSMC_EXE, '- set OSMC_EXE or tests/local_config.json')
    sys.exit(77)
out = tempfile.mkdtemp(prefix='zcheck')
fails = 0
for name, lon, lat, want in CITIES:
    osm, zip_, log = (os.path.join(out, 'z' + e) for e in ('.osm', '.zip', '.log'))
    for p in (zip_, log):
        if os.path.exists(p):
            os.remove(p)
    scene(osm, lon, lat)
    subprocess.run([tcfg.OSMC_EXE, osm, '-o', zip_, '--caves', '0', '--nogui', '--lang', 'en', '--log', log],
                   check=True, env=tcfg.tool_env())
    zone = ''
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            if 'Climate zone:' in line:
                zone = line.strip()
                break
    zl = zone.lower()
    ok = ('monsoon' not in zl and 'semi-arid' not in zl) if want == 'NOT' else (want in zl)
    fails += 0 if ok else 1
    print(f"{'OK ' if ok else 'BAD'} {name:18s} -> {zone}")
print('FAILS:', fails)
sys.exit(1 if fails else 0)
