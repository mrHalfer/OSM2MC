# -*- coding: utf-8 -*-
# A check of the export with terrain (mc_test_relief.zip): valleys are not filled up to the "plane", the spawn stands on the surface.
import gzip, os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V


def surface_y(ch, lx, lz):
    for s in sorted(ch.get('sections') or [], key=lambda s: -s['Y']):
        bs = s['block_states']
        names = [e if isinstance(e, str) else (e.get('id') or e.get('Name') or e.get('')) for e in bs['palette']]
        if len(names) == 1 and names[0] == 'minecraft:air':
            continue
        d = bs.get('data')
        bits = max(4, (len(names) - 1).bit_length())
        per = 64 // bits
        for ly in range(15, -1, -1):
            if d is None:
                n = names[0]
            else:
                i = (ly << 8) | (lz << 4) | lx
                n = names[(d[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1)]
            if n != 'minecraft:air':
                return s['Y'] * 16 + ly, n
    return None


def check(path):
    zf = zipfile.ZipFile(path)
    ys = []
    chunks = {}
    blendN = 0
    for cx, cz, ch in V.chunk_iter(zf):
        if 'sections' not in ch:
            continue
        chunks[(cx, cz)] = ch
        if 'blending_data' in ch:
            blendN += 1
        r = surface_y(ch, 3, 3)
        if r and r[1] == 'minecraft:grass_block':
            ys.append(r[0])
    ok = True
    def rep(good, msg):
        nonlocal ok
        print('  [%s] %s' % ('OK ' if good else 'MISS', msg))
        ok &= good
    lo, hi = (min(ys), max(ys)) if ys else (0, 0)
    ys.sort()
    p10 = ys[len(ys) // 10] if ys else 0
    rep(len(ys) > 200 and hi - lo >= 40, 'terrain: turf from y=%d to y=%d over %d chunks (want a spread >= 40 — valleys are kept)' % (lo, hi, len(ys)))
    rep(p10 < hi - 20, 'valleys are not filled up to the plane: 10%% of chunks below y=%d at a maximum of %d' % (p10, hi))
    # smoothing the border with the ungenerated world: chunks at the export edge get blending_data written (the vanilla blending mechanism)
    borderN = 0
    for (cx, cz) in chunks:
        if any((cx + dx, cz + dz) not in chunks for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            borderN += 1
    rep(borderN > 0 and blendN == borderN, 'blending_data on edge chunks: %d of %d edge ones (want all)' % (blendN, borderN))
    # spawn: above the ground (in the spawn column there is a solid block below it, at its level and above — air)
    lv = V.parse(gzip.decompress(zf.read('level.dat')))['Data']
    sp = lv['spawn']['pos']
    ch = chunks.get((sp[0] >> 4, sp[2] >> 4))
    if ch:
        r = surface_y(ch, sp[0] & 15, sp[2] & 15)
        rep(r is not None and r[0] < sp[1], 'spawn (%d,%d,%d) above the column surface (top y=%s)' % (sp[0], sp[1], sp[2], r[0] if r else '?'))
    else:
        rep(False, 'the spawn chunk is in the archive')
    return ok


if __name__ == '__main__':
    sys.exit(0 if check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test_relief.zip')) else 1)
