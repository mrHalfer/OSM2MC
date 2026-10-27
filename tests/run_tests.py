# -*- coding: utf-8 -*-
"""A single entry point for running the tMC tests and generating test worlds.

  py -3 run_tests.py                 fast tests: osm (+ shp, if there is data)
  py -3 run_tests.py osm shp         the chosen stages
  py -3 run_tests.py all             all stages, including the harness and servers (long: ~15-30 min)

Stages:
  osm      a synthetic OSM -> building classification (pass 1 and 2), the fallback Pale Garden, 3 world variants
  shp      the "folder with shape files" mode on real data (TMC_TEST_SHP, optional)
  harness  building the C++ harness, 6 worlds mc_test*.zip, the full verify_mc_export.py (RESULT PASS is needed)
  servers  real Minecraft servers 26.3 and 1.21.11 open the tMC worlds (osm_buildings*.zip) and the harness noise worlds (smoke_mc.py)

The worlds lie in tests\\worlds (zip + .log), the servers — tests\\servers. The result: a PASS/FAIL/SKIP table, the exit code 0 only if there is no FAIL.
"""
import os, re, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, 'tools')
sys.path.insert(0, TOOLS)
import tcfg


def run(cmd, cwd=None, env=None, timeout=7200):
    t0 = time.time()
    p = subprocess.run(cmd, cwd=cwd or TOOLS, env=env or tcfg.tool_env(), timeout=timeout,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
    return p.returncode, p.stdout, time.time() - t0


def py(*a):
    return [sys.executable] + list(a)


def stage_osm():
    rc1, o1, t1 = run(py(os.path.join(TOOLS, 'test_osm.py')))
    rc2, o2, t2 = run(py(os.path.join(TOOLS, 'test_enhancer.py')))
    rc3, o3, t3 = run(py(os.path.join(TOOLS, 'test_spawn.py')))
    rc4, o4, t4 = run(py(os.path.join(TOOLS, 'test_address.py')))
    return (0 if rc1 == 0 and rc2 == 0 and rc3 == 0 and rc4 == 0 else 1), o1 + o2 + o3 + o4, t1 + t2 + t3 + t4


def stage_shp():
    return run(py(os.path.join(TOOLS, 'test_shp.py')))


def stage_harness():
    out = ''
    rc, o, t = run(['cmd', '/c', os.path.join(HERE, 'harness', 'build_harness.bat')])
    out += o
    if 'ALL-OK' not in o:
        return 1, out, t
    rc, o, t2 = run(py(os.path.join(TOOLS, 'run_test.py')))
    out += o
    if 'EXIT 0' not in o:
        return 1, out, t + t2
    rc, o, t3 = run(py(os.path.join(TOOLS, 'verify_mc_export.py')))
    out += o
    ok = 'RESULT PASS' in o
    return (0 if ok else 1), out, t + t2 + t3


def stage_servers():
    out, ok, total = '', True, 0.0
    jobs = []
    for which, name in (('26', 'osm_buildings.zip'), ('121', 'osm_buildings_legacy.zip')):
        z = os.path.join(tcfg.WORLDS, name)
        if os.path.exists(z):
            jobs.append((py(os.path.join(TOOLS, 'server_check.py'), which, z, '--name', 'osm' + which), 'server ' + which + ' ' + name))
    for which, name in (('26', 'mc_test_noise.zip'), ('121', 'mc_test_legacy_noise.zip')):
        z = os.path.join(tcfg.WORLDS, name)
        if os.path.exists(z):
            jobs.append((py(os.path.join(TOOLS, 'smoke_mc.py'), which, z), 'smoke ' + which + ' ' + name))
    if not jobs:
        return 77, 'no worlds: first the stages osm / harness', 0.0
    for cmd, title in jobs:                          # strictly one after another: the common port 25565
        rc, o, t = run(cmd)
        out += '--- %s: rc=%d\n%s\n' % (title, rc, o[-1500:])
        ok = ok and rc == 0
        total += t
    return (0 if ok else 1), out, total


STAGES = {'osm': stage_osm, 'shp': stage_shp, 'harness': stage_harness, 'servers': stage_servers}


def main():
    want = [a for a in sys.argv[1:] if not a.startswith('-')]
    if not want:
        want = ['osm', 'shp']
    elif want == ['all']:
        want = ['osm', 'shp', 'harness', 'servers']
    verbose = '-v' in sys.argv
    results = []
    for name in want:
        if name not in STAGES:
            sys.exit('unknown stage %s (available: %s, all)' % (name, ', '.join(STAGES)))
        print('>>> %s ...' % name, flush=True)
        rc, out, sec = STAGES[name]()
        status = 'SKIP' if rc == 77 else ('PASS' if rc == 0 else 'FAIL')
        if status == 'FAIL' or verbose:
            print(out[-6000:])
        results.append((name, status, sec))
        print('    %s (%.0f c)' % (status, sec), flush=True)
    print('\n==== RESULT ====')
    for name, status, sec in results:
        print('  %-8s %-5s %6.0f c' % (name, status, sec))
    return 1 if any(s == 'FAIL' for _, s, _ in results) else 0


if __name__ == '__main__':
    sys.exit(main())
