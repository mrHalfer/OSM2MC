# -*- coding: utf-8 -*-
"""unzip_world.py <zip> <directory>: unpack a save (the directory is cleared)."""
import os, shutil, sys, zipfile
if len(sys.argv) < 3:
    sys.exit(__doc__)
dst = sys.argv[2]
if os.path.isdir(dst):
    shutil.rmtree(dst)
os.makedirs(dst)
with zipfile.ZipFile(sys.argv[1]) as z:
    z.extractall(dst)
print('unzipped', sys.argv[1], '->', dst)
