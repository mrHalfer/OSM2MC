# -*- coding: utf-8 -*-
# A server smoke test of noise (cave) archives — a replacement for their decode verification (a user
# decision: "there is nothing to look for in dungeons by decoding — check with the server that the world
# opens and works"). Two versions: 26.3 (a native zip) and 1.21.11 (a legacy zip).
#
# PASS criteria:
#   1) the server came up ("Done (" in the log) — level.dat/the world structure are valid;
#   2) forceload loaded the city, the wheat field and the meadow (the most "stuffed" chunks:
#      buildings + caves under them) — region files are readable, chunks do not fall off;
#   3) the console is alive (say SMOKEPROBE answered) AND there is no hole at the bottom y=-64:
#      execute if air -> say SMOKEHOLE. The bottom of a noise world is soil/cave templates, NOT
#      bedrock (the exporter writes bedrock only into flat world_gen_settings), so
#      we check "not air", not a specific block;
#   4) there is no "Failed to load level / Failed to handle / unexpected exception" in the log;
#   5) display mannequins (armor_stand entities) are loaded by the server from entities/*.mca and
#      at least one armor slot is non-empty (for 1.21.11 this is also a check that the format of
#      the slots {id,count} and the empty {} are read by it without "Failed to load entity");
#   6) the minecraft:spawner block at a deterministic cell is alive (there are no named *_spawner
#      blocks in the game — the former export was silently turned into air by the server).
#
# Run (the zip paths — full or in testsworlds):
#   py -3 smoke_mc.py 26  mc_test_noise.zip
#   py -3 smoke_mc.py 121 mc_test_legacy_noise.zip
# Logs: <server>\server_smok<N>.log. The servers use one port — do NOT run in parallel.
import os, shutil, subprocess, sys, threading, time, zipfile

import tcfg
JAVA = tcfg.JAVA
SERVERS = {   # the base folder : jar (lies in tests\servers\*\server.jar)
    '26':  (tcfg.SERVER_DIR['26'], os.path.join(tcfg.SERVER_DIR['26'], 'server.jar')),
    '121': (tcfg.SERVER_DIR['121'], os.path.join(tcfg.SERVER_DIR['121'], 'server.jar')),
}
HERE = tcfg.WORLDS

# forceload add has a LIMIT of 256 chunks per command (System chat "Too many chunks") —
# all boxes are <= 256; in total we load the city (3 boxes), the wheat (2) and the meadow.
CMDS = [
    ('forceload add -70 -220 120 0',    20),   # the city, the west half (168)
    ('forceload add -70 0 120 200',     20),   # the city, the east half (156)
    ('forceload add 120 -220 320 -10',  20),   # the city, the north-east (182)
    ('forceload add 120 -10 320 200',   20),   # the city, the south-east, the shopping center (182)
    ('forceload add -520 -90 -350 209', 20),   # the wheat, the west (209)
    ('forceload add -350 -90 -171 209', 20),   # the wheat, the east (228)
    ('forceload add 280 -110 519 109',  20),   # the meadow (224)
    ('forceload add -170 -215 -70 -95',  20),   # the field of csrpol sites (56)
    ('forceload query',                   5),  # «N force loaded chunks were found ...»
    ('say SMOKEPROBE',                    2),  # the console is responsive
    ('execute if block 0 -64 0 minecraft:air run say SMOKEHOLE', 3),  # is the bottom holey?
    # Shopping center mannequins = armor_stand entities (a deterministic noise seed: the cells 284,-197 and
    # 286,-202 fall into the "north-east"). The first command — the fact of loading entities from
    # entities/*.mca; the second — "at least one armor slot is NOT empty" (the pattern [{},{},{},{}] =
    # all slots are empty; the inversion ! = a display in armor). data get from the 26.x console is silent,
    # the nbt selector answers deterministically on both versions.
    ('execute if entity @e[type=minecraft:armor_stand,limit=1] run say SMOKEARMOR', 8),
    ('execute if entity @e[type=minecraft:armor_stand,nbt=!{ArmorItems:[{},{},{},{}]}] run say SMOKEARMORW', 6),
    # A monster spawner (a deterministic position in the high-rise 44400000 of the noise world): an
    # ORDINARY minecraft:spawner block (there are no named zombie/skeleton_spawner in the game — the server
    # turned them into air, a user remark). if block proves that the server did NOT
    # throw the block out; the type (skeleton) lies in the BE and is checked by the verify decode.
    ('execute if block 172 209 -102 minecraft:spawner run say SMOKESPAWN', 4),
    # The table top = oak_pressure_plate ON a fence — the block is placed bypassing the placement
    # rules (a plate above a fence cannot be placed by hand!). if block proves that the
    # server did not throw the block out on loading (plates have no self-detachment without support — but
    # we check it by measurement, not on faith). The cell is deterministic (bigPriv, tier noise=70).
    ('execute if block 242 70 43 minecraft:oak_pressure_plate run say SMOKEPLATE', 4),
]
BAD = ['failed to load', 'failed to handle', 'encountered an unexpected exception',
       'a fatal error', 'could not load', 'unable to load']

def smoke(tag, zip_path, world):
    base, jar = SERVERS[tag]
    with open(os.path.join(base, 'eula.txt'), 'w') as f:
        f.write('eula=true\n')
    with open(os.path.join(base, 'server.properties'), 'w') as f:
        f.write('online-mode=false\nlevel-type=minecraft\\:flat\nview-distance=3\n'
                'spawn-protection=0\nmotd=test\nmax-players=1\nsync-chunk-writes=false\n'
                'pause-when-empty-seconds=100000\n')
    wdir = os.path.join(base, world)
    if os.path.isdir(wdir):
        shutil.rmtree(wdir)
    os.makedirs(wdir)
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(wdir)

    log = []
    p = subprocess.Popen(
        [JAVA, '-Xmx2G', '-jar', 'server.jar', '--world', world, 'nogui'],
        cwd=base, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
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
        for c, s in CMDS:
            p.stdin.write(c + '\n')
            p.stdin.flush()
            time.sleep(s)
        t0 = time.time()
        need = ('SMOKEPROBE', 'SMOKEARMOR', 'SMOKEARMORW', 'SMOKESPAWN', 'SMOKEPLATE')
        while time.time() - t0 < 120 and not all(any(n in l for l in log) for n in need):
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
    with open(os.path.join(base, f'server_{world}.log'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(log))

    have = lambda sub: any(sub in l for l in log)
    probe, hole = have('SMOKEPROBE'), have('SMOKEHOLE')
    # Mannequins/spawner: SMOKEARMOR — armor_stand loaded; the armor id in the ArmorItems dump —
    # "a display sometimes in armor"; SMOKESPAWN — the server saved the minecraft:spawner block (not air).
    armor, spawner = have('SMOKEARMOR'), have('SMOKESPAWN')
    armorId = have('SMOKEARMORW')
    plate = have('SMOKEPLATE')
    # The query reply: "N force loaded chunks were found in minecraft:overworld at: ..."
    flq = next((l for l in reversed(log) if 'force loaded chunks were found' in l), '')
    import re
    m = re.search(r'(\d+)\s+force loaded chunks', flq)
    flqn = int(m.group(1)) if m else 0
    badlines = [l for l in log if any(b in l.lower() for b in BAD)]
    print(f'=== smoke {tag} ({os.path.basename(zip_path)} -> {world}) ===')
    print('  done:      ', ok_done)
    print('  forceload: ', flqn, 'chunks (want >= 1000) ', ('' if flq else '— NO REPLY to query'))
    print('  probe/hole:', probe, hole)
    print('  armor_stand:', armor, '| armor id in the log:', armorId, '| spawner block:', spawner,
          '| plate on a fence:', plate)
    for l in badlines:
        print('  BAD LINE:  ', l)
    ok = (ok_done and probe and not hole and flqn >= 1000 and not badlines
          and armor and armorId and spawner and plate)
    print('SMOKE', tag, 'PASS' if ok else 'FAIL')
    return ok

if __name__ == '__main__':
    a = [x for x in sys.argv[1:]]
    tag = a[0] if a else '26'
    zp = a[1] if len(a) > 1 else ('mc_test_noise.zip' if tag == '26' else 'mc_test_legacy_noise.zip')
    if not os.path.isabs(zp):
        zp = os.path.join(HERE, zp)
    sys.exit(0 if smoke(tag, zp, 'smok' + tag) else 1)
