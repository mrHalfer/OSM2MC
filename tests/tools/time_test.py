# Timing of mc_export_test.exe (4 exports) with PATH to Qt, as in run_test.py.
import os, subprocess, time
HERE = os.path.dirname(os.path.abspath(__file__))
import tcfg
env = tcfg.tool_env()
t = time.time()
p = subprocess.run([os.path.join(HERE, 'mc_export_test.exe')], cwd=HERE,
                   capture_output=True, timeout=600, env=env)
print('sec', round(time.time() - t, 2), 'exit', p.returncode)
