# -*- coding: utf-8 -*-
# A check on a 26.x server: the shipwreck chest loaded with a LootTable, the kelp is in place. Usage: py -3 probe_wreck.py lake_real.zip
import os, shutil, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_mc_server as R
import verify_mc_export as V
zf = zipfile.ZipFile(sys.argv[1] if len(sys.argv) > 1 else 'lake_real.zip')
chest = None
for cx, cz, ch in V.chunk_iter(zf):
    for be in ch.get('block_entities') or []:
        if 'shipwreck' in str(be.get('LootTable', '')) and chest is None:
            chest = (be['x'], be['y'], be['z'])
print('chest', chest)
world = os.path.join(R.BASE, 'world_portal')
shutil.rmtree(world, ignore_errors=True)
os.makedirs(world)
zf.extractall(world)
R.prep()
x, y, z = chest
log = R.run('world_portal', ['forceload add %d %d %d %d' % (x - 24, z - 24, x + 24, z + 24),
                             'data get block %d %d %d LootTable' % (x, y, z),
                             'execute positioned %d %d %d run fill ~-20 ~-14 ~-20 ~20 ~14 ~20 minecraft:air replace minecraft:kelp' % (x, y, z)])
for l in log:
    if 'LootTable' in l or 'block data' in l or 'Successfully' in l or 'filled' in l.lower() or 'rror' in l or 'xception' in l:
        print(l[:300])
