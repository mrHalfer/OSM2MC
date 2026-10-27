# -*- coding: utf-8 -*-
"""show_zebra.py <zip> [block=minecraft:white_carpet] [window_by_X=15] [window_by_Z=15] [max_groups=4]

Finds in a world (the 26.x format) the blocks of the given kind (by default a white carpet — marking/a zebra), groups them by proximity
and draws a "top view" map around each group at the carpet height: W — the sought block, c — another carpet, g — under the cell there is gray wool/concrete
(a roadway without a carpet), . — other. Needed to check by eye that the zebra lies exactly on the roadway.
"""
import sys, zipfile, collections
import tcfg
sys.path.insert(0, tcfg.TOOLS)
from verify_mc_export import chunk_iter, decode


def load(zf):
    """chunks[(cx, cz)][secY] = (entries, data)"""
    chunks = {}
    for cx, cz, ch in chunk_iter(zf):
        secs = {}
        for s in ch.get('sections') or []:
            bs = s.get('block_states', {})
            entries = []
            for e in bs.get('palette') or []:
                if isinstance(e, dict):
                    entries.append((e.get('Name') or e.get('id') or e.get(''), None))
                else:
                    entries.append((e, None))
            secs[s.get('Y')] = (entries, bs.get('data'))
        chunks[(cx, cz)] = secs
    return chunks


def block_at(chunks, x, y, z):
    secs = chunks.get((x >> 4, z >> 4))
    if not secs:
        return None
    sec = secs.get(y >> 4)
    if not sec:
        return 'minecraft:air'
    entries, data = sec
    if not entries:
        return 'minecraft:air'
    return decode(entries, data, x & 15, y & 15, z & 15)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    zf = zipfile.ZipFile(sys.argv[1])
    target = sys.argv[2] if len(sys.argv) > 2 else 'minecraft:white_carpet'
    wx = int(sys.argv[3]) if len(sys.argv) > 3 else 15
    wz = int(sys.argv[4]) if len(sys.argv) > 4 else 15
    maxg = int(sys.argv[5]) if len(sys.argv) > 5 else 4
    only_small = '--zebra' in sys.argv      # only groups of width <= 4 and length <= 20 (crossings)
    sys.argv = [a for a in sys.argv if a != '--zebra']
    chunks = load(zf)
    cells = []
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            names = [e[0] for e in entries]
            if target not in names:
                continue
            for i in range(4096):
                lx, lz, ly = i & 15, (i >> 4) & 15, (i >> 8) & 15
                if decode(entries, data, lx, ly, lz) == target:
                    cells.append((cx * 16 + lx, sy * 16 + ly, cz * 16 + lz))
    print('%s: %d blocks' % (target, len(cells)))
    # groups: connectivity by distance <= 3 cells
    seen, groups = set(), []
    byxz = collections.defaultdict(list)
    for c in cells:
        byxz[(c[0] // 4, c[2] // 4)].append(c)
    for c in cells:
        if c in seen:
            continue
        stack, grp = [c], []
        seen.add(c)
        while stack:
            a = stack.pop()
            grp.append(a)
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for b in byxz.get((a[0] // 4 + dx, a[2] // 4 + dz), []):
                        if b not in seen and abs(b[0] - a[0]) <= 3 and abs(b[2] - a[2]) <= 3:
                            seen.add(b)
                            stack.append(b)
        groups.append(grp)
    if only_small:
        def small(g):
            xs = [c[0] for c in g]; zs = [c[2] for c in g]
            a, b = max(xs) - min(xs) + 1, max(zs) - min(zs) + 1
            return min(a, b) <= 4 and max(a, b) <= 20 and len(g) >= 8
        groups = [g for g in groups if small(g)]
    groups.sort(key=len, reverse=True)
    print('groups: %d (drawing the first %d)' % (len(groups), min(maxg, len(groups))))
    for grp in groups[:maxg]:
        xs = [c[0] for c in grp]; zs = [c[2] for c in grp]; ys = collections.Counter(c[1] for c in grp)
        y = ys.most_common(1)[0][0]
        cx0, cx1, cz0, cz1 = min(xs) - 3, max(xs) + 3, min(zs) - 3, max(zs) + 3
        print('--- group: %d blocks, x %d..%d, z %d..%d, y %d' % (len(grp), min(xs), max(xs), min(zs), max(zs), y))
        if cx1 - cx0 > wx * 3 or cz1 - cz0 > wz * 3:
            print('    (the group is big — the map is cropped)')
            cx1, cz1 = min(cx1, cx0 + wx * 3), min(cz1, cz0 + wz * 3)
        for z in range(cz0, cz1 + 1):
            row = ''
            for x in range(cx0, cx1 + 1):
                b = block_at(chunks, x, y, z)
                under = block_at(chunks, x, y - 1, z) or ''
                if b == target:
                    row += 'W'
                elif b and b.endswith('_carpet'):
                    row += 'c'
                elif 'gray_wool' in under or 'gray_concrete' in under:
                    row += 'g'
                else:
                    row += '.'
            print('    ' + row)


if __name__ == '__main__':
    main()
