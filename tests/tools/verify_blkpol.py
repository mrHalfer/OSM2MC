# -*- coding: utf-8 -*-
# blkpol: a square (CODE 45300000) is laid with stone bricks along the surface (air above the block), asphalt 62231000 — gray concrete.
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V


def check(path):
    zf = zipfile.ZipFile(path)
    top = 0
    for cx, cz, ch in V.chunk_iter(zf):
        secs = {s['Y']: s for s in ch.get('sections') or []}
        for y, s in secs.items():
            bs = s['block_states']
            nm = [e if isinstance(e, str) else (e.get('id') or e.get('Name') or e.get('')) for e in bs['palette']]
            if 'minecraft:stone_bricks' not in nm or y < 0: continue
            d = bs.get('data')
            if d is None: continue
            bits = max(4, (len(nm) - 1).bit_length()); per = 64 // bits; gi = nm.index('minecraft:stone_bricks')
            above = secs.get(y)
            for i in range(4096):
                if (d[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1) != gi: continue
                ly = i >> 8
                if ly < 15:
                    j = i + 256
                    n2 = nm[(d[j // per] >> ((j % per) * bits)) & ((1 << bits) - 1)]
                    if n2 == 'minecraft:air': top += 1
    good = top >= 400
    print('  [%s] blkpol square 45300000: stone bricks on the surface %d (want >= 400 of 572)' % ('OK ' if good else 'MISS', top))
    return good


if __name__ == '__main__':
    sys.exit(0 if check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')) else 1)
