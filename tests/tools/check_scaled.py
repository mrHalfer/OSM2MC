# -*- coding: utf-8 -*-
# A check of "map units -> meters": mc_test_scaled.zip (scale 0.5) against mc_test.zip (scale 1).
# Horizontal sizes must decrease twice: the chunk count is ~4 times smaller (with a margin for margins),
# heights do not change (the tallest building is the same).
import sys, os, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V

def stats(path):
    zf = zipfile.ZipFile(path)
    n = 0
    top = -999
    for cx, cz, ch in V.chunk_iter(zf):
        n += 1
        for s in ch.get('sections') or []:
            pal = (s.get('block_states') or {}).get('palette') or []
            if len(pal) > 1:
                top = max(top, s['Y'])
    return n, top

n1, t1 = stats(os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip'))
n2, t2 = stats(os.path.join(__import__('tcfg').WORLDS, 'mc_test_scaled.zip'))
ok = n2 < n1 * 0.45 and abs(t1 - t2) <= 1
print('chunks: scale 1 = %d, scale 0.5 = %d (want < 45%%), the top section %d / %d (want equal) -> %s'
      % (n1, n2, t1, t2, 'OK' if ok else 'MISS'))

# --- spawn at the view point (map x0+5, y0+5 -> block ((x0+5-cx0)*0.5, (cy0-(y0+5))*0.5) = (42, 2)) ---
import gzip
def spawn_of(path):
    zf = zipfile.ZipFile(path)
    root = V.parse(gzip.decompress(zf.read('level.dat')))
    d = root.get('Data', root)
    sp = d.get('spawn')
    if isinstance(sp, dict) and 'pos' in sp:
        return tuple(sp['pos'])
    return (d.get('SpawnX'), d.get('SpawnY'), d.get('SpawnZ'))
sp = spawn_of(os.path.join(__import__('tcfg').WORLDS, 'mc_test_scaled.zip'))
okSp = sp[0] is not None and abs(sp[0] - 42) <= 1 and abs(sp[2] - 2) <= 1
print('spawn (scale 0.5, the view point = the middle of the first cube): %s (want x≈42, z≈2) -> %s' % (sp, 'OK' if okSp else 'MISS'))
sys.exit(0 if (ok and okSp) else 1)
