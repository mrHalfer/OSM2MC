# -*- coding: utf-8 -*-
"""A test of the spawn point choice: --spawn-lonlat must shift the spawn in level.dat by the expected distance.

Builds a world from data/osm_buildings.osm twice (the spawn to the west/east by 0.001° of longitude) and compares the difference of X blocks
with meters (cos of the latitude). Run: py -3 test_spawn.py   Exit 0 — PASS.
"""
import gzip, math, os, sys, zipfile
import tcfg, nbtlite
from tmc_run import run_tmc

LAT, LON, D = 51.661, 39.2682, 0.001   # near the center of the test OSM data


def spawn_of(zpath):
    with zipfile.ZipFile(zpath) as z:
        name = [n for n in z.namelist() if n.endswith('level.dat')][0]
        nbt = nbtlite.read_nbt(gzip.decompress(z.read(name)))
    data = nbt.get('Data', nbt)
    return list(data['spawn']['pos'])


def main():
    osm = os.path.join(tcfg.DATA, 'osm_buildings.osm')
    res = []
    for tag, lon in (('w', LON - D), ('e', LON + D)):
        out = os.path.join(tcfg.WORLDS, 'spawn_%s.zip' % tag)
        code, log, sec = run_tmc([osm, '--out', out, '--seed', '777', '--no-enhancer', '--no-greenery',
                                  '--spawn-lonlat', '%.6f %.6f' % (lon, LAT)],
                                 os.path.join(tcfg.WORLDS, 'spawn_%s.log' % tag))
        if code != 0 or not os.path.exists(out):
            print('FAIL: export %s failed\n' % tag, log[-1500:]); return 1
        res.append(spawn_of(out))
        print('spawn %s: %s (%.1f c)' % (tag, res[-1], sec))
    dx = res[1][0] - res[0][0]
    want = 2 * D * 111320.0 * math.cos(math.radians(LAT))
    ok = abs(dx - want) <= 0.03 * want + 60   # the spawn shifts because of buildings up to 48 blocks
    print('%s difference by X: %d blocks, expected about %.0f' % ('ok ' if ok else 'BAD', dx, want))
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
