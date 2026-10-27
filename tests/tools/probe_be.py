# -*- coding: utf-8 -*-
# A check on a 26.x server: block entities from the template (vault, trial_spawner, dispenser...) are loaded and carry real data (data get block).
import os, shutil, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_mc_server as R
import verify_mc_export as V

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mc_test_noise.zip')
zf = zipfile.ZipFile(src)
want = {'minecraft:vault': None, 'minecraft:trial_spawner': None, 'minecraft:dispenser': None}
for cx, cz, ch in V.chunk_iter(zf):
    for b in ch.get('block_entities') or []:
        i = b.get('id')
        if i in want and want[i] is None:
            want[i] = (b['x'], b['y'], b['z'])
    if all(want.values()): break
print(want)
world = os.path.join(R.BASE, 'world_portal')
shutil.rmtree(world, ignore_errors=True)
os.makedirs(world)
zf.extractall(world)
R.prep()
cmds = []
for i, p in want.items():
    if p: cmds += ['forceload add %d %d %d %d' % (p[0] - 8, p[2] - 8, p[0] + 8, p[2] + 8), 'data get block %d %d %d' % p]
log = R.run('world_portal', cmds)
for l in log:
    if 'block data' in l or 'rror' in l or 'xception' in l or 'nknown' in l:
        print(l[:400])
