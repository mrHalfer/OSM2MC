# One-off style check of chunk BIOME palettes in an exported zip or in an unpacked save folder.
# Usage: python verify_biomes.py <world.zip | save folder> [biome]   (default minecraft:swamp)
# Prints the biome histogram (chunks per biome) and the chunks containing the wanted biome
# (with a spawn hint for the first such block). Exit code 0 when the biome is present, 1 otherwise.
import os, struct, sys, zipfile, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_mc_export as V


def cells_with(pal, data, want):
    # counts the 4x4x4 cells of a section mapped to the wanted biome (the writer uses no minimum bits)
    if want not in pal:
        return 0
    idx = pal.index(want)
    if data is None:
        return 64
    bits = max(1, (len(pal) - 1).bit_length())
    per, mask = 64 // bits, (1 << bits) - 1
    return sum(1 for j in range(64) if (data[j // per] >> ((j % per) * bits)) & mask == idx)


def scan_chunks(iter3):
    hits = {}            # biome name -> chunk count
    per_chunk = []       # (cx, cz, wanted-cell-count)
    total = 0
    for cx, cz, ch in iter3:
        total += 1
        wc = 0
        names = set()
        for s in ch.get('sections') or []:
            b = s.get('biomes') or {}
            pal = b.get('palette') or []
            wc += cells_with(pal, b.get('data'), WANT)
            names.update(pal)
        for nm in names:
            hits[nm] = hits.get(nm, 0) + 1        # one count per chunk, not per section
        if wc:
            per_chunk.append((cx, cz, wc))
    return total, hits, per_chunk


def iter_save_folder(save):
    # an unpacked world: every region/ subfolder has r.X.Z.mca with the same chunk scheme
    regdirs = []
    for root, dirs, files in os.walk(save):
        if 'region' in dirs:
            p = os.path.join(root, 'region')
            if p not in regdirs:
                regdirs.append(p)
    print('region dirs:', regdirs)
    for p in regdirs:
        for n in sorted(os.listdir(p)):
            if not n.endswith('.mca'):
                continue
            m = open(os.path.join(p, n), 'rb').read()
            rx, rz = map(int, n[2:-4].split('.'))
            for slot in range(1024):
                loc = struct.unpack('>I', m[slot * 4:slot * 4 + 4])[0]
                if not loc:
                    continue
                so = loc >> 8
                size = struct.unpack('>I', m[so * 4096:so * 4096 + 4])[0]
                data = m[so * 4096 + 5:so * 4096 + size + 4]
                try:
                    data = zlib.decompress(data) if data[:2] != b'\x1f\x8b' else __import__('gzip').decompress(data)
                    ch = V.parse(data)
                except Exception:
                    continue                       # a padding/failed chunk
                yield (slot % 32) + (rx << 5), (slot // 32) + (rz << 5), ch


path = sys.argv[1]
WANT = sys.argv[2] if len(sys.argv) > 2 else 'minecraft:swamp'
if os.path.isdir(path):
    total, hits, per_chunk = scan_chunks(iter_save_folder(path))
elif zipfile.is_zipfile(path):
    total, hits, per_chunk = scan_chunks(V.chunk_iter(zipfile.ZipFile(path)))
else:
    print('not a zip and not a folder:', path)
    sys.exit(2)

print('chunks scanned:', total)
print('biome names seen (chunk count):')
for nm, c in sorted(hits.items(), key=lambda kv: -kv[1]):
    print(f'  {nm}: {c}')
print(f'chunks containing {WANT}: {len(per_chunk)}')
if per_chunk:
    xs = [c[0] for c in per_chunk]
    zs = [c[1] for c in per_chunk]
    x0, x1, z0, z1 = min(xs) * 16, max(xs) * 16 + 15, min(zs) * 16, max(zs) * 16 + 15
    print(f'wanted-biome area: x {x0}..{x1}, z {z0}..{z1} (spawn at x={x0 + 8}, z={z0 + 8} to see)')
    for cx, cz, wc in sorted(per_chunk)[:20]:
        print(f'  chunk ({cx},{cz}) wanted cells: {wc}')
    sys.exit(0)
sys.exit(1)
