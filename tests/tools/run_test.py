# -*- coding: utf-8 -*-
# Running mc_export_test.exe from this folder + a report: exit, zips, stderr/stdout.
import os, subprocess, glob, sys
import tcfg
HERE = tcfg.WORLDS
env = tcfg.tool_env()   # without Qt5Core.dll the exe crashes before main
for f in glob.glob(os.path.join(HERE, 'mc_test*.zip')):
    os.remove(f)
p = subprocess.run([os.path.join(tcfg.BUILD, 'mc_export_test.exe'), HERE], cwd=tcfg.BUILD,
                   capture_output=True, timeout=600, env=env)
print('EXIT', p.returncode)
print('ZIPS', [(os.path.basename(z), os.path.getsize(z)) for z in glob.glob(os.path.join(HERE, 'mc_test*.zip'))])
print('STDERR', p.stderr[:3000].decode('utf-8', 'replace'))
print('STDOUT', p.stdout[:500].decode('utf-8', 'replace'))
