import os, subprocess, sys, threading, time, zipfile

import tcfg
BASE = tcfg.SERVER_DIR['26']
JAVA = tcfg.JAVA

def prep():
    os.makedirs(BASE, exist_ok=True)
    with open(os.path.join(BASE, 'eula.txt'), 'w') as f:
        f.write('eula=true\n')
    with open(os.path.join(BASE, 'server.properties'), 'w') as f:
        f.write('online-mode=false\nlevel-type=minecraft\\:flat\nview-distance=3\nspawn-protection=0\nmotd=test\nmax-players=1\nsync-chunk-writes=false\n')

def unzip_export(world_dir):
    os.makedirs(world_dir, exist_ok=True)
    with zipfile.ZipFile(os.path.join(tcfg.WORLDS, 'mc_test.zip')) as z:
        z.extractall(world_dir)

def run(world_name, commands):
    """Start server for a world, send commands once 'Done' appears, then stop. Returns log tail."""
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

    if done.wait(300):
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

def tail(log, keywords):
    out = []
    for l in log:
        ll = l.lower()
        if any(k in ll for k in keywords):
            out.append(l)
    return out

if __name__ == '__main__':
    prep()
    world = sys.argv[1]
    cmds = sys.argv[2:] if len(sys.argv) > 2 else ['forceload add 0 0 3 3']
    if world == 'testlegacy':
        unzip_export(os.path.join(BASE, 'testlegacy'))
    log = run(world, cmds)
    print('=== total lines', len(log))
    print('=== TAIL 40 ===')
    print('\n'.join(log[-40:]))
    print('=== KEY LINES (error|except|version|fix|fail|chunk) ===')
    for l in tail(log, ['error', 'exception', 'data version', 'fixing', 'fail', 'upgrad', 'could not', 'invalid']):
        print(l)
