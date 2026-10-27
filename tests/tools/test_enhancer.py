# -*- coding: utf-8 -*-
"""A test of the enhancer (the fallback type/height of buildings from .dat). The DB is not needed: the .dat is built here by tests/tools/tmcb.py.

1) format: the header and the number of records are read;
2) priorities when exporting OSM (the TMC_OSM_DEBUG log):
   - a building=yes building without signs gets the TYPE from the enhancer (and without it — by neighbors/area);
   - a building with a type tag (building=apartments) keeps the OSM type, even if there is a point of another type nearby;
   - height: the height tag > building:levels (formulas) > the enhancer data > the heights.csv table;
   - private sector codes from the database are reduced to 44400002.
"""
import importlib.util, json, math, os, re, sys
import tcfg
from tmc_run import run_tmc

import tmcb as be                                   # the file format helpers (writing and reading the .dat)

RULES = os.path.join(tcfg.RESOURCES, 'osm_rules.json')
lat0, lon0 = 51.66, 39.20
mx = 111320.0 * math.cos(math.radians(lat0)); my = 111320.0


def latlon(x, y):
    return lat0 + y / my, lon0 + x / mx


def main():
    ok = True

    # --- 2/3. file and export ---
    osm_nodes, osm_ways, nid, wid = [], [], [0], [100]
    ids = {}

    def rect(name, x, y, w, h, tags):
        pts = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
        refs = []
        for px, py in pts:
            nid[0] += 1
            la, lo = latlon(px, py)
            osm_nodes.append('<node id="%d" lat="%.7f" lon="%.7f"/>' % (nid[0], la, lo))
            refs.append(nid[0])
        refs.append(refs[0])
        wid[0] += 1
        ids[name] = str(wid[0])
        osm_ways.append('<way id="%d">%s%s</way>' % (wid[0], ''.join('<nd ref="%d"/>' % r for r in refs),
                                                      ''.join('<tag k="%s" v="%s"/>' % kv for kv in tags.items())))

    # spaced by >60 m so that neighbors (nb:*) do not affect
    rect('A', 0, 0, 20, 20, {'building': 'yes'})                                              # the type only from the enhancer (a school 44312000, 15 m)
    rect('B', 200, 0, 20, 20, {'building': 'apartments'})                                     # the OSM type, no height in OSM -> db:rel
    rect('C', 400, 0, 20, 20, {'building': 'yes'})                                            # a private sector code from the database -> 44400002
    rect('D', 600, 0, 20, 20, {'building': 'apartments', 'building:levels': '9'})            # height by floors, db:rel (99 m) is ignored
    rect('E', 800, 0, 20, 20, {'building': 'yes', 'height': '14 m'})                         # height from OSM is more important than db:rel; the type from the enhancer (shopping center)
    rect('F', 1000, 0, 20, 20, {'building': 'yes'})                                          # no point nearby -> a heuristic (not from the enhancer)
    rect('G', 1200, 0, 20, 20, {'building': 'office'})                                        # the OSM type (general purpose), in the database only the floor count 5 -> a formula
    osm = os.path.join(tcfg.WORLDS, 'enhancer_test.osm')
    open(osm, 'w', encoding='utf-8').write("<?xml version='1.0' encoding='UTF-8'?>\n<osm version='0.6'>\n" + '\n'.join(osm_nodes) + '\n' + '\n'.join(osm_ways) + '\n</osm>\n')

    # the points of the file — in the building center
    def rec(x, y, code, rel_m, floor):
        la, lo = latlon(x + 10, y + 10)
        return (int(round(la * 1e6)), int(round(lo * 1e6)), code, int(round(rel_m * 10)), floor)
    recs = [rec(0, 0, 44312000, 15.0, 4), rec(200, 0, 44318000, 12.5, 3), rec(400, 0, 44400100, 0, 0), rec(600, 0, 44310000, 99.0, 30),
            rec(800, 0, 44318000, 33.0, 6), rec(1200, 0, 44310000, 0, 5)]
    dat = os.path.join(tcfg.WORLDS, 'enhancer_test.dat')
    n, cells = be.write_dat(dat, recs)
    info = be.read_info(dat)
    good = info[0] == 1 and info[3] == len(recs)
    print('  %s file: version %d, records %d (expected %d), cells %d' % ('ok ' if good else 'BAD', info[0], info[3], len(recs), cells))
    ok = ok and good

    # --- export: with and without the enhancer ---
    def run(extra, name):
        code, log, sec = run_tmc([osm, '--osm-rules', RULES, '--dry-run'] + extra, os.path.join(tcfg.WORLDS, name + '.log'), env_extra={'TMC_OSM_DEBUG': '1'})
        res, rel = {}, {}
        for m in re.finditer(r'\[osm (\d+)\][^\n]*-> bldpol (\d+)', log):
            res[m.group(1)] = m.group(2)                       # the last line = the result after the 2nd pass
        for m in re.finditer(r'\[rel (\d+)\] CODE=\d+ REL=([\d.]+)', log):
            rel[m.group(1)] = float(m.group(2))
        return code, log, res, rel

    code, log, res, rel = run(['--enhancer', dat, '--no-greenery'], 'enhancer_on')
    if code != 0:
        print('FAIL: the export failed\n', log[-1500:])
        return 1
    exp_type = {'A': '44312000', 'B': '44400000', 'C': '44400002', 'D': '44400000', 'E': '44318000', 'G': '44310000'}
    for k, want in exp_type.items():
        have = res.get(ids[k])
        good = have == want
        print('  %s type %s: expected %s, got %s' % ('ok ' if good else 'BAD', k, want, have))
        ok = ok and good
    # F: no point nearby -> not from the enhancer (the heuristic will give something from residential/general purpose), the main thing — not 44312000/44318000
    good = ids['F'] in res and res[ids['F']] not in ('44312000', '44318000')
    print('  %s type F without a point in the database: %s (heuristic)' % ('ok ' if good else 'BAD', res.get(ids['F'])))
    ok = ok and good
    exp_rel = {'A': 15.0, 'B': 12.5, 'D': 29.7, 'E': 14.0}          # A: db:rel; B: db:rel; D: 9 floors * 3.3; E: height from OSM
    for k, want in exp_rel.items():
        have = rel.get(ids[k], -1)
        good = abs(have - want) < 0.25
        print('  %s height %s: expected %.1f, got %.1f' % ('ok ' if good else 'BAD', k, want, have))
        ok = ok and good
    # G: the OSM type office, in the database only the floor count 5 -> the formula for 44310000: 5*4 = 20
    have = rel.get(ids['G'], -1)
    good = abs(have - 20.0) < 0.25
    print('  %s height G (the floor count 5 from the database by the formula): expected 20.0, got %.1f' % ('ok ' if good else 'BAD', have))
    ok = ok and good

    # without the enhancer the same buildings do not get types from the database
    code, log2, res2, rel2 = run(['--no-enhancer', '--no-greenery'], 'enhancer_off')
    good = code == 0 and res2.get(ids['A']) != '44312000' and res2.get(ids['E']) != '44318000'
    print('  %s without the enhancer the types from the database are not applied (A=%s, E=%s)' % ('ok ' if good else 'BAD', res2.get(ids['A']), res2.get(ids['E'])))
    ok = ok and good

    print('TEST enhancer', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
