import os, subprocess, sys, threading, time, zipfile

import tcfg
BASE = tcfg.SERVER_DIR['121']
JAVA = tcfg.JAVA

def prep(world_dir, src_zip):
    os.makedirs(BASE, exist_ok=True)
    if not os.path.exists(os.path.join(BASE, 'server.jar')):
        sys.exit('No %s: put there the server.jar of a Minecraft 1.21.x server (downloaded from minecraft.net, EULA terms)' % os.path.join(BASE, 'server.jar'))
    with open(os.path.join(BASE, 'eula.txt'), 'w') as f:
        f.write('eula=true\n')
    with open(os.path.join(BASE, 'server.properties'), 'w') as f:
        f.write('online-mode=false\nlevel-type=minecraft\\:flat\nview-distance=3\nspawn-protection=0\nmotd=test\nmax-players=1\nsync-chunk-writes=false\n')
    import shutil
    if os.path.isdir(world_dir):
        shutil.rmtree(world_dir)
    os.makedirs(world_dir)
    with zipfile.ZipFile(src_zip) as z:
        z.extractall(world_dir)

def run(world_name, commands):
    log = []
    p = subprocess.Popen(
        [JAVA, '-Xmx1G', '-XX:+UseSerialGC', '-jar', 'server.jar', '--world', world_name, 'nogui'],
        cwd=BASE, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding='utf-8', errors='replace', bufsize=1)
    done = threading.Event()
    def reader():
        for line in p.stdout:
            log.append(line.rstrip())
            if 'Done (' in line:
                done.set()
    t = threading.Thread(target=reader, daemon=True)
    t.start()
    if done.wait(420):
        for c in commands:
            p.stdin.write(c + '\n'); p.stdin.flush()
            time.sleep(8)
    time.sleep(5)
    try:
        p.stdin.write('stop\n'); p.stdin.flush()
    except OSError:
        pass
    try:
        p.wait(180)
    except subprocess.TimeoutExpired:
        p.kill()
    with open(os.path.join(BASE, f'server_{world_name}.log'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(log))
    return log

if __name__ == '__main__':
    world = sys.argv[1] if len(sys.argv) > 1 else 'testlegacy121'
    src = sys.argv[2] if len(sys.argv) > 2 else os.path.join(tcfg.WORLDS, 'mc_test_legacy.zip')
    # everything after the zip is ONE command line (joined), optionally separated by ';'
    cmds = ' '.join(sys.argv[3:]).split(';') if len(sys.argv) > 3 else ['forceload add -1 -1 2 2']
    cmds = [c.strip() for c in cmds if c.strip()]
    prep(os.path.join(BASE, world), src)
    log = run(world, cmds)
    print('=== total lines', len(log))
    print('=== TAIL 30 ===')
    print('\n'.join(log[-30:]))
    print('=== KEY LINES ===')
    for l in log:
        ll = l.lower()
        if any(k in ll for k in ['error', 'exception', 'data version', 'fixing', 'fail', 'upgrad', 'could not', 'invalid']):
            print(l)
