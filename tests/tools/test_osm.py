# -*- coding: utf-8 -*-
"""A test of the tMC OSM source.

1) generates data/osm_buildings.osm (+the expectations) and builds a world through tMC with the rules osm_rules.buildings.draft.json;
2) compares the code of each building in the log (TMC_OSM_DEBUG=1) with the expected one — pass 1 (tags, POI, plots) and pass 2 (neighbors, area);
3) checks the fallback Pale Garden ("an empty site was allocated"), the code summary and the object count;
4) builds the same data in the 1.21 format and in a superflat world (they must export without errors).

Run: py -3 test_osm.py        Exit 0 — PASS.
"""
import json, os, re, subprocess, sys
import tcfg
from tmc_run import run_tmc

RULES = os.path.join(tcfg.RESOURCES, 'osm_rules.json')


def main():
    subprocess.check_call([sys.executable, os.path.join(tcfg.DATA, 'gen_osm_test.py')], stdout=subprocess.DEVNULL)
    osm = os.path.join(tcfg.DATA, 'osm_buildings.osm')
    exp_all = json.load(open(os.path.join(tcfg.DATA, 'osm_buildings.expected.json'), encoding='utf-8'))
    expected, exp_layers = exp_all['buildings'], exp_all['layers']
    ok = True

    out = os.path.join(tcfg.WORLDS, 'osm_buildings.zip')
    code, log, sec = run_tmc([osm, '--osm-rules', RULES, '--out', out, '--seed', '777', '--no-enhancer', '--no-greenery'],
                             os.path.join(tcfg.WORLDS, 'osm_buildings.log'), env_extra={'TMC_OSM_DEBUG': '1'})
    print('tMC exit %d, %.1f s, zip %s' % (code, sec, os.path.getsize(out) if os.path.exists(out) else 'NONE'))
    if code != 0 or not os.path.exists(out):
        print('FAIL: the export failed\n', log[-1500:])
        return 1

    # the final code of each building = the last line of the log with its osm_id
    got = {}
    for m in re.finditer(r'\[osm (\d+)\][^\n]*-> ([^\n]*)', log):
        res = m.group(2).strip()
        got[m.group(1)] = None if res.startswith('(none)') else res.split()[-1]
    bad = 0
    for oid, want in sorted(expected.items(), key=lambda kv: int(kv[0])):
        have = got.get(oid, '<not in the log>')
        mark = 'ok ' if have == want else 'BAD'
        if have != want:
            bad += 1
            ok = False
        print('  %s osm %s: expected %s, got %s' % (mark, oid, want, have))
    print('buildings: %d checked, %d errors' % (len(expected), bad))

    def need(pattern, title):
        nonlocal ok
        hit = re.search(pattern, log) is not None
        print('  %s %s' % ('ok ' if hit else 'BAD', title))
        ok = ok and hit

    # a regression: non-building objects (plots, forests, parking lots) must not get into the building layer
    nb = sum(1 for v in expected.values() if v is not None)
    m = re.search(r'Layer osm\.bldpol: (\d+) objects', log)
    cnt = int(m.group(1)) if m else -1
    print('  %s in the bldpol layer exactly %d buildings (got %d)' % ('ok ' if cnt == nb else 'BAD', nb, cnt))
    ok = ok and cnt == nb

    # the other layers: the object count and the code summary must match the expectations
    for layer, codes in sorted(exp_layers.items()):
        total = sum(codes.values())
        m = re.search(r'Layer osm\.%s: (\d+) objects\s+codes: ([^\n]*)' % layer, log)
        have = {}
        if m:
            for part in m.group(2).split('; '):
                k, _, v = part.partition(' × ')
                have[k.strip()] = int(v)
        good = m is not None and int(m.group(1)) == total and have == {k: v for k, v in codes.items()}
        print('  %s layer %s: expected %s, got %s' % ('ok ' if good else 'BAD', layer, codes, have if m else 'no layer'))
        ok = ok and good

    need(r'zebra on the roads: crossings 2 of 2 \(cells [1-9]\d*\)', 'zebra: both crossings (the line and the node) lay on the roadway')
    need(r'equipment on sports grounds: [1-9]\d* items', 'playgrounds and sports grounds got equipment (csrpol without height)')
    need(r'an empty site of 15×15 chunks', 'the fallback Pale Garden (there is no vegetation in the data)')
    need(r'Buildings without signs: \d+, of which recognized by neighbors and area: \d+', 'the summary of the 2nd pass')
    need(r'codes: .*44400000 × \d+', 'the layer code summary')
    need(r'creaking hearts: \d+', 'the Pale Garden is built (creaking hearts)')
    need(r'Objects: \d+ \(skipped: 0\)', 'all objects were exported')

    for title, extra, name in (('1.21.x', ['--format', '1.21'], 'osm_buildings_legacy'), ('superflat', ['--flat'], 'osm_buildings_flat')):
        o2 = os.path.join(tcfg.WORLDS, name + '.zip')
        c2, l2, s2 = run_tmc([osm, '--osm-rules', RULES, '--out', o2, '--seed', '777', '--no-enhancer', '--no-greenery'] + extra, os.path.join(tcfg.WORLDS, name + '.log'))
        good = c2 == 0 and os.path.exists(o2) and 'Error' not in l2
        print('  %s %s: exit %d, %.1f c' % ('ok ' if good else 'BAD', title, c2, s2))
        ok = ok and good

    print('TEST osm', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
