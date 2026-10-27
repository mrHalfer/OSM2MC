# -*- coding: utf-8 -*-
"""A server check of a world: a real Minecraft opens a tMC save.

  py -3 server_check.py 26|121 <zip> [--area x0 z0 x1 z1] [--name name]

PASS: the server came up ("Done ("), forceload loaded the chunks, the console answers, there are no world loading errors in the log.
--area — the block coordinates of the box for forceload (up to 256 chunks); without it the middle of the biggest region file is taken.
The servers 26.3 and 1.21.11 use one port — run strictly one after another. Logs: tests\\servers\\<mc26|mc121>\\server_<name>.log
"""
import os, re, shutil, subprocess, sys, threading, time, zipfile
import tcfg

BAD = ['failed to load', 'failed to handle', 'encountered an unexpected exception', 'a fatal error', 'could not load', 'unable to load']


def default_area(zf):
    best, size = None, -1
    for i in zf.infolist():
        m = re.search(r'region/r\.(-?\d+)\.(-?\d+)\.mca$', i.filename)
        if m and i.file_size > size and '/entities/' not in i.filename and '/poi/' not in i.filename:
            best, size = (int(m.group(1)), int(m.group(2))), i.file_size
    if best is None:
        return None
    rx, rz = best
    x0, z0 = rx * 512 + 192, rz * 512 + 192          # a box of 8×8 chunks in the middle of the region
    return x0, z0, x0 + 127, z0 + 127


def server_check(which, zip_path, area=None, name=None):
    base = tcfg.SERVER_DIR[which]
    world = name or ('chk' + which)
    with open(os.path.join(base, 'eula.txt'), 'w') as f:
        f.write('eula=true\n')
    with open(os.path.join(base, 'server.properties'), 'w') as f:
        f.write('online-mode=false\nlevel-type=minecraft\\:flat\nview-distance=3\nspawn-protection=0\nmotd=tmc-test\n'
                'max-players=1\nsync-chunk-writes=false\npause-when-empty-seconds=100000\n')
    wdir = os.path.join(base, world)
    if os.path.isdir(wdir):
        shutil.rmtree(wdir)
    os.makedirs(wdir)
    with zipfile.ZipFile(zip_path) as z:
        if area is None:
            area = default_area(z)
        z.extractall(wdir)
    if area is None:
        print('FAIL: there are no region files in the archive')
        return False

    log = []
    p = subprocess.Popen([tcfg.JAVA, '-Xmx2G', '-jar', 'server.jar', '--world', world, 'nogui'], cwd=base,
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding='utf-8', errors='replace', bufsize=1)
    done = threading.Event()

    def reader():
        for line in p.stdout:
            log.append(line.rstrip())
            if 'Done (' in line:
                done.set()
    threading.Thread(target=reader, daemon=True).start()

    ok_done = done.wait(420)
    if ok_done:
        for c, s in (('forceload add %d %d %d %d' % area, 25), ('forceload query', 5), ('say TMCPROBE', 3)):
            p.stdin.write(c + '\n')
            p.stdin.flush()
            time.sleep(s)
        t0 = time.time()
        while time.time() - t0 < 60 and not any('TMCPROBE' in l and '[Server]' in l for l in log):
            time.sleep(2)
    time.sleep(2)
    try:
        p.stdin.write('stop\n')
        p.stdin.flush()
    except OSError:
        pass
    try:
        p.wait(240)
    except subprocess.TimeoutExpired:
        p.kill()
    open(os.path.join(base, 'server_%s.log' % world), 'w', encoding='utf-8').write('\n'.join(log))

    probe = any('TMCPROBE' in l for l in log)
    fq = next((l for l in reversed(log) if 'force loaded chunks were found' in l), '')
    m = re.search(r'(\d+)\s+force loaded chunks', fq)
    n = int(m.group(1)) if m else 0
    bad = [l for l in log if any(b in l.lower() for b in BAD)]
    print('=== server %s: %s (box %s) ===' % (which, os.path.basename(zip_path), area))
    print('  done: %s, forceload: %d chunks, the console answers: %s' % (ok_done, n, probe))
    for l in bad[:10]:
        print('  BAD LINE:', l)
    ok = ok_done and probe and n >= 1 and not bad
    print('SERVER %s %s' % (which, 'PASS' if ok else 'FAIL'))
    return ok


if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) < 2:
        sys.exit(__doc__)
    area = None
    name = None
    if '--area' in a:
        i = a.index('--area')
        area = tuple(int(x) for x in a[i + 1:i + 5])
    if '--name' in a:
        name = a[a.index('--name') + 1]
    sys.exit(0 if server_check(a[0], a[1], area, name) else 1)
