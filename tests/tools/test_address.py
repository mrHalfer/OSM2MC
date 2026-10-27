# -*- coding: utf-8 -*-
"""A test of address signs and street signposts for the OSM source.

Builds a small OSM with different ways of writing an address (the street type at the beginning/at the end, abbreviations, a building and a structure,
addr:place, addr:full, only a house number next to a named road, an English address) and compares the lines
"type | name | number" that tMC writes to the log at TMC_OSM_DEBUG=1 (the line "[addr id] type|name|number").

Run: py -3 test_address.py        Exit 0 — PASS.
"""
import math, os, re, sys
import tcfg
from tmc_run import run_tmc

lat0, lon0 = 51.66, 39.20
mx = 111320.0 * math.cos(math.radians(lat0))
my = 111320.0
nid = [0]
wid = [0]
nodes, ways = [], []


def esc(v):
    return v.replace('&', '&amp;').replace('"', '&quot;').replace('<', '&lt;')


def node(x, y):
    nid[0] += 1
    nodes.append('<node id="%d" lat="%.7f" lon="%.7f"/>' % (nid[0], lat0 + y / my, lon0 + x / mx))
    return nid[0]


def way(pts, tags, closed):
    ids = [node(x, y) for x, y in pts]
    if closed:
        ids.append(ids[0])
    wid[0] += 1
    nd = ''.join('<nd ref="%d"/>' % i for i in ids)
    t = ''.join('<tag k="%s" v="%s"/>' % (k, esc(v)) for k, v in tags.items())
    ways.append('<way id="%d">%s%s</way>' % (wid[0], nd, t))
    return str(wid[0])


expected = {}
x = [0]


def house(tags, want):
    """A nine-storey residential house with the given addr tags; want — the expected (type, name, number)."""
    base = {'building': 'apartments', 'building:levels': '9'}
    base.update(tags)
    x0 = x[0]
    x[0] += 200                                  # the houses are far from each other and from roads
    expected[way([(x0, 5000), (x0 + 40, 5000), (x0 + 40, 5020), (x0, 5020)], base, True)] = want


def road(name_tags, want):
    tags = {'highway': 'residential'}
    tags.update(name_tags)
    x0 = x[0]
    x[0] += 200
    expected[way([(x0, 9000), (x0 + 150, 9000)], tags, False)] = want


house({'addr:street': 'улица Ленина', 'addr:housenumber': '5'}, ('улица', 'Ленина', '5'))
house({'addr:street': 'Южный проспект', 'addr:housenumber': '12к1'}, ('проспект', 'Южный', '12К.1'))
house({'addr:street': 'пр-кт Мира', 'addr:housenumber': '26а'}, ('проспект', 'Мира', '26А'))
house({'addr:street': 'Садовая ул.', 'addr:housenumber': 'д. 7'}, ('улица', 'Садовая', '7'))
house({'addr:street': 'ул. 8 Марта', 'addr:housenumber': '3'}, ('улица', '8 Марта', '3'))
house({'addr:street': '2-й Южный проезд', 'addr:housenumber': '12 к 3'}, ('проезд', '2-й Южный', '12К.3'))
house({'addr:street': 'Школьная улица', 'addr:housenumber': '12с2'}, ('улица', 'Школьная', '12С.2'))
house({'addr:street': 'бульвар 60-летия Победы', 'addr:housenumber': '12/3'}, ('бульвар', '60-летия Победы', '12/3'))
house({'addr:street': 'Oxford Street', 'addr:housenumber': '10'}, ('', 'Oxford Street', '10'))
house({'addr:place': 'деревня Ивановка', 'addr:housenumber': '3'}, ('деревня', 'Ивановка', '3'))
house({'addr:full': 'Воронеж, улица Гагарина, 14'}, ('улица', 'Гагарина', '14'))
house({'addr:housename': 'Белый дом'}, ('', 'Белый дом', ''))
road({'name': 'Тверская улица'}, ('улица', 'Тверская', ''))
road({'name': 'Ленинский проспект', 'name:ru': 'Ленинский проспект'}, ('проспект', 'Ленинский', ''))
road({'name': 'Mira Street', 'name:ru': 'улица Мира'}, ('улица', 'Мира', ''))
# a house with only a number next to a named road: the street is taken from the road (in the log the line "Addresses: the street was taken ...")
x0 = x[0]
x[0] += 400
way([(x0, 12000), (x0 + 300, 12000)], {'highway': 'residential', 'name': 'Кирова улица'}, False)
way([(x0 + 100, 12020), (x0 + 140, 12020), (x0 + 140, 12040), (x0 + 100, 12040)],
    {'building': 'apartments', 'building:levels': '9', 'addr:housenumber': '4'}, True)

# an intersection of two named streets: signposts with the names must appear on it (checked by the text in the world)
xc = x[0] + 400
way([(xc - 150, 15000), (xc + 150, 15000)], {'highway': 'residential', 'name': 'улица Пушкина'}, False)
way([(xc, 14850), (xc, 15150)], {'highway': 'residential', 'name': 'Кутузовский проспект'}, False)

path = os.path.join(tcfg.WORLDS, 'osm_addr.osm')
with open(path, 'w', encoding='utf-8') as f:
    f.write("<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6'>\n" + '\n'.join(nodes) + '\n' + '\n'.join(ways) + '\n</osm>\n')

rules = os.path.join(tcfg.RESOURCES, 'osm_rules.json')
code, log, sec = run_tmc([path, '--osm-rules', rules, '--dry-run', '--no-enhancer', '--no-greenery'],
                         os.path.join(tcfg.WORLDS, 'osm_addr.log'), env_extra={'TMC_OSM_DEBUG': '1'})
print('tMC exit %d, %.1f c' % (code, sec))
if code != 0:
    print(log[-1500:])
    sys.exit(1)

got = {}
for m in re.finditer(r'\[addr (\d+)\] ([^|\n]*)\|([^|\n]*)\|([^\n]*)', log):
    got[m.group(1)] = (m.group(2), m.group(3), m.group(4).strip())

bad = 0
for oid, want in sorted(expected.items(), key=lambda kv: int(kv[0])):
    have = got.get(oid)
    ok = have == want
    bad += 0 if ok else 1
    print('  %s  %-4s %s -> %s' % ('ok ' if ok else 'BAD', oid, want, have))
if 'Addresses: the street was taken from the nearest road for 1 buildings' not in log:
    bad += 1
    print('  BAD  the street from the nearest road was not substituted')
else:
    print('  ok   the street from the nearest road was substituted')
# end to end: a world with addresses (flat — fast), the blocks must contain the texts of signs and signposts
world = os.path.join(tcfg.WORLDS, 'osm_addr.zip')
code, log2, sec = run_tmc([path, '--osm-rules', rules, '--flat', '--seed', '1', '--no-enhancer', '--no-greenery', '--out', world],
                          os.path.join(tcfg.WORLDS, 'osm_addr_world.log'))
if code != 0 or not os.path.exists(world):
    bad += 1
    print('  BAD  the world was not built')
else:
    import struct, zipfile, zlib
    z = zipfile.ZipFile(world)
    blob = []
    for name in z.namelist():
        if not name.endswith('.mca') or 'entities' in name or 'poi' in name:
            continue
        d = z.read(name)
        for i in range(1024):
            off = struct.unpack('>I', bytes(1) + d[i * 4:i * 4 + 3])[0] * 4096
            if not off:
                continue
            ln = struct.unpack('>I', d[off:off + 4])[0]
            raw = d[off + 5:off + 4 + ln]
            try:
                blob.append(zlib.decompress(raw) if d[off + 4] == 2 else raw)
            except Exception:
                pass
    data = b''.join(blob)
    for s in ('Ленина', '12К.1', 'Мира', 'Гагарина', 'Ивановка', 'Белый дом', 'Кирова', 'Пушкина', 'Кутузовский'):
        n = data.count(s.encode('utf-8'))
        bad += 0 if n else 1
        print('  %s  in the world "%s": %d' % ('ok ' if n else 'BAD', s, n))
print('PASS' if bad == 0 else 'FAIL (%d)' % bad)
sys.exit(0 if bad == 0 else 1)
