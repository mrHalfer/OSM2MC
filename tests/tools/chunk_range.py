# The actual chunk coordinate range of a tMC world zip (the world size check:
# compare the range, not the number of region files - one region file covers 32x32 chunks).
import os, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_mc_export as V

zf = zipfile.ZipFile(sys.argv[1])
xs, zs = [], []
for cx, cz, ch in V.chunk_iter(zf):
    xs.append(cx)
    zs.append(cz)
if not xs:
    print('no chunks')
    sys.exit(1)
print('chunks:', len(xs), ' x:', min(xs), '...', max(xs), '  z:', min(zs), '...', max(zs))
print('size: %d x %d chunks = %d x %d blocks' % (max(xs) - min(xs) + 1, max(zs) - min(zs) + 1,
                                                 (max(xs) - min(xs) + 1) * 16, (max(zs) - min(zs) + 1) * 16))
