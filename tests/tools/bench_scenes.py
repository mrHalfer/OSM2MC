# Bench/probe .osm scenes for step-time profiling and regression checks (the speed check in .agents/build-test.md).
# Usage: python bench_scenes.py <out_dir> [name ...]     (without names - all scenes)
#       python bench_scenes.py --list
# Scenes (~1.2-1.5 km, temperate lat 55.75, corner buildings give the world bounds + voxel chunks):
#   s1_city s2_forest s3_water s4_roads s5_structures - one hotspot per exporter step
#   s6_metro  - a subway+tunnel=yes line with turns (the tunnel test scene)
#   bench     - the dense scene for the speed regressions (forests + overlapping wetlands + water)
#   p1_nowet p2a_swamp10k p2b_swamp90k p2c_swamp250k p4_floor p5_small - scaling probes
import io, math, os, random, sys

LAT0 = 55.75
KX = 111320.0 * math.cos(math.radians(LAT0))
KY = 111320.0
OUT = r'.'


class Scene:
    def __init__(self, name):
        self.name = name
        self.nodes = []
        self.ways = []
        self.nid = 0
        self.wid = 0

    def lon(self, x):
        return 37.60 + x / KX

    def lat(self, y):
        return LAT0 + y / KY

    def node(self, x, y, tags):
        self.nid += 1
        self.nodes.append((self.nid, x, y, tags))
        return self.nid

    def rect(self, x0, y0, x1, y1, tags):
        a = self.node(x0, y0, {}); b = self.node(x1, y0, {}); c = self.node(x1, y1, {}); d = self.node(x0, y1, {})
        self.wid += 1
        self.ways.append(([a, b, c, d, a], tags))

    def line(self, pts, tags):
        ids = [self.node(px, py, {}) for px, py in pts]
        self.wid += 1
        self.ways.append((ids, tags))

    def corners(self):
        for bx, by in [(50, 50), (1150, 50), (50, 1150), (1150, 1150)]:
            self.rect(bx, by, bx + 20, by + 20, {'building': 'yes', 'building:levels': '5'})

    def save(self):
        p = os.path.join(OUT, '%s.osm' % self.name)
        out = io.open(p, 'w', encoding='utf-8', newline='')
        out.write('<?xml version="1.0" encoding="UTF-8"?>\n<osm version="0.6" generator="probe">\n')
        for i, x, y, tg in self.nodes:
            out.write(' <node id="%d" lat="%.7f" lon="%.7f"' % (i, self.lat(y), self.lon(x)))
            if tg:
                out.write('>\n')
                for k, v in sorted(tg.items()):
                    out.write('  <tag k="%s" v="%s"/>\n' % (k, v))
                out.write(' </node>\n')
            else:
                out.write('/>\n')
        wid = 500000
        for nds, tg in self.ways:
            wid += 1
            out.write(' <way id="%d">\n' % wid)
            for n in nds:
                out.write('  <nd ref="%d"/>\n' % n)
            for k, v in sorted(tg.items()):
                out.write('  <tag k="%s" v="%s"/>\n' % (k, v))
            out.write(' </way>\n')
        out.write('</osm>\n')
        out.close()
        print('%s: nodes %d ways %d -> %s' % (self.name, len(self.nodes), len(self.ways), p))


def city():
    s = Scene('s1_city')
    s.corners()
    rng = random.Random(11)
    mats = ['brick', 'stone', 'concrete', 'glass', 'metal']
    gx = range(200, 1101, 110)
    for x in gx:
        s.line([(x, 150), (x, 1150)], {'highway': 'residential', 'surface': 'asphalt'})
    for y in gx:
        s.line([(150, y), (1150, y)], {'highway': 'secondary', 'lanes': '4', 'surface': 'asphalt'})
    s.line([(150, 605), (1150, 605)], {'highway': 'primary', 'lanes': '6', 'surface': 'asphalt'})
    for bx in list(gx)[:-1]:
        for by in list(gx)[:-1]:
            for _ in range(3):
                w = rng.randint(22, 45); h = rng.randint(22, 45)
                x0 = bx + rng.randint(6, 100 - w if 100 - w > 6 else 6)
                y0 = by + rng.randint(6, 100 - h if 100 - h > 6 else 6)
                lv = rng.choice([3, 4, 5, 5, 7, 9, 12])
                s.rect(x0, y0, x0 + w, y0 + h, {'building': 'yes', 'building:levels': str(lv),
                                                'building:material': rng.choice(mats)})
    for cx, cy in [(420, 420), (530, 480), (470, 590), (640, 420), (700, 700), (960, 860)]:
        s.rect(cx, cy, cx + 40, cy + 40, {'building': 'yes', 'building:levels': str(rng.randint(14, 22)),
                                          'building:material': rng.choice(['glass', 'concrete', 'metal'])})
    s.rect(250, 250, 340, 330, {'amenity': 'school', 'building': 'school', 'building:levels': '4'})
    s.rect(850, 250, 960, 340, {'amenity': 'hospital', 'building': 'yes', 'building:levels': '6'})
    s.rect(250, 850, 360, 950, {'shop': 'mall', 'building': 'retail', 'building:levels': '3'})
    s.rect(860, 860, 920, 940, {'amenity': 'place_of_worship', 'building': 'church'})
    s.rect(620, 250, 690, 320, {'amenity': 'kindergarten', 'building': 'yes', 'building:levels': '2'})
    s.save()


def forest():
    s = Scene('s2_forest')
    s.corners()
    for fx, fy in [(150, 150), (620, 150), (1000, 150), (150, 620), (620, 620), (1000, 620), (380, 980), (820, 980)]:
        s.rect(fx, fy, min(fx + 350, 1250), min(fy + 350, 1250), {'natural': 'wood'})
    s.rect(150, 150, 450, 450, {'natural': 'wetland'})
    s.rect(620, 620, 920, 920, {'natural': 'wetland'})
    s.rect(1000, 1000, 1240, 1240, {'natural': 'wetland'})
    for px, py in [(520, 300), (300, 900), (960, 480)]:
        s.rect(px, py, px + 220, py + 220, {'leisure': 'park'})
        s.rect(px, py, px + 220, py + 220, {'natural': 'wood'})
    s.rect(700, 300, 800, 360, {'landuse': 'farmland'})
    s.save()


def water():
    s = Scene('s3_water')
    s.corners()
    s.rect(300, 300, 950, 900, {'natural': 'water'})                        # reservoir 422k m2
    s.rect(100, 1000, 1250, 1090, {'natural': 'water', 'water': 'river', 'waterway': 'riverbank'})
    rng = random.Random(5)
    for _ in range(8):
        x0 = rng.randint(100, 1100); y0 = rng.randint(100, 1050)
        s.rect(x0, y0, x0 + 70, y0 + 50, {'natural': 'water'})
    for px, py in [(1050, 200), (150, 950), (1000, 950)]:
        s.rect(px, py, px + 50, py + 25, {'leisure': 'swimming_pool'})
    s.line([(60, 600), (1250, 600)], {'highway': 'secondary', 'lanes': '4', 'surface': 'asphalt'})   # bridge
    s.line([(625, 60), (625, 1140)], {'waterway': 'canal'})
    s.rect(200, 150, 300, 250, {'natural': 'wetland'})
    s.save()


def roads():
    s = Scene('s4_roads')
    s.corners()
    for x in range(150, 1201, 70):
        s.line([(x, 120), (x, 1180)], {'highway': 'residential', 'surface': 'asphalt'})
    for y in range(150, 1201, 70):
        s.line([(120, y), (1180, y)], {'highway': 'residential', 'surface': 'asphalt'})
    s.line([(120, 650), (1180, 650)], {'highway': 'primary', 'lanes': '6', 'surface': 'asphalt'})
    s.line([(650, 120), (650, 1180)], {'highway': 'primary', 'lanes': '6', 'surface': 'asphalt'})
    s.line([(120, 300), (1180, 300)], {'railway': 'rail'})
    s.line([(900, 120), (900, 1180)], {'railway': 'rail'})
    for x in range(200, 1200, 160):
        s.line([(x, 120), (x + 60, 1180)], {'highway': 'footway'})
    for bx, by in [(300, 400), (800, 800), (400, 900)]:
        s.rect(bx, by, bx + 60, by + 60, {'building': 'yes', 'building:levels': '4'})
    s.save()


def structures():
    s = Scene('s5_structures')
    s.corners()
    s.rect(300, 300, 400, 400, {'amenity': 'place_of_worship', 'building': 'church', 'roof:shape': 'onion',
                                'roof:colour': 'gold'})
    s.rect(700, 300, 850, 420, {'amenity': 'school', 'building': 'school', 'building:levels': '4'})
    s.rect(300, 700, 460, 830, {'amenity': 'university', 'building': 'university', 'building:levels': '6'})
    s.rect(700, 700, 800, 800, {'amenity': 'kindergarten', 'building': 'yes', 'building:levels': '2'})
    s.rect(520, 520, 640, 640, {'amenity': 'hospital', 'building': 'yes', 'building:levels': '8'})
    s.rect(950, 500, 1050, 600, {'shop': 'mall', 'building': 'retail', 'building:levels': '3'})
    for i in range(14):
        x0 = 150 + (i % 7) * 140; y0 = 150 + (i // 7) * 420
        s.rect(x0, y0, x0 + 45, y0 + 45, {'building': 'yes', 'building:levels': str(12 + (i % 8)),
                                          'building:material': ['brick', 'panel', 'concrete', 'glass'][i % 4]})
    s.rect(150, 1000, 1250, 1120, {'landuse': 'industrial'})
    s.rect(200, 1030, 350, 1090, {'building': 'industrial', 'building:levels': '2'})
    s.rect(500, 1020, 700, 1100, {'building': 'industrial', 'building:levels': '3'})
    s.rect(900, 1030, 1100, 1110, {'building:part': 'yes', 'building:levels': '5'})
    s.save()


def metro():
    s = Scene('s6_metro')
    s.corners()
    # the subway line with turns and a diagonal (railCellChain completes the corners)
    s.line([(100, 600), (600, 600), (600, 200), (1000, 200), (1000, 800), (1300, 800)],
           {'railway': 'subway', 'tunnel': 'yes'})
    s.line([(1100, 100), (1250, 100)], {'railway': 'subway'})         # without tunnel=yes: must stay dropped
    s.rect(300, 580, 340, 620, {'building': 'yes', 'building:levels': '5'})   # a building over the tunnel
    s.line([(150, 1000), (1100, 1000)], {'railway': 'rail'})          # a surface railway for contrast
    s.line([(700, 400), (700, 950)], {'railway': 'rail', 'electrified': 'yes'})   # electrified: powered rails on straights, stone bed
    s.line([(250, 940), (250, 700), (650, 700)], {'railway': 'tram'}) # tram: always electrified, stone brick bed
    s.save()


def bench():
    # the dense speed-regression scene (~1.5 km): forests with overlapping wetlands (the overlay
    # path), bare wetland, a 360k m2 reservoir (the squid gate), a park with a pond, a river
    s = Scene('bench')
    for bx, by in [(50, 50), (1290, 50), (50, 1290), (1290, 1290)]:
        s.rect(bx, by, bx + 20, by + 20, {'building': 'yes', 'building:levels': '5'})
    for fx, fy in [(100, 100), (800, 100), (1250, 600), (100, 900), (500, 1150)]:
        s.rect(fx, fy, fx + 300, fy + 300, {'natural': 'wood'})
    s.rect(100, 100, 350, 350, {'natural': 'wetland'})
    s.rect(800, 150, 1100, 400, {'natural': 'wetland'})
    s.rect(950, 950, 1150, 1150, {'natural': 'wetland'})
    s.rect(500, 500, 700, 700, {'leisure': 'park'})
    s.rect(500, 500, 700, 700, {'natural': 'wood'})
    s.rect(560, 560, 590, 590, {'natural': 'water'})
    s.rect(200, 400, 800, 850, {'natural': 'water'})
    s.rect(1250, 1000, 1370, 1120, {'natural': 'water'})
    s.rect(600, 1000, 1300, 1080, {'natural': 'water', 'water': 'river', 'waterway': 'riverbank'})
    s.rect(900, 700, 1000, 760, {'landuse': 'farmland'})
    s.rect(300, 1300, 420, 1400, {'natural': 'water'})
    s.save()


def p1_nowet():
    # s3_water without the wetland rect (the swamp overlay control)
    s = Scene('p1_nowet')
    s.corners()
    s.rect(300, 300, 950, 900, {'natural': 'water'})
    s.rect(100, 1000, 1250, 1090, {'natural': 'water', 'water': 'river', 'waterway': 'riverbank'})
    rng = random.Random(5)
    for _ in range(8):
        x0 = rng.randint(100, 1100); y0 = rng.randint(100, 1050)
        s.rect(x0, y0, x0 + 70, y0 + 50, {'natural': 'water'})
    for px, py in [(1050, 200), (150, 950), (1000, 950)]:
        s.rect(px, py, px + 50, py + 25, {'leisure': 'swimming_pool'})
    s.line([(60, 600), (1250, 600)], {'highway': 'secondary', 'lanes': '4', 'surface': 'asphalt'})
    s.line([(625, 60), (625, 1140)], {'waterway': 'canal'})
    s.save()


def p2_swamp(size, name):
    # bare wetland scaling: ~10k / 90k / 250k columns
    s = Scene(name)
    s.corners()
    s.rect(400, 400, 400 + size, 400 + size, {'natural': 'wetland'})
    s.save()


def p4_floor():
    # the empty-world floor: two corner buildings and nothing else
    s = Scene('p4_floor')
    s.corners()
    s.save()


def p5_small():
    # the tiny-world control (~150x150 m): the fixed cost of the ground laps
    s = Scene('p5_small')
    for bx, by in [(50, 50), (150, 50), (50, 150), (150, 150)]:
        s.rect(bx, by, bx + 20, by + 20, {'building': 'yes', 'building:levels': '5'})
    s.rect(80, 80, 130, 130, {'natural': 'wetland'})
    s.save()


SCENES = {'s1_city': city, 's2_forest': forest, 's3_water': water, 's4_roads': roads,
          's5_structures': structures, 's6_metro': metro, 'bench': bench,
          'p1_nowet': p1_nowet, 'p2a_swamp10k': lambda: p2_swamp(100, 'p2a_swamp10k'),
          'p2b_swamp90k': lambda: p2_swamp(300, 'p2b_swamp90k'),
          'p2c_swamp250k': lambda: p2_swamp(500, 'p2c_swamp250k'),
          'p4_floor': p4_floor, 'p5_small': p5_small}

if sys.argv[1:2] == ['--list'] or not sys.argv[1:]:
    print('scenes:', ' '.join(SCENES))
    sys.exit(0 if sys.argv[1:2] == ['--list'] else 2)
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
for name in (sys.argv[2:] or list(SCENES)):
    SCENES[name]()
print('done')
