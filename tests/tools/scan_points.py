# Counts and Y extents of marker blocks of the POINT structures (step 3.575) in an exported zip.
# Usage: python scan_points.py <world.zip> [marker ...]
# The default markers cover the structures of point_models.csv (fountain water, tower, composter...).
# Prints per marker: block count (with block properties in the key) and the min/max Y.
import os, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_mc_export as V

WANT = sys.argv[2:] or ['composter', 'quartz_block', 'iron_block', 'vine', 'gold_block', 'stone_pressure_plate',
                        'water{level=8}', 'lantern', 'mossy_cobblestone', 'polished_blackstone',
                        'cobblestone_wall', 'stone_stairs', 'spruce_log']
zf = zipfile.ZipFile(sys.argv[1])


def pal_keys(palette):
    # 'name' or 'name{prop=value,...}' - the same key form the markers are written with
    out = []
    for e in palette:
        if isinstance(e, str):
            out.append(e)
        elif 'Name' in e:
            k = e['Name']
            pr = e.get('Properties')
            if isinstance(pr, dict) and pr:
                k += '{' + ','.join('%s=%s' % (a, b) for a, b in sorted(pr.items())) + '}'
            out.append(k)
        else:
            out.append(str(e))
    return out


cnt, yext, n_chunks = {}, {}, 0
for cx, cz, ch in V.chunk_iter(zf):
    n_chunks += 1
    for s in [q for q in ch.get('sections') or [] if isinstance(q, dict)]:
        bs = s.get('block_states')
        if not isinstance(bs, dict):
            continue
        names = pal_keys(bs['palette'])
        data = bs.get('data')
        y0 = int(s['Y']) * 16

        def hit(nm):
            return any(w in nm for w in WANT)

        if data is None:
            if hit(names[0]):
                cnt[names[0]] = cnt.get(names[0], 0) + 4096
                yext.setdefault(names[0], [y0, y0 + 15])
            continue
        idx = [i for i, n in enumerate(names) if hit(n)]
        if not idx:
            continue
        keys = [names[i] for i in idx]
        kset = set(keys)
        bits = max(4, (len(names) - 1).bit_length())
        per, mask = 64 // bits, (1 << bits) - 1
        for w_i, word in enumerate(data):
            for b in range(per):
                v = (word >> (b * bits)) & mask
                if v >= len(names):
                    continue
                nm = names[v]
                if nm in kset:
                    cnt[nm] = cnt.get(nm, 0) + 1
                    y = y0 + ((w_i * per + b) >> 8)
                    e = yext.setdefault(nm, [y, y])
                    e[0] = min(e[0], y)
                    e[1] = max(e[1], y)
print('chunks:', n_chunks)
if not cnt:
    print('no markers found')
for k in sorted(cnt):
    lo, hi = yext.get(k, (0, 0))
    print('%-55s %d   (y %d..%d)' % (k, cnt[k], lo, hi))
