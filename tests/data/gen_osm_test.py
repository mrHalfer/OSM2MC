# -*- coding: utf-8 -*-
"""A synthetic OSM for checking the building classification rules (osm_rules.buildings.draft.json).

Writes osm_buildings.osm and osm_buildings.expected.json next to it ({osm_id: the expected CODE or null — "skipped"}).
The buildings are placed in clusters at a distance of > 60 m from each other so that neighbors (nb:*) do not interfere with the neighboring cases.
"""
import json, math, os

HERE = os.path.dirname(os.path.abspath(__file__))
lat0, lon0 = 51.66, 39.20
mx = 111320.0 * math.cos(math.radians(lat0))
my = 111320.0
nid = [0]
wid = [100]
nodes, ways, expected = [], [], {}


def esc(v):
    return v.replace('&', '&amp;').replace('"', '&quot;').replace('<', '&lt;')


def node(x, y, tags=None):
    nid[0] += 1
    t = ''.join('<tag k="%s" v="%s"/>' % (k, esc(v)) for k, v in (tags or {}).items())
    nodes.append('<node id="%d" lat="%.7f" lon="%.7f">%s</node>' % (nid[0], lat0 + y / my, lon0 + x / mx, t))
    return nid[0]


def rect(x, y, w, h, tags, expect='-', note=''):
    """expect: CODE (str) — the expected building code; None — the object must be skipped; '-' — not a building, no check is needed."""
    ids = [node(x, y), node(x + w, y), node(x + w, y + h), node(x, y + h)]
    ids.append(ids[0])
    wid[0] += 1
    nd = ''.join('<nd ref="%d"/>' % i for i in ids)
    t = ''.join('<tag k="%s" v="%s"/>' % (k, esc(v)) for k, v in tags.items())
    ways.append('<way id="%d">%s%s</way>' % (wid[0], nd, t))
    if expect != '-':
        expected[str(wid[0])] = expect
    return wid[0]


# --- pass 1: explicit signs ---------------------------------------------------------
rect(0, 0, 30, 20, {'building': 'yes'}, '44312000')                                           # A: yes + a school POI inside
node(15, 10, {'amenity': 'school', 'name': 'S'})
rect(100, 0, 60, 15, {'building': 'yes', 'building:levels': '9', 'addr:street': 'Ул', 'addr:housenumber': '1'}, '44400000')   # B: 9 floors
rect(250, -50, 150, 120, {'landuse': 'residential'})                                           # a residential zone
rect(300, 0, 10, 10, {'building': 'yes'}, '44400002')                                          # C: a small yes on a residential zone (the 2nd pass)
rect(600, 0, 40, 20, {'building': 'yes'}, '44312000')                                          # D: yes inside a school plot, 800 m²
rect(560, -40, 150, 100, {'amenity': 'school'})                                                # the school plot
rect(1000, 0, 100, 60, {'building': 'retail'}, '44318000')                                     # E: a big retail
rect(1300, 0, 50, 30, {'building': 'yes'}, '44318000')                                         # F: yes + POI shop=mall
node(1325, 15, {'shop': 'mall'})
rect(1600, 0, 20, 10, {'building': 'roof'}, None)                                              # G: a canopy — skip
rect(1700, 0, 12, 10, {'building': 'house', 'building:levels': '2'}, '44400002')               # H: a house
rect(1800, 0, 4, 4, {'building': 'garage'}, '44000000')                                        # I: a garage
rect(1900, 0, 25, 25, {'building': 'yes', 'height': '14 m'}, '44310000')                       # J: yes without neighbors, 625 m²
rect(2200, 0, 20, 20, {'building': 'church'}, '44319000')                                      # religious
rect(2400, 0, 20, 20, {'building': 'yes', 'amenity': 'kindergarten'}, '44313000')              # a kindergarten: amenity on the building
rect(2600, 0, 30, 20, {'building': 'hospital'}, '44314000')                                    # a hospital
rect(2800, 0, 40, 25, {'building': 'industrial'}, '44340000')                                  # industrial

# a temple territory: three neutral buildings inside a place_of_worship plot (of different sizes), an explicit shed stays a shed
rect(3300, -40, 120, 100, {'amenity': 'place_of_worship'})
rect(3320, 0, 30, 20, {'building': 'yes'}, '44319000')
rect(3370, 0, 12, 8, {'building': 'yes'}, '44319000')
rect(3400, 0, 5, 5, {'building': 'shed'}, '44000000')

# --- pass 2: a "guess" by neighbors and area -------------------------------------------
for i in range(3):
    rect(3000 + i * 40, 0, 30, 14, {'building': 'apartments', 'building:levels': '9'}, '44400000')    # an apartment block cluster
rect(3000, 30, 50, 20, {'building': 'yes'}, '44400000')                                        # K: 1000 m² next to apartment blocks
rect(3070, 30, 8, 6, {'building': 'yes'}, '44000000')                                          # L: 48 m² next to apartment blocks
for i in range(3):
    rect(5000 + i * 25, 0, 10, 10, {'building': 'house'}, '44400002')                          # a cluster of private houses
rect(5010, 20, 12, 10, {'building': 'yes'}, '44400002')                                        # M: 120 m² next to private ones
rect(5040, 20, 30, 20, {'building': 'yes'}, '44310000')                                        # N: 600 m² next to private ones
rect(7000, 0, 10, 10, {'building': 'yes'}, '44400002')                                         # O: 100 m² a loner


# --- not buildings: roads, rails, parking lots, sites, a square (x from 8000) -----------------------
expected_layers = {}      # layer -> {code: how many}


def line(pts, tags, layer=None, code=None):
    ids = [node(x, y) for x, y in pts]
    wid[0] += 1
    nd = ''.join('<nd ref="%d"/>' % i for i in ids)
    t = ''.join('<tag k="%s" v="%s"/>' % (k, esc(v)) for k, v in tags.items())
    ways.append('<way id="%d">%s%s</way>' % (wid[0], nd, t))
    if layer:
        expected_layers.setdefault(layer, {}).setdefault(code, 0)
        expected_layers[layer][code] += 1


def area(x, y, w, h, tags, layer=None, code=None):
    rect(x, y, w, h, tags, '-')
    if layer:
        expected_layers.setdefault(layer, {}).setdefault(code, 0)
        expected_layers[layer][code] += 1


line([(8000, 0), (8200, 0)], {'highway': 'primary'}, 'rdslin', '45210000')
line([(8000, 20), (8200, 20)], {'highway': 'residential', 'name': 'X'}, 'rdslin', '45230000')
line([(8000, 40), (8200, 40)], {'highway': 'residential'}, 'rdslin', '45270000')
line([(8000, 60), (8200, 60)], {'highway': 'footway'}, 'rdslin', '45260000')
line([(8000, 80), (8200, 80)], {'highway': 'footway', 'footway': 'sidewalk'}, 'rdslin', '45260000')
line([(8000, 100), (8200, 100)], {'highway': 'service', 'service': 'driveway'}, 'rdslin', '45260000')
line([(8000, 120), (8200, 120)], {'highway': 'cycleway'}, 'rdslin', '45261000')
line([(8000, 140), (8200, 140)], {'highway': 'track'}, 'rdslin', '61330000')
# pedestrian crossings: a line across primary (a zebra), a node on another section of the same road (-> a 12 m line), a node next to a crossing line (skipped),
# a node on a trail where there is no motor road (skipped)
line([(8100, -8), (8100, 8)], {'highway': 'footway', 'footway': 'crossing'}, 'rdslin', '45262000')
node(8150, 0, {'highway': 'crossing'})
expected_layers['rdslin']['45262000'] += 1
node(8105, 0, {'highway': 'crossing'})
node(8100, 60, {'highway': 'crossing'})
line([(8000, 180), (8200, 180)], {'highway': 'residential', 'tunnel': 'yes'})                      # skip
line([(8000, 200), (8300, 200)], {'railway': 'rail'}, 'rrslin', '61111000')
line([(8000, 220), (8300, 220)], {'railway': 'abandoned'})                                         # skip
area(8500, 0, 60, 40, {'amenity': 'parking'}, 'rdcpol', '62230000')
area(8500, 100, 60, 40, {'amenity': 'parking', 'parking': 'underground'})                          # skip
area(8700, 0, 40, 30, {'leisure': 'playground'}, 'csrpol', '53710000')
node(8800, 20, {'leisure': 'playground'})                                                          # a point -> a 20 m square
expected_layers.setdefault('csrpol', {}).setdefault('53710000', 0)
expected_layers['csrpol']['53710000'] += 1
area(8900, 0, 60, 30, {'leisure': 'pitch', 'sport': 'soccer'}, 'csrpol', '53700000')
area(9100, 0, 50, 50, {'highway': 'pedestrian', 'area': 'yes'}, 'blkpol', '45300000')
# vegetation (outlines smaller than the Pale Garden threshold of 2000 m2: the fallback forest must remain)
area(9300, 0, 40, 40, {'natural': 'wood'}, 'vegpol', '71111110')
area(9400, 0, 40, 40, {'landuse': 'grass'}, 'vegpol', '71329000')
area(9500, 0, 40, 40, {'leisure': 'park'}, 'vegpol', '71126000')

osm = ("<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6'>\n" + '\n'.join(nodes) + '\n' + '\n'.join(ways) + '\n</osm>\n')
open(os.path.join(HERE, 'osm_buildings.osm'), 'w', encoding='utf-8').write(osm)
json.dump({'buildings': expected, 'layers': expected_layers}, open(os.path.join(HERE, 'osm_buildings.expected.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('osm_buildings.osm:', len(ways), 'ways,', len(expected), 'buildings with an expected result')
