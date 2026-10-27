# -*- coding: utf-8 -*-
# A check on a 26.x server: a position inside a trial chambers piece of the cave template is really "in the structure" (as for the Trial(s) Edition advancement).
import os, shutil, sys, zipfile, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_mc_server as R
import verify_mc_export as V

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mc_test_noise.zip')
zf = zipfile.ZipFile(src)
inside = None
for cx, cz, ch in V.chunk_iter(zf):
    st = ((ch.get('structures') or {}).get('starts') or {}).get('minecraft:trial_chambers')
    if st and st.get('Children'):
        for c in st['Children']:
            bb = c['BB']
            if bb[3] - bb[0] >= 6 and bb[5] - bb[2] >= 6:
                inside = ((bb[0] + bb[3]) // 2, (bb[1] + bb[4]) // 2, (bb[2] + bb[5]) // 2)
                break
    if inside: break
print('probe point', inside)
world = os.path.join(R.BASE, 'world_portal')
shutil.rmtree(world, ignore_errors=True)
os.makedirs(world)
zf.extractall(world)
dp = os.path.join(world, 'datapacks', 'probe')
os.makedirs(os.path.join(dp, 'data', 'probe', 'predicate'), exist_ok=True)
open(os.path.join(dp, 'pack.mcmeta'), 'w').write('{"pack":{"description":"probe","min_format":82,"max_format":121}}')
json.dump({"type": "minecraft:location_check", "predicate": {"structures": "minecraft:trial_chambers"}},
          open(os.path.join(dp, 'data', 'probe', 'predicate', 'trial.json'), 'w'))
R.prep()
x, y, z = inside
log = R.run('world_portal', ['forceload add %d %d %d %d' % (x - 20, z - 20, x + 20, z + 20),
                             'execute positioned %d %d %d if predicate probe:trial run say INSIDE_TRIAL' % (x, y, z),
                             'execute positioned %d %d %d unless predicate probe:trial run say OUTSIDE_TRIAL' % (x + 3000, y, z + 3000),
                             'execute positioned %d %d %d if predicate probe:trial run say BAD_FAR' % (x + 3000, y, z + 3000)])
for l in log:
    if any(k in l for k in ('INSIDE_TRIAL', 'OUTSIDE_TRIAL', 'BAD_FAR', 'rror', 'xception', 'nknown')):
        print(l)
