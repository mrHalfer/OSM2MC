# -*- coding: utf-8 -*-
# A check on a real 26.x server: /locate structure minecraft:ruined_portal finds the frame in a forest (the structure start + the tqt_lairs datapack).
import os, shutil, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_mc_server as R

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mc_test.zip')
world = os.path.join(R.BASE, 'world_portal')
shutil.rmtree(world, ignore_errors=True)
os.makedirs(world)
with zipfile.ZipFile(src) as z:
    z.extractall(world)
R.prep()
log = R.run('world_portal', ['locate structure minecraft:trial_chambers', 'locate structure minecraft:mineshaft', 'locate structure minecraft:ancient_city', 'locate structure minecraft:stronghold', 'locate structure minecraft:ruined_portal', 'locate structure minecraft:pillager_outpost'])
for l in R.tail(log, ['nearest', 'could not find', 'error', 'exception', 'failed', 'unknown', 'invalid', 'block entity', 'structure']):
    print(l)
