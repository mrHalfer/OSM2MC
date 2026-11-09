# -*- coding: utf-8 -*-
"""Small ruins of the game world (ruined portals) cut out of a REAL generated world into patch_*.bin.

  py -3 extract_ruins.py gen [--server 121] [--seed 12081] [--world ruinsgen]
      Starts the server on a fresh noise world, finds ruined portals with locate (execute positioned at
      different points - the further ones), forceloads a square around each of them, save-all, stop.
      The found coordinates are saved to <world>/ruins_found.json.

  py -3 extract_ruins.py extract [--server 121] [--world ruinsgen] [--out ../../resources/templates]
      OFFLINE (no server): reads region/*.mca of that world, finds the portal blocks near every located point
      (the connected component of the structure blocks), cuts it out together with the terrain and writes a
      PGD2 file (the same format as extract_pale_garden.py: the hd top of each column, the surf surface block,
      the above-terrain cells (ady, palette), chests as BE with LootTable, the bb frame of the structure).
      Names: patch_ruins_portal1.bin ...

The block cells and BEs are relative to the terrain top hd of the column (exactly as paledonor.cpp expects:
y = tn + ady); hd itself is absolute (the exporter subtracts hdRef of the reference column).
"""
import argparse, json, os, re, shutil, struct, subprocess, sys, threading, time, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tcfg
from verify_mc_export import parse          # NBT reader (verified on both servers)

JAVA = tcfg.JAVA
SERVERS = tcfg.SERVER_DIR

# -------------------------------------------------------------------------------- generation

STRUCTURES = ['minecraft:ruined_portal']
LOCATE_POINTS = [(0, 0), (1200, 900), (-1500, 1200), (2600, -2200), (-2700, 2500), (4200, 3800), (-4200, -3600),
                 (6000, -800), (-6000, -1200), (1800, 5200), (-2400, -5400), (7500, 2200), (-7200, 4200)]
MARGIN = 40                                                     # forceload around the found point, blocks

def gen(server, seed, world):
    base = SERVERS[server]
    wdir = os.path.join(base, world)
    if os.path.isdir(wdir):
        shutil.rmtree(wdir)
    os.makedirs(wdir)
    with open(os.path.join(base, 'eula.txt'), 'w') as f:
        f.write('eula=true\n')
    with open(os.path.join(base, 'server.properties'), 'w') as f:
        f.write('online-mode=false\nlevel-seed=%d\ngenerate-structures=true\nview-distance=4\n'
                'spawn-protection=0\nmotd=ruins\nmax-players=1\nsync-chunk-writes=false\n'
                'pause-when-empty-seconds=100000\n' % seed)
    log = []
    p = subprocess.Popen([JAVA, '-Xmx3G', '-jar', 'server.jar', '--world', world, 'nogui'],
                         cwd=base, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding='utf-8', errors='replace', bufsize=1)
    done = threading.Event()
    def reader():
        for line in p.stdout:
            log.append(line.rstrip())
            if 'Done (' in line:
                done.set()
    threading.Thread(target=reader, daemon=True).start()
    def say(cmd, sec):
        p.stdin.write(cmd + '\n'); p.stdin.flush(); time.sleep(sec)
    found = []
    try:
        if not done.wait(420):
            raise RuntimeError('server did not start')
        for (px, pz) in LOCATE_POINTS:
            for s in STRUCTURES:
                say('execute positioned %d 64 %d run locate structure %s' % (px, pz, s), 10)
        for line in log:
            m = re.search(r'The nearest (minecraft:[a-z_]+) is at \[(-?\d+), ~, (-?\d+)\]', line)
            if m and m.group(1) in STRUCTURES:
                rec = {'id': m.group(1), 'x': int(m.group(2)), 'z': int(m.group(3))}
                if rec not in found:
                    found.append(rec)
        for fnd in found:
            say('forceload add %d %d %d %d' % (fnd['x'] - MARGIN, fnd['z'] - MARGIN, fnd['x'] + MARGIN, fnd['z'] + MARGIN), 15)
        say('forceload query', 4)
        say('save-all', 12)
        say('save-all flush', 20)
    finally:
        try:
            p.stdin.write('stop\n'); p.stdin.flush()
        except OSError:
            pass
        try:
            p.wait(240)
        except subprocess.TimeoutExpired:
            p.kill()
    open(os.path.join(base, 'server_ruins.log'), 'w', encoding='utf-8').write('\n'.join(log))
    with open(os.path.join(wdir, 'ruins_found.json'), 'w', encoding='utf-8') as f:
        json.dump(found, f, indent=1)
    for fnd in found:
        print('  %s at %d %d' % (fnd['id'], fnd['x'], fnd['z']))
    if not found:
        print('!! nothing located (see server_ruins.log)')

# -------------------------------------------------------------------------------- offline reading

class WorldZF:
    """A zipfile-like view of the world folder for verify_mc_export.chunk_iter (only region/*.mca)."""
    def __init__(self, wdir):
        self.files = {}
        for sub in ('region', 'DIM-1/region', 'DIM1/region'):
            d = os.path.join(wdir, sub.replace('/', os.sep))
            if os.path.isdir(d):
                for fn in os.listdir(d):
                    if fn.endswith('.mca'):
                        self.files[sub + '/' + fn] = os.path.join(d, fn)
    def namelist(self):
        return list(self.files)
    def read(self, name):
        data = open(self.files[name], 'rb').read()
        return data + b'\0' * (4096 - len(data)) if len(data) < 4096 else data

def pal_entry(e):
    """A palette entry (both 1.21 {Name,Properties} and 26.x {id,properties} and plain string) -> (name, props dict)."""
    if isinstance(e, str):
        return e, {}
    if isinstance(e, dict):
        nm = e.get('Name') or e.get('id') or 'minecraft:air'
        pr = e.get('Properties') or e.get('properties') or {}
        return nm, {k: str(v) for k, v in pr.items()}
    return 'minecraft:air', {}

def load_chunks(wdir):
    """(cx,cz) -> {sectionY: (palette [(name, props)], data longs, bits, per, mask), '_be': [TileEntity]}."""
    from verify_mc_export import chunk_iter
    out = {}
    for cx, cz, nbt in chunk_iter(WorldZF(wdir)):
        secs = nbt.get('sections') or (nbt.get('Level') or {}).get('Sections') or []
        ch = {'_be': nbt.get('TileEntities') or nbt.get('block_entities') or []}
        for s in secs:
            y = s.get('Y', s.get('y', 0))
            bs = s.get('block_states') or s
            pal = [pal_entry(e) for e in bs.get('palette', bs.get('Palettes', []))]
            data = bs.get('data', bs.get('BlockStates'))
            bits = max(4, (len(pal) - 1).bit_length()) if len(pal) > 1 else 4
            ch[int(y)] = (pal, data, bits, 64 // bits, (1 << bits) - 1)
        out[(cx, cz)] = ch
    return out

def block_at(chunks, x, y, z):
    ch = chunks.get((x >> 4, z >> 4))
    if not ch:
        return 'minecraft:air', {}
    sec = ch.get(y >> 4)
    if not sec or not sec[0]:
        return 'minecraft:air', {}
    pal, data, bits, per, mask = sec
    if data is None:
        return pal[0]
    i = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15)
    v = (data[i // per] >> ((i % per) * bits)) & mask
    return pal[v] if v < len(pal) else ('minecraft:air', {})

# -------------------------------------------------------------------------------- extraction

GROUND = {
    'minecraft:grass_block', 'minecraft:dirt', 'minecraft:coarse_dirt', 'minecraft:rooted_dirt',
    'minecraft:podzol', 'minecraft:mycelium', 'minecraft:mud', 'minecraft:clay', 'minecraft:sand',
    'minecraft:red_sand', 'minecraft:sandstone', 'minecraft:cut_sandstone', 'minecraft:smooth_sandstone',
    'minecraft:red_sandstone', 'minecraft:stone', 'minecraft:cobblestone', 'minecraft:andesite',
    'minecraft:polished_andesite', 'minecraft:diorite', 'minecraft:polished_diorite', 'minecraft:granite',
    'minecraft:polished_granite', 'minecraft:deepslate', 'minecraft:gravel', 'minecraft:moss_block',
    'minecraft:dirt_path', 'minecraft:farmland', 'minecraft:snow_block', 'minecraft:pale_moss_block',
    'minecraft:packed_mud', 'minecraft:muddy_mangrove_roots', 'minecraft:tuff', 'minecraft:calcite',
}
WATER = {'minecraft:water', 'minecraft:lava'}
BAD_CELLS = {'minecraft:tnt'}                                   # TNT next to the portal fire - a boom on the first neighbour
PORTAL = {   # the portal blocks (the connected component = the structure frame and its netherrack apron)
    'minecraft:obsidian', 'minecraft:crying_obsidian', 'minecraft:netherrack', 'minecraft:magma_block',
    'minecraft:blackstone', 'minecraft:polished_blackstone', 'minecraft:polished_blackstone_bricks',
    'minecraft:chiseled_polished_blackstone', 'minecraft:cracked_polished_blackstone_bricks',
    'minecraft:polished_blackstone_wall', 'minecraft:polished_blackstone_slab', 'minecraft:polished_blackstone_stairs',
    'minecraft:blackstone_wall', 'minecraft:blackstone_slab', 'minecraft:blackstone_stairs', 'minecraft:gilded_blackstone',
    'minecraft:stone_bricks', 'minecraft:cracked_stone_bricks', 'minecraft:mossy_stone_bricks',
    'minecraft:gold_block', 'minecraft:iron_bars', 'minecraft:fire', 'minecraft:soul_fire', 'minecraft:candle',
}
STRUCT = PORTAL | {'minecraft:item_frame'}                       # + the decorations that mark the frame box

def state_str(name, props):
    if not props:
        return name
    return name + '|' + '|'.join('%s=%s' % kv for kv in sorted(props.items()))

def find_component(chunks, cx, cz, radius=40):
    """The connected (in XZ) component of the structure blocks nearest to the located point."""
    seen = {}
    for dz in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            x, z = cx + dx, cz + dz
            hit = None
            for y in range(200, 0, -1):          # the whole column: the frame may stand under a tree crown or be buried
                nm, pr = block_at(chunks, x, y, z)
                if nm in STRUCT:
                    hit = (y, nm, pr)
                    break
            if hit:
                seen[(x, z)] = hit
    if not seen:
        return None
    comps = []
    rest = dict(seen)
    while rest:
        start = next(iter(rest))
        comp = {start: rest.pop(start)}
        stack = [start]
        while stack:
            x, z = stack.pop()
            for n in ((x + 1, z), (x - 1, z), (x, z + 1), (x, z - 1)):
                if n in rest:
                    comp[n] = rest.pop(n)
                    stack.append(n)
        comps.append(comp)
    def dist(c):
        xs = [k[0] for k in c]; zs = [k[1] for k in c]
        return max(0, min(xs) - cx, cx - max(xs)) + max(0, min(zs) - cz, cz - max(zs))
    comps.sort(key=dist)
    return comps[0] if comps else None

def write_pgd2(fn, W, H, pal, hd, surf, cells, bes, bb):
    raw = struct.pack('>III', W, H, len(pal))
    for s in pal:
        b = s.encode('utf-8'); raw += struct.pack('>H', len(b)) + b
    for di in range(W * H):
        raw += struct.pack('>hhh', hd[di], surf[di], len(cells[di]))
        for (ady, p) in cells[di]:
            raw += struct.pack('>hh', ady, p)
    raw += struct.pack('>i', len(bes))
    for (bx, bz, dy, bkind, loot) in bes:
        b = loot.encode('utf-8')
        raw += struct.pack('>h h h B H', bx, bz, dy, bkind, len(b)) + b
    raw += struct.pack('>hhhh', max(0, bb[0]), max(0, bb[2]), min(W - 1, bb[1]), min(H - 1, bb[3]))
    blob = b'PGD2' + struct.pack('>I', len(raw)) + zlib.compress(raw)
    open(fn, 'wb').write(blob)
    return len(blob)

def neutralize(pal, surf, cells):
    """Strips the snow/ice of a snowy donor: the same ruin for forests of any climate."""
    newpal = []; idx = {}
    def pid(key):
        if key not in idx:
            idx[key] = len(newpal); newpal.append(key)
        return idx[key]
    def drop(s):
        return s.split('|')[0] in ('minecraft:snow_block', 'minecraft:snow', 'minecraft:powder_snow', 'minecraft:ice')
    def remap(s):
        return 'minecraft:grass_block|snowy=false' if s.startswith('minecraft:grass_block|snowy=true') else s
    surf2 = []
    for si in surf:
        s = pal[si]
        surf2.append(pid('minecraft:grass_block|snowy=false' if drop(s) else remap(s)))
    cells2 = []
    for c in cells:
        lst = []
        for (ady, p) in c:
            s = pal[p]
            if drop(s):
                continue
            lst.append((ady, pid(remap(s))))
        cells2.append(lst)
    return newpal, surf2, cells2

def extract(server, world, outdir, pad=12, max_side=42):
    base = SERVERS[server]
    wdir = os.path.join(base, world)
    found = json.load(open(os.path.join(wdir, 'ruins_found.json'), encoding='utf-8'))
    if not found:
        print('!! ruins_found.json is empty')
        return
    chunks = load_chunks(wdir)
    counts = {}
    for fnd in found:
        comp = find_component(chunks, fnd['x'], fnd['z'])
        if not comp:
            print('  %s at %d %d: no structure blocks near - skipped' % (fnd['id'], fnd['x'], fnd['z']))
            continue
        xs = [k[0] for k in comp]; zs = [k[1] for k in comp]
        bw, bh = max(xs) - min(xs) + 1, max(zs) - min(zs) + 1
        names = {v[1] for v in comp.values()}
        if 'minecraft:obsidian' not in names and 'minecraft:crying_obsidian' not in names:
            print('  %s at %d %d: no obsidian frame (%d blocks, %dx%d) - skipped' % (fnd['id'], fnd['x'], fnd['z'], len(comp), bw, bh))
            continue
        if bw > max_side or bh > max_side:
            print('  %s at %d %d: too big (%dx%d) - skipped' % (fnd['id'], fnd['x'], fnd['z'], bw, bh))
            continue
        x0, z0 = min(xs) - pad, min(zs) - pad
        W, H = bw + 2 * pad, bh + 2 * pad
        kind = 'portal'
        counts[kind] = counts.get(kind, 0) + 1
        tag = '%s%d' % (kind, counts[kind])
        pal = []; palIdx = {}
        def pid(name, props):
            key = state_str(name, props)
            if key not in palIdx:
                palIdx[key] = len(pal); pal.append(key)
            return palIdx[key]
        hd = [0] * (W * H); surf = [0] * (W * H); cells = [[] for _ in range(W * H)]
        bb = [W, -1, H, -1]
        water = 0
        for j in range(H):
            for i in range(W):
                x, z = x0 + i, z0 + j
                top = None
                for y in range(319, -64, -1):
                    nm, pr = block_at(chunks, x, y, z)
                    if nm == 'minecraft:air':
                        continue
                    if nm in GROUND:
                        top = (y, nm, pr)
                        break
                di = j * W + i
                if top is None:                               # a well / all cells are of the structure: the ground from neighbours
                    nb = [hd[di + d] for d in (-1, 1, -W, W) if 0 <= di + d < W * H and hd[di + d]]
                    hd[di] = max(nb) if nb else 64
                    surf[di] = pid('minecraft:grass_block', {})
                    continue
                hd[di] = top[0]
                surf[di] = pid(top[1], top[2])
                if top[1] in STRUCT:                          # the surface block is a part of the structure (the path of mossy cobble)
                    bb[0] = min(bb[0], i); bb[1] = max(bb[1], i); bb[2] = min(bb[2], j); bb[3] = max(bb[3], j)
                n = 0
                for y in range(top[0] + 1, min(top[0] + 48, 319)):
                    nm, pr = block_at(chunks, x, y, z)
                    if nm in WATER:                           # a pond/sea above the ground - not a forest ruin site
                        water += 1
                        break
                    if nm == 'minecraft:air' or nm in BAD_CELLS:
                        continue
                    cells[di].append((y - top[0], pid(nm, pr)))
                    n += 1
                    if n > 96:
                        break
                    if nm in STRUCT:
                        bb[0] = min(bb[0], i); bb[1] = max(bb[1], i); bb[2] = min(bb[2], j); bb[3] = max(bb[3], j)
        if bb[1] < bb[0]:
            print('  %s: no structure blocks in the square - skipped' % tag)
            continue
        if water:
            print('  %s: water in the square (%d columns) - skipped' % (tag, water))
            continue
        bes = []
        for te in [b for ch in chunks.values() for b in ch['_be']]:
            tx, ty, tz = te.get('x', 0), te.get('y', 0), te.get('z', 0)
            if not (x0 <= tx < x0 + W and z0 <= tz < z0 + H):
                continue
            tid = str(te.get('id', ''))
            if tid not in ('minecraft:chest', 'chest', 'minecraft:trapped_chest', 'trapped_chest'):
                continue
            i, j = tx - x0, tz - z0
            dyy = ty - hd[j * W + i]
            if dyy <= 0:
                continue                      # a chest under the ground (an igloo basement) - no block above the terrain
            bkind = 1 if 'trapped' not in tid else 2
            loot = str(te.get('LootTable', ''))
            bes.append((i, j, dyy, bkind, loot))
        # ---- PGD2
        snowy = any(('snow_block' in s) or s.startswith('minecraft:snow|') or ('snowy=true' in s) or ('ice' in s) for s in pal)
        if snowy:
            tag = '%s_snowy%d' % (kind, counts[kind])
        fn = os.path.join(outdir, 'patch_ruins_%s.bin' % tag)
        nbytes = write_pgd2(fn, W, H, pal, hd, surf, cells, bes, bb)
        nbl = sum(len(c) for c in cells)
        print('  %s: %dx%d, palette %d, blocks %d, chests %d, bb [%d..%d]x[%d..%d], %d bytes -> %s'
              % (tag, W, H, len(pal), nbl, len(bes), bb[0], bb[1], bb[2], bb[3], nbytes, os.path.basename(fn)))
        if snowy:
            # the same ruin without its snow/ice: for forests of a temperate climate
            npal, nsurf, ncells = neutralize(pal, surf, cells)
            ntag = '%s_green%d' % (kind, counts[kind])
            nfn = os.path.join(outdir, 'patch_ruins_%s.bin' % ntag)
            nb = write_pgd2(nfn, W, H, npal, hd, nsurf, ncells, bes, bb)
            print('  %s: %dx%d, palette %d, blocks %d, %d bytes -> %s'
                  % (ntag, W, H, len(npal), sum(len(c) for c in ncells), nb, os.path.basename(nfn)))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mode', choices=['gen', 'extract'])
    ap.add_argument('--server', default='121')
    ap.add_argument('--seed', type=int, default=12081)
    ap.add_argument('--world', default='ruinsgen')
    ap.add_argument('--out', default=os.path.join(tcfg.RESOURCES, 'templates'))
    a = ap.parse_args()
    if a.mode == 'gen':
        gen(a.server, a.seed, a.world)
    else:
        os.makedirs(a.out, exist_ok=True)
        extract(a.server, a.world, a.out)

if __name__ == '__main__':
    main()
