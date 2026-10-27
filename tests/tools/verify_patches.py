# -*- coding: utf-8 -*-
# Bandit outposts and abandoned camps (patch_*.bin): chests with loot, the spawn zone, lava puddles in the pale forest are lined with stone.
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V


def check(path):
    zf = zipfile.ZipFile(path)
    camp = outp = starts = 0
    for cx, cz, ch in V.chunk_iter(zf):
        for b in ch.get('block_entities') or []:
            t = str(b.get('LootTable', ''))
            if 'abandoned_camp' in t: camp += 1
            if t.endswith('chests/pillager_outpost'): outp += 1
        if 'pillager_outpost' in str((ch.get('structures') or {}).get('starts', '')): starts += 1
    good = camp >= 1 and outp >= 1 and starts >= 1
    print('  [%s] outposts/camps: camp chests %d, outpost %d (want >= 1), raider spawn zones in chunks %d (want >= 1)'
          % ('OK ' if good else 'MISS', camp, outp, starts))
    return good


if __name__ == '__main__':
    sys.exit(0 if check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test_noise.zip')) else 1)
