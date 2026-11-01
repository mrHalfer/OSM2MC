# Climate-zone probe: counts of the zone tree blocks + a histogram of vertical trunk runs.
# Usage: python scan_zone.py world.zip [log ...]      (default logs: oak_log, dark_oak_log)
# The trunk-run length shows the tree species that really grew in the zone (tall trunks = mature
# oaks, short runs = birch/park trees). Prints the counts of the usual zone blocks for comparison.
import os, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_mc_export as V

TARGETS = [t if t.startswith('minecraft:') else 'minecraft:' + t for t in sys.argv[2:]] \
    or ['minecraft:oak_log', 'minecraft:dark_oak_log']
COUNTS = ['minecraft:oak_log', 'minecraft:oak_leaves', 'minecraft:dark_oak_log', 'minecraft:dark_oak_leaves',
          'minecraft:pale_oak_log', 'minecraft:spruce_log', 'minecraft:spruce_leaves', 'minecraft:birch_log']
WATCH = set(COUNTS) | set(TARGETS)

counts = {n: 0 for n in WATCH}
col_y = {}   # (name, x, z) -> set of y for the target logs
zf = zipfile.ZipFile(sys.argv[1])
for cx, cz, ch in V.chunk_iter(zf):
    for s in ch.get('sections', []):
        if not isinstance(s, dict):
            continue
        bs = s.get('block_states', {})
        names = []
        for e in bs.get('palette') or []:
            if isinstance(e, dict):
                names.append(e.get('Name') or e.get('id') or str(e))
            else:
                names.append(str(e))
        if not any(n in WATCH for n in names):
            continue
        y0 = int(s['Y']) * 16
        data = bs.get('data')
        if data is None:
            counts[names[0]] += 4096      # whole section is one block; no per-column runs for a uniform section
            continue
        bits = max(4, (len(names) - 1).bit_length())
        per, mask = 64 // bits, (1 << bits) - 1
        for i in range(4096):
            v = (data[i // per] >> ((i % per) * bits)) & mask
            if v >= len(names):
                continue
            nm = names[v]
            if nm not in WATCH:
                continue
            counts[nm] += 1
            if nm in TARGETS:
                x = cx * 16 + (i & 15)
                z = cz * 16 + ((i >> 4) & 15)
                col_y.setdefault((nm, x, z), set()).add(y0 + (i >> 8))

print('== counts ==')
for nm in COUNTS:
    print('%-32s %d' % (nm, counts.get(nm, 0)))
for nm in TARGETS:
    if nm not in COUNTS:
        print('%-32s %d' % (nm, counts.get(nm, 0)))
print('== trunk runs (consecutive vertical column) ==')
for t in TARGETS:
    hist = {}
    run8 = 0
    for (nm, x, z), ys in col_y.items():
        if nm != t:
            continue
        ys = sorted(ys)
        run = 1
        for a, b in zip(ys, ys[1:]):
            if b == a + 1:
                run += 1
            else:
                hist[run] = hist.get(run, 0) + 1
                if run >= 8:
                    run8 += 1
                run = 1
        hist[run] = hist.get(run, 0) + 1
        if run >= 8:
            run8 += 1
    print(t, 'run-length histogram:', dict(sorted(hist.items())), '| runs >= 8:', run8)
