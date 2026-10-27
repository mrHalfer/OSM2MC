import os, shutil, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_mc_server as R
import verify_mc_export as V
zf = zipfile.ZipFile('mc_test_noise.zip')
pos = None
for e in V.ent_iter(zf):
    for en in e.get('Entities') or []:
        if en.get('id') == 'minecraft:cat':
            pos = [int(v) for v in en['Pos']]; break
    if pos: break
print('cat at', pos)
world = os.path.join(R.BASE, 'world_portal')
shutil.rmtree(world, ignore_errors=True)
os.makedirs(world)
zf.extractall(world)
R.prep()
x, y, z = pos
log = R.run('world_portal', ['forceload add %d %d %d %d' % (x - 16, z - 16, x + 16, z + 16),
                             'execute positioned %d %d %d run data get entity @e[type=minecraft:cat,distance=..8,limit=1] variant' % (x, y, z),
                             'execute positioned %d %d %d run data get entity @e[type=minecraft:wolf,limit=1,sort=nearest] Pos' % (x, y, z)])
for l in log:
    if 'variant' in l or 'entity data' in l or 'rror' in l or 'xception' in l or 'Found no' in l or 'following' in l:
        print(l[:300])
