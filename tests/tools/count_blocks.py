# Block instance counts in exported world zips.
# Usage: python count_blocks.py <world.zip> [...] [name-part ...]
# The trailing arguments are lowercase name parts to filter by (e.g. log leaves lantern);
# without a filter the whole block list is printed, most frequent first.
import os, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_mc_export as V

args = sys.argv[1:]
zips = [a for a in args if a.endswith('.zip')]
parts = tuple(a for a in args if not a.endswith('.zip'))
if not zips:
    print('usage: count_blocks.py <world.zip> [...] [name-part ...]')
    sys.exit(2)

for f_ in zips:
    cnt = {}
    for cx, cz, ch in V.chunk_iter(zipfile.ZipFile(f_)):
        for s in ch.get('sections') or []:
            bs = s.get('block_states') or {}
            names = [((e.get('Name') or e.get('id') or '') if isinstance(e, dict) else str(e))
                     for e in bs.get('palette') or []]
            data = bs.get('data')
            if data is None:
                n0 = names[0] if names else ''
                if not parts or any(p in n0 for p in parts):
                    cnt[n0] = cnt.get(n0, 0) + 4096
                continue
            pal = [(n, {}) for n in names]
            for i in range(4096):
                n = V.decode(pal, data, i & 15, i >> 8 & 15, i >> 4 & 15)
                if n and n != 'minecraft:air' and (not parts or any(p in n for p in parts)):
                    cnt[n] = cnt.get(n, 0) + 1
    print(f_)
    for k, c in sorted(cnt.items(), key=lambda kv: -kv[1]):
        print(f'  {k}: {c}')
