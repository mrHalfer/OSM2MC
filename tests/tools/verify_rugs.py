# -*- coding: utf-8 -*-
# Carpet compositions of residential high-rises: connected carpet areas >= 9 cells of at least two colors (a border / a checkerboard / a path), not 2×2.
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V


def check(path):
    zf = zipfile.ZipFile(path)
    carp = {}
    for cx, cz, ch in V.chunk_iter(zf):
        for s in ch.get('sections') or []:
            bs = s['block_states']
            names = [e if isinstance(e, str) else (e.get('id') or e.get('Name') or e.get('')) for e in bs['palette']]
            if not any(n and n.endswith('_carpet') for n in names):
                continue
            d = bs.get('data')
            bits = max(4, (len(names) - 1).bit_length())
            per = 64 // bits
            for i in range(4096):
                n = names[0] if d is None else names[(d[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1)]
                if n and n.endswith('_carpet'):
                    carp[(cx * 16 + (i & 15), s['Y'] * 16 + (i >> 8), cz * 16 + ((i >> 4) & 15))] = n
    seen = set()
    big = 0
    for p in carp:
        if p in seen:
            continue
        st = [p]
        seen.add(p)
        c = []
        while st:
            q = st.pop()
            c.append(q)
            for d in ((1, 0, 0), (-1, 0, 0), (0, 0, 1), (0, 0, -1)):
                n = (q[0] + d[0], q[1], q[2] + d[2])
                if n in carp and n not in seen:
                    seen.add(n)
                    st.append(n)
        if len(c) >= 9 and len({carp[x] for x in c}) >= 2:
            big += 1
    good = big >= 3
    print('  [%s] carpet compositions of residential houses: connected carpets of 2+ colors from 9 cells %d (want >= 3)' % ('OK ' if good else 'MISS', big))
    return good


if __name__ == '__main__':
    sys.exit(0 if check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')) else 1)
