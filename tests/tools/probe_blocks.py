# -*- coding: utf-8 -*-
# A live probe: a fresh world from a zip, forceload + execute if block. Arguments:
#   probe_blocks.py 26|121 <world> <zip> "cmd1;cmd2;..."
import os, shutil, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
which, world, src = sys.argv[1], sys.argv[2], sys.argv[3]
cmds = [c.strip() for c in ' '.join(sys.argv[4:]).split(';') if c.strip()]
if which == '26':
    import run_mc_server as S
    S.prep()
    wd = os.path.join(S.BASE, world)
    if os.path.isdir(wd):
        shutil.rmtree(wd)
    os.makedirs(wd)
    with zipfile.ZipFile(src) as z:
        z.extractall(wd)
else:
    import run_mc121_server as S
    S.prep(os.path.join(S.BASE, world), src)
log = S.run(world, cmds)
hits = [l for l in log if 'Test passed' in l or 'Test failed' in l or 'forceload' in l.lower() and 'Chunk' in l]
print('=== PROBE LINES ===')
for l in hits:
    print(l)
print('=== errors ===')
for l in log:
    ll = l.lower()
    if any(k in ll for k in ('error', 'exception', 'fail', 'could not', 'invalid')):
        print(l)
