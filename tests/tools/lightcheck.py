# All wall_torch/lantern(hanging) in the zip — is there a REAL holder block?
import sys, zipfile
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import tcfg
import verify_mc_export as V

zf = zipfile.ZipFile(sys.argv[1] if len(sys.argv) > 1 else __import__('os').path.join(tcfg.WORLDS, 'mc_test.zip'))
chunks = {}
for cx, cz, ch in V.chunk_iter(zf):
    secs = {}
    for s in ch.get('sections', []):
        bs = s.get('block_states') or {}
        pal = bs.get('palette') or []
        entries = [((p.get('id', p.get('Name', 'minecraft:air')), p.get('properties') or p.get('Properties') or {})
                   if isinstance(p, dict) else (p, {})) for p in pal]
        entries = [('minecraft:flower_pot' if n.startswith('minecraft:potted_') else n, pr) for n, pr in entries]
        secs[s.get('Y', 0)] = (entries, bs.get('data'))
    chunks[(cx, cz)] = secs

def cell(x, y, z):
    secs = chunks.get((x >> 4, z >> 4))
    if not secs or (y >> 4) not in secs: return None
    e, d = secs[y >> 4]
    return V.decode(e, d, x & 15, y & 15, z & 15)

def cellEnt(e, d, i):
    # The palette entry of a CELL by its index. names.index(nm) catches duplicate names in the palette
    # (wall_torch facing=east + facing=north) and gives foreign properties — only the exact index!
    if d is None: return e[0]
    bits = max(4, (len(e) - 1).bit_length()); per = 64 // bits
    v = (d[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1)
    return e[v] if v < len(e) else ('minecraft:air', {})

bad = torch = lamp = lad = pot = 0
for (cx, cz), secs in chunks.items():
    for sy, (e, d) in secs.items():
        names = [n for n, _ in e]
        if ('minecraft:wall_torch' not in names and 'minecraft:lantern' not in names
                and 'minecraft:ladder' not in names
                and 'minecraft:flower_pot' not in names): continue
        for i in range(4096):
            lx, ly, lz = i & 15, (i >> 8) & 15, (i >> 4) & 15
            nm, pr = cellEnt(e, d, i)
            x, y, z = cx * 16 + lx, sy * 16 + ly, cz * 16 + lz
            if nm == 'minecraft:wall_torch':
                torch += 1
                f = dict(pr).get('facing', 'east')
                sup = {'east': (x - 1, y, z), 'west': (x + 1, y, z), 'north': (x, y, z + 1),
                       'south': (x, y, z - 1)}.get(f)
                if sup is None or cell(*sup) in (None, 'minecraft:air'):
                    bad += 1; print('FLOAT torch', x, y, z, f)
            elif nm == 'minecraft:lantern':
                pr = dict(pr)
                if pr.get('hanging') != 'true': continue
                lamp += 1
                if cell(x, y + 1, z) in (None, 'minecraft:air'):
                    bad += 1; print('FLOAT lamp', x, y, z)
            elif nm == 'minecraft:ladder':
                lad += 1
                f = dict(pr).get('facing', 'north')
                # facing looks AWAY from the wall: the holder is the neighbor on the opposite side
                sup = {'north': (x, y, z + 1), 'south': (x, y, z - 1), 'east': (x - 1, y, z),
                       'west': (x + 1, y, z)}[f]
                if cell(*sup) in (None, 'minecraft:air'):
                    bad += 1; print('FLOAT ladder', x, y, z, f)
            elif nm == 'minecraft:flower_pot':
                # A planter lives ONLY on a window sill: there must be a support below (the row "under
                # the window hole" — lining/wall). A pot in the air — the client will throw it out.
                pot += 1
                if cell(x, y - 1, z) in (None, 'minecraft:air'):
                    bad += 1; print('FLOAT pot', x, y, z)
print('torch', torch, 'hung-lamp', lamp, 'ladder', lad, 'pot', pot, 'floating', bad)
