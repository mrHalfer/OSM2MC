# Verify mc_test.zip (native 26.x) / mc_test_legacy.zip (1.21.x):
#  - every column of every written chunk solid at y=-64 AND y=0 (no voids/craters)
#  - hollow building: air in middle column above floor of concrete bbox, count < solid volume
#  - bricks-from-material, trees, carpets, rails(shape props), grass plants
#  - palette encoding: native {id,properties}/{'' :name} compounds when props exist; legacy {Name,Properties?}
import io, os, struct, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tcfg
WORLDS = tcfg.WORLDS
from dump121 import read_str

F = {1:'>b',2:'>h',3:'>i',4:'>q',5:'>f',6:'>d'}

POT_FL = set()   # the kinds of flowers found in minecraft:potted_<flower> blocks

def parse(raw):
    f = io.BytesIO(raw)
    def val(tid):
        if tid in F: return struct.unpack(F[tid], f.read(struct.calcsize(F[tid])))[0]
        if tid == 7: n=struct.unpack('>i',f.read(4))[0]; return f.read(n)
        if tid == 8: return read_str(f)
        if tid == 9:
            et=f.read(1)[0]; n=struct.unpack('>i',f.read(4))[0]
            return [val(et) for _ in range(n)]
        if tid == 10: return comp()
        if tid == 11:
            n=struct.unpack('>i',f.read(4))[0]; return list(struct.unpack('>'+'i'*n, f.read(4*n)))
        if tid == 12:
            n=struct.unpack('>i',f.read(4))[0]; return list(struct.unpack('>'+'q'*n, f.read(8*n)))
    def comp():
        d={}
        while True:
            t=f.read(1)[0]
            if t==0: return d
            nm=read_str(f); d[nm]=val(t)
    assert f.read(1)[0]==10; read_str(f)
    return comp()

def chunk_iter(zf):
    for n in [x for x in zf.namelist() if x.endswith('.mca')]:
        if '/entities/' in n or n.startswith('entities/'):
            continue      # entities/r.*.mca — another NBT scheme (Position/Entities), see ent_iter
        m = zf.read(n)
        rx, rz = map(int, n.split('/')[-1][2:-4].split('.'))
        for slot in range(1024):
            loc = struct.unpack('>I', m[slot*4:slot*4+4])[0]
            if not loc: continue
            so = loc >> 8
            size, fmt = struct.unpack('>IB', m[so*4096:so*4096+5])
            data = m[so*4096+5:so*4096+size+4]
            if data[:2] == b'\x1f\x8b':
                import gzip as _gz
                data = _gz.decompress(data)
            else:
                import zlib as _zl
                data = _zl.decompress(data)
            cx = (slot % 32) + (rx << 5)
            cz = (slot // 32) + (rz << 5)
            yield cx, cz, parse(data)

def ent_iter(zf):
    # entities/r.*.mca — since 1.17 the server reads entities ONLY from here (the Entities tag in
    # a region chunk is ignored). The cell root: {Position:[cx,cz], DataVersion, Entities}.
    for n in [x for x in zf.namelist() if x.endswith('.mca')
              and ('/entities/' in x or x.startswith('entities/'))]:
        m = zf.read(n)
        for slot in range(1024):
            loc = struct.unpack('>I', m[slot*4:slot*4+4])[0]
            if not loc: continue
            so = loc >> 8
            size, fmt = struct.unpack('>IB', m[so*4096:so*4096+5])
            data = m[so*4096+5:so*4096+size+4]
            if data[:2] == b'\x1f\x8b':
                import gzip as _gz
                data = _gz.decompress(data)
            else:
                import zlib as _zl
                data = _zl.decompress(data)
            yield parse(data)

def decode(entries, data, lx, ly, lz):
    names = [e[0] for e in entries]
    if data is None:
        return names[0]
    bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
    i = (ly << 8) | (lz << 4) | lx
    li = i // per; sh = (i % per) * bits
    v = (data[li] >> sh) & mask
    return names[v] if v < len(names) else 'minecraft:air'

def check(path, legacy, env='flat', full=True):
    zf = zipfile.ZipFile(path)
    # environment settings: the seed + the generator type (level.dat in legacy, a separate file in 26.x)
    import gzip as _gz
    if legacy:
        wgs = parse(_gz.decompress(zf.read('level.dat')))['Data']['WorldGenSettings']
        feat = wgs.get('generate_features')
    else:
        wgs = parse(_gz.decompress(zf.read('data/minecraft/world_gen_settings.dat')))['data']
        feat = wgs.get('generate_structures')
    gen = wgs['dimensions']['minecraft:overworld']['generator']['type']
    gset = wgs['dimensions']['minecraft:overworld']['generator'].get('settings')
    seed_ok = wgs.get('seed') == (777 if env == 'noise' else 1234567890)
    gen_ok = gen == ('minecraft:noise' if env == 'noise' else 'minecraft:flat')
    # without explicit settings the overworld of a 26.x noise generator fails with "Overworld settings missing"
    gset_ok = (gset == 'minecraft:overworld') if env == 'noise' else isinstance(gset, dict)
    feat_ok = feat == (1 if env == 'noise' else 0)
    print(f"  env={env}: seed={wgs.get('seed')} gen={gen} settings={gset!r} features={feat} -> "
          f"{'OK' if seed_ok and gen_ok and gset_ok and feat_ok else 'BAD'}")
    ok = seed_ok and gen_ok and gset_ok and feat_ok
    if not full:
        # Noise worlds are NOT checked by decoding (a user decision): the cave sections
        # would inflate the parsing to tens of minutes, and there is nothing to look for there. The real check of such
        # archives is the server (TESTS.md → smoke_mc.py): it came up, loaded the chunks, did not crash.
        # Only the environment config (above) and the presence of region files remain here.
        nReg = sum(1 for n in zf.namelist() if n.endswith('.mca'))
        print(f'  noise — decode is off (for completeness: --decode-noise); checked by the server '
              f'(smoke_mc.py); region files in the archive: {nReg} (want > 0)')
        return ok and nReg > 0
    chunks = {}
    props_seen = set()
    fmt_ok = True
    allBe = []                                    # block_entities (chests/paintings/spawners) of all chunks
    allEnt = []                                   # entities/*.mca — armor_stand mannequins of the shopping center
    for root in ent_iter(zf):
        allEnt += root.get('Entities') or []
    for cx, cz, ch in chunk_iter(zf):
        secs = {}
        allBe += ch.get('block_entities') or []
        for s in ch['sections']:
            bs = s.get('block_states', {})
            pal = bs.get('palette') or []
            data = bs.get('data')
            entries = []
            for e in pal:
                if isinstance(e, dict):
                    if 'Name' in e: entries.append((e['Name'], e.get('Properties')))
                    elif 'id' in e: entries.append((e['id'], e.get('properties')))
                    else: entries.append((e[''], None))
                else:
                    entries.append((e, None))
            for _i, (_n, _p) in enumerate(entries):   # a potted flower = the block potted_<flower>
                if _n.startswith('minecraft:potted_'):
                    POT_FL.add(_n[len('minecraft:potted_'):]); entries[_i] = ('minecraft:flower_pot', _p)
            compound_used = any(isinstance(e, dict) for e in pal)
            if legacy and not all(isinstance(e, dict) and 'Name' in e for e in pal):
                fmt_ok = False
            if (not legacy) and compound_used:
                for e in pal:
                    if not isinstance(e, dict) or (('id' in e) == ('' in e)):
                        fmt_ok = False
            for nm, pr in entries:
                if pr: props_seen.add((nm, tuple(sorted(pr.items()))))
            secs[s['Y']] = (entries, data)
        chunks[(cx, cz)] = secs

    counts = {}
    # "Floor under furniture" (a request: "interior elements sometimes hang in the air in the
    # stairwells"): we collect the coordinates of ALL floor furniture — the same set as FINNED
    # below, WITHOUT paintings (a painting hangs a tier higher and is attached to the wall, the floor under its cell
    # is not required). oak_stairs are both chairs and legal flights (a diagonal support).
    FLOORFURN = {'minecraft:bookshelf', 'minecraft:crafting_table', 'minecraft:cartography_table',
                 'minecraft:barrel', 'minecraft:furnace', 'minecraft:oak_fence',
                 'minecraft:oak_pressure_plate', 'minecraft:red_bed', 'minecraft:white_bed',
                 'minecraft:blue_bed', 'minecraft:cauldron', 'minecraft:composter',
                 'minecraft:loom', 'minecraft:grindstone', 'minecraft:smithing_table',
                 'minecraft:flower_pot', 'minecraft:oak_stairs'}
    furnPos = []                                  # [(x, y, z, name)]
    torchY = set()      # the levels of wall_torch / hanging lamps — "light on all floors", not only the 1st
    hangY = set()
    grassMaxY = -999    # the highest grass: <=8 — a flat test world (the city at y≈1)
    grassTot = 0        # all grass_block in the world (the denominator of the share of low ones)
    grassLow = 0        # …at y<=8: in a flat test world there are practically 100% of them
    def cell(x, y, z):
        ent = chunks.get((x >> 4, z >> 4), {}).get(y >> 4)
        if ent is None: return 'minecraft:air'
        return decode(ent[0], ent[1], x & 15, y & 15, z & 15)

    bad = 0
    cols = 0
    # First — a per-section count of air by sections (needed below): underground air = caves from
    # templates (a user request). sections -4..-2 (y<-32): before the "dungeons" there is 0 air there.
    secAir = {}
    deepChunk = set()               # chunks that have air at y<0 (sections -4..-1) — cave coverage
    for (cx, cz), secs in chunks.items():
        for sy in range(-4, 20):                  # the whole world column — in the noise mode the city is raised beyond y=32
            ent = secs.get(sy)
            if ent is None: continue
            entries, data = ent
            if data is None:                      # a unary palette (stone "apron" sections) — without decoding
                counts[entries[0][0]] = counts.get(entries[0][0], 0) + 4096
                continue
            for i in range(4096):
                nm = decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15)
                counts[nm] = counts.get(nm, 0) + 1
                if nm == 'minecraft:air':
                    secAir[sy] = secAir.get(sy, 0) + 1
                    if sy <= -1: deepChunk.add((cx, cz))
                elif nm == 'minecraft:grass_block':
                    y = sy * 16 + ((i >> 8) & 15)
                    if y > grassMaxY: grassMaxY = y
                    grassTot += 1
                    if y <= 8: grassLow += 1
                elif nm in FLOORFURN:
                    furnPos.append((cx * 16 + (i & 15), sy * 16 + ((i >> 8) & 15),
                                    cz * 16 + ((i >> 4) & 15), nm))
            # The Y distribution of light: torches and hanging lamps must be on several floors,
            # not concentrated on the first (the palette gives the hanging property index of the lantern)
            wt = {pi for pi, (en, ep) in enumerate(entries) if en == 'minecraft:wall_torch'}
            hl = {pi for pi, (en, ep) in enumerate(entries)
                  if en == 'minecraft:lantern' and ep and ep.get('hanging') == 'true'}
            if wt or hl:
                names = [e[0] for e in entries]
                bits = max(4, (len(names) - 1).bit_length()); per = 64 // bits; mask = (1 << bits) - 1
                for i in range(4096):
                    v = (data[i // per] >> ((i % per) * bits)) & mask
                    if v in wt: torchY.add(sy * 16 + ((i >> 8) & 15))
                    if v in hl: hangY.add(sy * 16 + ((i >> 8) & 15))
    # "Dungeons" are on if there is air deep (y<-48) — before the checkbox there was zero.
    caves = sum(secAir.get(s, 0) for s in (-4, -3)) > 4096
    # A flat test world: turf IN EVERY chunk at the walking level (y=0..1), in a noise world
    # it is raised to y~66. The max criterion does not work — 21 scene objects drop single turf
    # cells onto roofs (y=3..14), so we take the SHARE of turf at low Y.
    flatWorld = grassTot > 0 and grassLow * 2 > grassTot   # >50% of turf at y<=8 — a test city
    # Column tightness: the world bottom (-64) without holes ALWAYS; y=0 must be soil in a flat
    # test world (3 rows of analytics under the turf — caves do not touch it) and in a world without caves;
    # in a noise world with caves y=0 is legal underground (a user request).
    for (cx, cz), secs in chunks.items():
        for lz in range(16):
            for lx in range(16):
                cols += 1
                if cell(cx*16+lx, -64, cz*16+lz) == 'minecraft:air':
                    bad += 1
                elif (flatWorld or not caves) and cell(cx*16+lx, 0, cz*16+lz) == 'minecraft:air':
                    # The junction chamfer (exporter 3.59b): a flat world stands at y=0, the edge columns
                    # are lowered by 1..2 — above the new surface (y=-1/-2) empty cells y=0..-1
                    # are legal if the soil UNDER them is intact (at y=-4 a strip of earth/stone must exist).
                    xx, zz = cx*16+lx, cz*16+lz
                    if not (flatWorld and cell(xx, -4, zz) != 'minecraft:air'):
                        bad += 1
    print(f'  chunks={len(chunks)} cols={cols} bad={bad}; palette fmt ok={fmt_ok}')
    if flatWorld:
        # The flat world surface (the walking level y=0) — turf or an object, but NOT bare
        # rock: a uniform template section wrongly applied to the surface layers gives a
        # stone plateau of 16 blocks (a regular "dungeon" starts BELOW the soil strip).
        # With the light terrain of empty lands (±4) the surface is no longer exactly at y=0: we take the top non-empty block of the column in the window y=-8..8.
        def topBlock(x, z):
            for yy in range(8, -9, -1):
                b = cell(x, yy, z)
                if b != 'minecraft:air': return b
            return 'minecraft:air'
        stoneTop = sum(1 for (cx, cz) in chunks
                       for lz in range(16) for lx in range(16)
                       if topBlock(cx*16+lx, cz*16+lz) in
                       ('minecraft:stone', 'minecraft:deepslate', 'minecraft:tuff'))
        print(f'  [{"OK " if stoneTop * 20 < cols else "MISS"}] flat surface (y=0) is not a stone plateau: '
              f'bare rock {stoneTop}/{cols}')
        ok &= stoneTop * 20 < cols
    # The "armor" of the private sector (a user remark, a regression on L-shaped houses): an earth/
    # turf column need not break off EXACTLY under the roof step — this is how a hanging
    # cornice/gable above the void looks, whose colMin raised the fill to the cornice.
    armor = 0
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            names = [e[0] for e in entries]
            # the private sector roof — oak stairs (interior flights) and slabs of the slopes
            idx = {pi for pi, n in enumerate(names)
                   if n.endswith('oak_stairs') or n.endswith('oak_slab')}
            if not idx: continue
            if data is None:
                cells96 = [(cx*16+(i & 15), sy*16+((i >> 8) & 15), cz*16+((i >> 4) & 15))
                           for i in range(4096)]
            else:
                bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
                cells96 = [(cx*16+(i & 15), sy*16+((i >> 8) & 15), cz*16+((i >> 4) & 15))
                           for i in range(4096)
                           if ((data[i // per] >> ((i % per) * bits)) & mask) in idx]
            for x, y, z in cells96:
                if cell(x, y - 1, z).split(':')[-1] in ('grass_block', 'dirt', 'coarse_dirt', 'podzol'):
                    armor += 1;
    print(f'  [{"OK " if armor == 0 else "MISS"}] no "armor": earth under the roof steps: {armor} (want 0)')
    # The second staircase + room partitions (a regression, a user request). Two big buildings
    # of the test scene: the "big one" 44310000 (x-60..-12, z166..192; a cavity of ~900 cells) — is cut into
    # rooms of 100..300 AND gets a SECOND staircase in the opposite corner; the shopping center 44318000
    # (x275..313, z-210..-187; a cavity of ~500) — one room of 300..900, but also two staircases.
    # Criteria: of the stone_stairs columns in each region — at least two disjoint clusters;
    # the "big one" has partitions at the walking level of the flat archive (y=2) inside the facade.
    def stairClusters(bx0, bz0, bx1, bz1):
        cols = set()
        for (cx, cz), secs in chunks.items():
            for sy, (entries, data) in secs.items():
                names = [e[0] for e in entries]
                idx = {pi for pi, n in enumerate(names) if n.endswith('stone_stairs')}
                if not idx: continue
                if data is None:
                    x = cx * 16; z = cz * 16
                    if bx0 <= x <= bx1 and bz0 <= z <= bz1: cols.add((x, z))
                    continue
                bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
                for i in range(4096):
                    v = (data[i // per] >> ((i % per) * bits)) & mask
                    if v not in idx: continue
                    x = cx * 16 + (i & 15); z = cz * 16 + ((i >> 4) & 15)
                    if bx0 <= x <= bx1 and bz0 <= z <= bz1: cols.add((x, z))
        seen, n = set(), 0
        for c0 in cols:
            if c0 in seen: continue
            n += 1
            stack = [c0]; seen.add(c0)
            while stack:
                a, b = stack.pop()
                for q in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
                    if q in cols and q not in seen:
                        seen.add(q); stack.append(q)
        return n
    k1 = stairClusters(-60, 166, -12, 192)
    k2 = stairClusters(275, -210, 313, -187)
    part = 0
    if flatWorld:                                       # noise worlds are shifted in Y — we count
        for x in range(-57, -15):                       # only for the flat one (walking y=2)
            for z in range(168, 189):                   # strictly inside the facade (a margin of 2)
                if cell(x, 2, z) != 'minecraft:air': part += 1
    partOk = part >= 40 or not flatWorld
    print(f'  [{"OK " if k1 >= 2 and k2 >= 2 else "MISS"}] second staircase (column clusters): '
          f'big one {k1}, shopping center {k2} (want >=2 each)')
    print(f'  [{"OK " if partOk else "MISS"}] room partitions of the big one (y=2): {part} (want >=40)')
    ok = ok and bad == 0 and fmt_ok and armor == 0 and k1 >= 2 and k2 >= 2 and partOk
    if not ok:
        print('  GROUND/FORMAT FAIL')

    def want(nm, n=1, exact=False):
        c = counts.get(nm, 0) if exact else sum(v for k, v in counts.items() if nm in k)
        print(f'  [{"OK " if c >= n else "MISS"}] {nm}: {c}')
        return c >= n
    ok &= want('minecraft:bricks')
    ok &= want('minecraft:oak_log', 10)
    ok &= want('minecraft:oak_leaves', 20)
    ok &= want('_carpet', 20)
    ok &= want('minecraft:rail', 20)
    ok &= want('minecraft:short_grass', 20, exact=True)
    ok &= want('minecraft:tall_grass', 10000)                 # >50000 m² — grown grass (2 halves)
    ok &= want('minecraft:wheat', 50000)                      # >100000 m² — ripe wheat, solid
    ok &= want('_wool', 40)                                   # csrpol without height — wool instead of turf
    wc = counts.get('minecraft:brown_concrete', 0)             # csrpol with height — BRICK, not brown concrete (the color of the test object)
    print(f'  [{"OK " if wc == 0 else "MISS"}] brown_concrete (csrpol->bricks): {wc}')
    ok &= wc == 0
    ok &= want('minecraft:water', 500)                         # hydrography — pits with water
    ok &= want('minecraft:chest', 2)                           # "treasures": every 5th floor of the city
    #   (the forest is now ~1/384 — singles, in the test spot there may be none at all — not checked)
    ok &= want('minecraft:wall_torch', 1)                      # light inside buildings: torches on walls
    ok &= want('minecraft:lantern', 1)                         # … lamps under the ceiling and street lamp lanterns
    ok &= want('minecraft:oak_fence', 12, exact=True)          # site fences + lamp post stands
    ok &= want('minecraft:oak_fence_gate', 2, exact=True)      # "do not forget at least 2 gates" per spot
    ok &= want('minecraft:cobblestone_wall', 1, exact=True)    # the lamp post plinth (the village style)
    pp = counts.get('minecraft:stone_pressure_plate', 0)        # the v1 cap — removed together with the old post
    # stone slabs are now legal ONLY in front of the doors of big shopping centers (2 per entrance, see "entrances" below): >0 and <= 8
    print(f'  [{"OK " if 0 <= pp <= 8 else "MISS"}] stone_pressure_plate (only at the entrances of big shopping centers): {pp} (want 0..8)')
    ok &= 0 <= pp <= 8
    ok &= want('minecraft:oak_stairs', 40, exact=True)          # oak stairs = inter-floor flights
    #   of private houses (the roofs moved from this gate to oak_slab — see "roof of slabs")
    ok &= want('minecraft:oak_slab', 800, exact=True)           # slopes of gable roofs (44400002/...)
    #   (+ the tops of dummy tables in interiors — the count only grew)
    # "FLOOR UNDER FURNITURE" (a request: "interior elements sometimes hang in the air in the
    # stairwells"): under every floor item — not air. A stair (oak_stairs — a chair
    # or a flight) legally stands DIAGONALLY above the lower stair: air right under it
    # is allowed only if the same stair stands horizontally below.
    floating = []
    for fx, fy, fz, fnm in furnPos:
        if cell(fx, fy - 1, fz) != 'minecraft:air':
            continue
        if fnm == 'minecraft:oak_stairs' and any(
                cell(fx + d0, fy - 1, fz + d1) == 'minecraft:oak_stairs'
                for d0, d1 in ((1, 0), (-1, 0), (0, 1), (0, -1))):
            continue
        floating.append((fx, fy, fz, fnm.replace('minecraft:', '')))
    print(f'  [{"OK " if not floating else "MISS"}] floor under furniture: {len(floating)} hanging '
          f'(want 0): {floating[:8]}')
    ok &= not floating
    # "Interiors" (a request: "houses stopped being boxes with occasional chests"):
    # residential high-rises (except lift=1) and the 1st floor of the private sector are furnished. The furniture names
    # in one list — from here also the set for the gates "cottage", bigPriv and "lift=1 without furniture".
    # v2 (user edits): + paintings, a cauldron/composter/loom/grindstone/smithing table.
    FINNED = ('minecraft:bookshelf', 'minecraft:crafting_table', 'minecraft:cartography_table',
              'minecraft:barrel', 'minecraft:furnace', 'minecraft:oak_fence',
              'minecraft:oak_slab', 'minecraft:oak_pressure_plate', 'minecraft:oak_stairs',
              'minecraft:red_bed', 'minecraft:white_bed', 'minecraft:blue_bed',
              'minecraft:painting', 'minecraft:cauldron', 'minecraft:composter',
              'minecraft:loom', 'minecraft:grindstone', 'minecraft:smithing_table')
    def is_furn(c):
        return c in FINNED or c.endswith('_carpet')
    ok &= want('minecraft:bookshelf', 25, exact=True)           # bookshelves along walls
    ok &= want('minecraft:crafting_table', 5, exact=True)       # crafting tables
    #   v2: the second workstation block is VARIED (cartography/loom/smithing rotation by hash)
    #   — each kind now occurs more rarely, but all must be present; the grindstone stands "like a straw".
    ok &= want('minecraft:cartography_table', 1, exact=True)    # cartography tables
    ok &= want('minecraft:loom', 1, exact=True)                 # looms
    ok &= want('minecraft:smithing_table', 1, exact=True)       # smithing tables
    ok &= want('minecraft:grindstone', 1, exact=True)           # grindstones
    ok &= want('minecraft:cauldron', 1, exact=True)             # cauldrons (kitchen/utility corner)
    ok &= want('minecraft:composter', 1, exact=True)            # composters
    ok &= want('minecraft:barrel', 10, exact=True)              # barrel cabinets
    # paintings are now minecraft:painting entities (there is no such block) — see the gate "painting entities" below
    ok &= want('_bed', 6)                                       # beds (a pair red/white/blue)
    bpart = {v for nm, pr in props_seen if nm.endswith('_bed')
             for k, v in (pr or []) if k == 'part'}
    print(f'  [{"OK " if bpart >= {"foot", "head"} else "MISS"}] '
          f'beds head+foot (palette): {sorted(bpart)}')
    ok &= bpart >= {'foot', 'head'}
    def spaced(ys):    # two Y values separated by a floor height (1..7) — a sign of different
        s = sorted(ys) # floors of one building; random Y of neighboring houses give a bigger spread
        return any(1 <= s[j] - s[i] <= 7 for i in range(len(s)) for j in range(i + 1, len(s)))
    t_ok = spaced(torchY) and spaced(hangY)
    print(f'  [{"OK " if t_ok else "MISS"}] light floors: wall_torch Y={sorted(torchY)} '
          f'lantern(hanging) Y={sorted(hangY)}')
    ok &= t_ok
    # the profile of water columns: the FOUND water top downward — solid water, under it NOT air;
    # depths = area/6 (3..15). We look for the surface by scanning, not "at y=0": in the noise mode the
    # city (and the water) is raised to y~68.
    tops = {}
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0] == 'minecraft:water' for k in entries): continue
            for i in range(4096):
                if decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15) == 'minecraft:water':
                    key = (cx * 16 + (i & 15), cz * 16 + ((i >> 4) & 15))
                    y = sy * 16 + ((i >> 8) & 15)
                    if key not in tops or y > tops[key]: tops[key] = y
    depths = []
    leak = 0
    # The "visible from the sky" filter: real SURFACE water (our pond/river) — columns where nothing overhangs
    # the water (the sky is open up to y=110). Cave lakes of the templates are always covered
    # by a stone strip of the cast, so they are cut off. Without this filter the depth profile of a pond/river
    # drowned in millions of underground lakes (depths=[1..20] instead of [4,10]).
    tops_surf = {}
    for (x, z), y0 in tops.items():
        for yy in range(110, y0, -1):                 # an early exit — at the first roof
            if cell(x, yy, z) != 'minecraft:air':
                break
        else:
            tops_surf[(x, z)] = y0
    tops = tops_surf
    for (x, z), y0 in tops.items():
        d = 0
        while d < 20 and cell(x, y0 - d, z) == 'minecraft:water':
            d += 1
        if cell(x, y0 - d, z) == 'minecraft:air':
            leak += 1
        depths.append(d)
    dset = set(depths)
    print(f'  water cols={len(depths)} depths={sorted(dset)} leak={leak}')
    # {4,10} — the profiles of a pond/river (a linear river is three times shallower than a pond); 1 — the water of the outflow
    # sources on the chamfer (3.59c): a single source block on the new surface.
    ok &= leak == 0 and len(depths) > 100 and max(dset) >= 3 and 1 in dset   # a pond of 60 cells = 3 (lakes are three times shallower: area/18), a river 4
    if flatWorld:
        # The junction chamfer (3.59b) is in place: in a flat world cell(0) is empty only on the lowered
        # outer columns (the bottom of templates y<=-2, the pond/objects solidity at y=0 are kept).
        taperCols = sum(1 for (cx, cz) in chunks for lz in range(16) for lx in range(16)
                        if cell(cx*16+lx, 0, cz*16+lz) == 'minecraft:air')
        # The lava of the outflow sources (3.59c): the donor lava of templates in flat is strictly y<=-2
        # (the template is applied at y < top-2 = -1), so lava at y>=-1 is exactly the chamfer.
        lavaSurf = 0
        for (cx, cz), secs in chunks.items():
            for sy, (entries, data) in secs.items():
                if sy < -1 or not any(k[0] == 'minecraft:lava' for k in entries): continue
                for i in range(4096):
                    if decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15) == 'minecraft:lava' \
                            and sy * 16 + ((i >> 8) & 15) >= -1:
                        lavaSurf += 1
        print(f'  [{"OK " if 500 <= taperCols < cols // 2 and 0 <= lavaSurf <= 400 else "MISS"}] chamfer: '
              f'lowered {taperCols} columns (want >= 500), lava on the surface {lavaSurf} (want 0..400: lava appears by hash, on a real border there are fewer places for it)')
        ok &= 500 <= taperCols < cols // 2 and 0 <= lavaSurf <= 400
    # windows (bldpol): glass in facades + a "floor" slab — air above and below it, the slab itself is 1 block
    ok &= want('minecraft:glass', 20, exact=True)
    gxs=[]; gys=[]; gzs=[]
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0]=='minecraft:glass' for k in entries): continue
            for i in range(4096):
                if decode(entries, data, i&15, (i>>8)&15, (i>>4)&15)=='minecraft:glass':
                    gxs.append(cx*16+(i&15)); gys.append(sy*16+((i>>8)&15)); gzs.append(cz*16+((i>>4)&15))
    floors_ok = False
    if gxs:
        # the glass belongs to different buildings — we take the cluster at the highest glass
        i0 = gys.index(max(gys))
        cl = [(x, y, z) for x, y, z in zip(gxs, gys, gzs) if abs(x - gxs[i0]) <= 20 and abs(z - gzs[i0]) <= 20]
        gx0, gx1 = min(c[0] for c in cl), max(c[0] for c in cl)
        gy1 = max(c[1] for c in cl); gz0, gz1 = min(c[2] for c in cl), max(c[2] for c in cl)
        mx = (gx0+gx1)//2; mz = (gz0+gz1)//2
        yy = gy1 - 1                                    # above the top row of glazing — the air of the top floor
        d = 0
        while d < 12 and cell(mx, yy-d, mz) == 'minecraft:air':
            d += 1
        # the slab: no more than a floor up to it (4..6+1), under it air again (it is a slab, not a filling)
        floors_ok = 1 <= d <= 8 and cell(mx, yy-d-1, mz) == 'minecraft:air'
        print(f'  glass cluster x {gx0}..{gx1} y ..{gy1} z {gz0}..{gz1}; air->slab at depth {d}, below-slab air: '
              f"{cell(mx, yy-d-1, mz)}; floors: {floors_ok}")
        ok &= floors_ok
        # a see-through window: behind the outer glass (it is at the bbox edge of the cluster) there is no inner wall block
        thru = tot = 0
        for x, y, z in cl:
            if x == gx0: ix, iz = x + 1, z
            elif x == gx1: ix, iz = x - 1, z
            elif z == gz0: ix, iz = x, z + 1
            elif z == gz1: ix, iz = x, z - 1
            else: continue
            tot += 1
            # Behind the glass — a hole (air). The lower rows of residential windows are decorated with planters: the pot stands
            # INSIDE the same hole and does not break the "see-through" (the hole under it is the window lining).
            # 2×3 windows (bigWin): the top row of the strip lies IN THE PLANE of the ceiling — behind it is the slab,
            # this is glass "on the ceiling" of the floor, not an uncut wall (1 row of 4).
            if cell(ix, y, iz) in ('minecraft:air', 'minecraft:flower_pot'): thru += 1
        print(f'  see-through windows: {thru}/{tot}')
        ok &= tot > 0 and thru * 10 >= tot * 9          # >=90% — taking into account the glass in the slab plane
    else:
        print('  glass MISS (no windows)'); ok = False
    # hollow: bbox of red_concrete
    xs=[]; ys=[]; zs=[]
    rc = counts.get('minecraft:red_concrete', 0)
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0]=='minecraft:red_concrete' for k in entries): continue
            names=[e[0] for e in entries]
            for i in range(4096):
                if decode(entries, data, i&15, (i>>8)&15, (i>>4)&15)=='minecraft:red_concrete':
                    xs.append(cx*16+(i&15)); ys.append(sy*16+((i>>8)&15)); zs.append(cz*16+((i>>4)&15))
    if xs:
        cx0, cx1 = min(xs), max(xs); cy0, cy1 = min(ys), max(ys); cz0, cz1 = min(zs), max(zs)
        mx = (cx0+cx1)//2; mz = (cz0+cz1)//2
        # NOT the geometric center of the bbox: for low houses it falls into the capital/ridge.
        # A room = the middle of the column above the floor: the first air cells go from cy0+1..+3.
        col = [cell(mx, cy0 + k, mz) for k in range(1, 4)]
        center = next((c for c in col if c == 'minecraft:air'), col[-1])
        vol = (cx1-cx0+1)*(cy1-cy0+1)*(cz1-cz0+1)
        print(f'  red_concrete bbox {cx0}..{cx1} x {cy0}..{cy1} x {cz0}..{cz1}, count {rc}, solid {vol}, room cells={col}')
        hollow = center == 'minecraft:air' and rc < vol
        print(f'  hollow: {hollow}')
        ok &= hollow
        # The private sector (a user request, the cottage 44400002 above): a 1-block foundation flush
        # with the ground (the bottom y=0), the 1st floor room EXACTLY 4 blocks (y1..y4), the ceiling slab at y5,
        # on the attic line (y6) there is NO flat cap (through the room — roof/air), the inter-floor
        # flight — OAK, the lower stair in the walking row y=1, there are no stone stairs in the house at all.
        if cy0 == 0:
            ix0, ix1 = cx0 + 2, cx1 - 2                         # inside the wall ring (2 blocks)
            iz0, iz1 = cz0 + 2, cz1 - 2
            tot = (ix1 - ix0 + 1) * (iz1 - iz0 + 1)
            LIVE = ('minecraft:air', 'minecraft:oak_stairs', 'minecraft:wall_torch',
                    'minecraft:lantern', 'minecraft:chest', 'minecraft:ladder',
                    'minecraft:spawner',                           # the "living" content of the room
                    'minecraft:bookshelf', 'minecraft:crafting_table',       # …and "interiors":
                    'minecraft:cartography_table', 'minecraft:barrel',       # the 1st floor furniture
                    'minecraft:furnace', 'minecraft:oak_fence', 'minecraft:oak_slab',
                    'minecraft:oak_pressure_plate',                  # the table top (v2.1)
                    'minecraft:red_bed', 'minecraft:white_bed', 'minecraft:blue_bed',
                    'minecraft:painting', 'minecraft:cauldron', 'minecraft:composter',
                    'minecraft:loom', 'minecraft:grindstone', 'minecraft:smithing_table',
                    'minecraft:flower_pot')
            def lived(x, y, z):                                 # carpets — any colors
                c = cell(x, y, z)
                return c in LIVE or c.endswith('_carpet')
            floor0 = sum(1 for x in range(ix0, ix1 + 1) for z in range(iz0, iz1 + 1)
                         if cell(x, 0, z) not in ('minecraft:air', 'minecraft:short_grass',
                                                  'minecraft:tall_grass', 'minecraft:flower_pot'))
            room4 = sum(1 for x in range(ix0, ix1 + 1) for z in range(iz0, iz1 + 1)
                        if all(lived(x, y, z) for y in (1, 2, 3, 4)))
            slab5 = sum(1 for x in range(ix0, ix1 + 1) for z in range(iz0, iz1 + 1)
                        if cell(x, 5, z) != 'minecraft:air')    # a slab or a stair — counts as a floor
            cap6 = sum(1 for x in range(ix0, ix1 + 1) for z in range(iz0, iz1 + 1)
                       if cell(x, 6, z) != 'minecraft:air')     # there must be no caps here
            oak_fl, stone_fl, low_fl = 0, 0, 99
            for x in range(cx0 - 1, cx1 + 2):
                for z in range(cz0 - 1, cz1 + 2):
                    for y in range(1, 6):
                        c = cell(x, y, z)
                        if c == 'minecraft:oak_stairs':
                            oak_fl += 1; low_fl = min(low_fl, y)
                        elif c == 'minecraft:stone_stairs': stone_fl += 1
            furn = sum(1 for x in range(ix0, ix1 + 1) for z in range(iz0, iz1 + 1)
                       for y in (1, 2, 3, 4)                    # flight stairs — not furniture
                       if is_furn(cell(x, y, z)) and cell(x, y, z) != 'minecraft:oak_stairs')
            good = (floor0 == tot and room4 >= tot - 16 and slab5 >= tot - 12
                    and cap6 == 0 and oak_fl >= 10 and low_fl == 1 and stone_fl == 0
                    and furn >= 2)
            print(f'  [{"OK " if good else "MISS"}] private sector (cottage): floor {floor0}/{tot} at y0, '
                  f'room of 4 blocks {room4}/{tot}, slab y5 {slab5}/{tot}, cap y6 {cap6} (want 0), '
                  f'oak stairs {oak_fl} bottom y={low_fl} (want 1), stone {stone_fl} (want 0), '
                  f'furniture {furn} (want >= 2)')
            ok &= good
    # Thin walls LIFT=1 (a request: "WALLS 1 block thick instead of 2, not the roof and not the
    # foundation"): a high-rise 12x12x14 x 150..162 z 8..20 — the facade x150 is solid concrete on all
    # rows y1..13, the row of the INNER wall lining x151 is see-through air (the floor y0 and
    # the ceiling lining y14 were not touched: they are written on wall columns too).
    thin = 0
    for y in list(range(1, 7)) + list(range(8, 14)):      # y7 — the SLAB row: it reaches the wall!
        for z in range(9, 20):
            if cell(151, y, z) != 'minecraft:air': thin += 1
    solid = sum(1 for y in range(1, 16) if cell(150, y, 14) == 'minecraft:gray_concrete')   # the facade is solid up to the very roof (y15)
    floor0 = cell(151, 0, 14) == 'minecraft:gray_concrete'
    ceil14 = cell(151, 14, 14) == 'minecraft:air' and cell(151, 15, 14) == 'minecraft:gray_concrete'   # the shaft ceiling — ONE block (y15), no lining under it
    slabw = cell(151, 7, 14) == 'minecraft:gray_concrete'   # the slab of the 2nd floor REACHES the thin wall
    good = thin == 0 and solid == 15 and floor0 and ceil14 and slabw
    print(f'  [{"OK " if good else "MISS"}] thin walls (LIFT): lining x151 y1..13 blocks. {thin} (want 0), '
          f'facade x150 {solid}/15, floor y0 {int(floor0)} and a 1-block ceiling (y14 empty, y15 concrete) {int(ceil14)} (want 1), '
          f'slab y7 at the wall {int(slabw)} (want 1)')
    ok &= good
    # Partitions of the private sector (a request: "the partition goes above the first floor by
    # 1 block — partitions are not needed on the second floor"): an orange house 27x27 x 240..267
    # z 39..66 (a cavity of 24x24=576 > 300 — is cut). The 1st floor: the partition EXISTS (a row of
    # blocks in the inner rectangle at y2), the mezzanine y6 is CLEAN,
    # the slab y5 lies everywhere except head holes above the flights.
    ix0, ix1, iz0, iz1 = 242, 265, 41, 64
    tot = (ix1 - ix0 + 1) * (iz1 - iz0 + 1)
    part2 = sum(1 for x in range(ix0, ix1 + 1) for z in range(iz0, iz1 + 1)
                if cell(x, 2, z) == 'minecraft:bricks')      # a 2-storey private house — BRICK walls/partitions
    attic6 = sum(1 for x in range(ix0, ix1 + 1) for z in range(iz0, iz1 + 1)
                 if cell(x, 6, z) == 'minecraft:bricks')
    slab5 = sum(1 for x in range(ix0, ix1 + 1) for z in range(iz0, iz1 + 1)
                if cell(x, 5, z) != 'minecraft:air')
    furn = sum(1 for x in range(ix0, ix1 + 1) for z in range(iz0, iz1 + 1) for y in (2, 3)
               if is_furn(cell(x, y, z)) and cell(x, y, z) != 'minecraft:oak_stairs')
    good = part2 >= 20 and attic6 == 0 and slab5 >= tot - 30 and furn >= 6   # was 10: block paintings (4 pcs) moved to entities
    print(f'  [{"OK " if good else "MISS"}] partitions of the private sector: partition y2 {part2} blocks (want >= 20), '
          f'orange on the 2nd floor y6 {attic6} (want 0), slab y5 {slab5}/{tot} (want >= {tot - 30}), '
          f'furniture y2..y3 {furn} (want >= 6)')
    ok &= good
    # A private house 8x8x5.01 (CODE 44400100): the floor slab by geometry does NOT fit (5 rows) — no attic ceiling
    # → walls of LOGS (oak_log), the ceiling (y5) of PLANKS, no brick at all; the scene: world x 20..28 z 152..160.
    logs = sum(1 for x in range(20, 29) for z in range(152, 161) for y in range(0, 6)
               if cell(x, y, z) == 'minecraft:oak_log')
    ceilP = sum(1 for x in range(21, 28) for z in range(153, 160) if cell(x, 5, z) == 'minecraft:oak_planks')
    brk = sum(1 for x in range(20, 29) for z in range(152, 161) for y in range(0, 6)
              if cell(x, y, z) == 'minecraft:bricks')
    good = logs >= 20 and ceilP >= 20 and brk == 0
    print(f'  [{"OK " if good else "MISS"}] a one-storey private house: logs {logs} (want >= 20), '
          f'ceiling planks y5 {ceilP} (want >= 20), brick {brk} (want 0)')
    ok &= good
    # Living NPCs (entities in entities/): villagers of private/residential houses, a priest in the church 44319000, a scientist in a
    # school/kindergarten (+ children), shopping center traders (including a wandering one and a llama). The format — as `data get entity` gives it.
    vill = [e for e in allEnt if e.get('id') == 'minecraft:villager']
    profs = [((e.get('VillagerData') or {}).get('profession') or '') for e in vill]
    babies = sum(1 for e in vill if isinstance(e.get('Age'), int) and e['Age'] < 0)
    traders = sum(1 for e in allEnt if e.get('id') == 'minecraft:wandering_trader')
    llamas = sum(1 for e in allEnt if e.get('id') == 'minecraft:trader_llama')
    badVill = sum(1 for e in vill if not (e.get('VillagerData') or {}).get('profession', '').startswith('minecraft:'))
    good = (len(vill) >= 5 and 'minecraft:cleric' in profs and 'minecraft:librarian' in profs
            and babies >= 1 and badVill == 0)
    print(f'  [{"OK " if good else "MISS"}] villagers: villager {len(vill)} (want >= 5), cleric {profs.count("minecraft:cleric")} '
          f'(want >= 1), librarian {profs.count("minecraft:librarian")} (want >= 1), children {babies} (want >= 1), '
          f'wandering traders {traders}, llamas {llamas}, broken {badVill} (want 0)')
    ok &= good
    # Paintings = minecraft:painting entities (there is no painting block in the game): motifs 1x1, block_pos in the cell in front of the wall.
    pts = [e for e in allEnt if e.get('id') == 'minecraft:painting']
    okVar = {'minecraft:kebab', 'minecraft:aztec', 'minecraft:alban', 'minecraft:aztec2', 'minecraft:bomb',
             'minecraft:plant', 'minecraft:wasteland', 'minecraft:meditative'}
    badVar = sum(1 for e in pts if e.get('variant') not in okVar)
    badPos = sum(1 for e in pts if not (isinstance(e.get('block_pos'), list) and len(e['block_pos']) == 3))
    good = len(pts) >= 5 and badVar == 0 and badPos == 0
    print(f'  [{"OK " if good else "MISS"}] painting entities: {len(pts)} (want >= 5), motif not 1x1 {badVar} (want 0), '
          f'without block_pos {badPos} (want 0)')
    ok &= good
    # There must be NO furniture inside lift=1 (a request: interiors — "except lift=1"): the inside
    # of the high-rise x 151..161 z 9..19 y 1..13 — no bookshelf, no chair (stone flights and lamps are in place).
    finIn = sum(1 for x in range(151, 162) for z in range(9, 20) for y in range(1, 14)
                if is_furn(cell(x, y, z)))
    print(f'  [{"OK " if finIn == 0 else "MISS"}] lift=1 without furniture: inside the high-rise {finIn} (want 0)')
    ok &= finIn == 0
    # Stitching of the real perimeter (a request: "the second floor of the private sector without gables and
    # soffits — after the roof go along the perimeter and finish with planks from the top block
    # of the real wall to the roof"): an L-house x60..72 z-81..-70 — the wing end stands inside the bbox
    # frame and does not reach the slope, the gap is sewn with oak (oak_planks) between y7 and y11;
    # the cottage also has a soffit above the inner row of the ring (the slab from the 2nd row
    # goes a block higher than the wall).
    plank = 'minecraft:oak_planks'
    wing = sum(1 for x in range(60, 73) for z in range(-81, -70) for y in range(7, 12)
               if cell(x, y, z) == plank)
    cot = sum(1 for x in range(80, 91) for z in range(0, 11) if cell(x, 7, z) == plank)
    good = wing >= 20 and cot >= 1
    print(f'  [{"OK " if good else "MISS"}] perimeter stitching (L-house): planks y7..y11 in the L-house {wing} (want >= 20), '
          f'cottage eaves soffit y7 {cot} (want >= 1)')
    ok &= good
    # A roof of slabs (a request: "roofs of stairs are TOO high — build from slabs:
    # first the bottom one, then the top one"): a slope = oak_slab, a rise of 0.5 block per column.
    # The cottage ridge (11 columns) — y9 instead of y11 of the stair roof, for bigPriv (28 columns)
    # — y11 instead of y14; in the cottage roof strip there must be no stairs (oak stairs are
    # only inter-floor flights y1..y5).
    roof = 'minecraft:oak_slab'
    cot_sl = sum(1 for x in range(80, 91) for z in range(0, 11) for y in range(7, 14)
                 if cell(x, y, z) == roof)
    cot_top = max((y for x in range(80, 91) for z in range(0, 11) for y in range(7, 14)
                   if cell(x, y, z) == roof), default=0)
    bp_sl = sum(1 for x in range(240, 268) for z in range(39, 67) for y in range(8, 16)
                if cell(x, y, z) == roof)
    bp_top = max((y for x in range(240, 268) for z in range(39, 67) for y in range(8, 16)
                  if cell(x, y, z) == roof), default=0)
    band_st = sum(1 for x in range(80, 91) for z in range(0, 11) for y in range(7, 14)
                  if cell(x, y, z) == 'minecraft:oak_stairs')
    good = cot_sl >= 60 and cot_top <= 9 and bp_sl >= 400 and bp_top <= 11 and band_st == 0
    print(f'  [{"OK " if good else "MISS"}] roof of slabs: cottage slabs {cot_sl} (want >= 60) '
          f'top y={cot_top} (want <= 9, with stairs it was y11); bigPriv slabs {bp_sl} (want >= 400) '
          f'top y={bp_top} (want <= 11, it was y14); stairs in the cottage roof {band_st} (want 0)')
    ok &= good
    for nm, pr in sorted(props_seen):
        print('  props:', nm, dict(pr))
    ok &= any('oak_leaves' in nm and any(k=='persistent' for k,_ in pr) for nm, pr in props_seen)
    ok &= any(nm=='minecraft:rail' and any(k=='shape' for k,_ in pr) for nm, pr in props_seen)
    ok &= any(nm=='minecraft:tall_grass' and any(k=='half' and v=='upper' for k,v in pr) for nm, pr in props_seen)
    ok &= any(nm=='minecraft:wheat' and any(k=='age' and v=='7' for k,v in pr) for nm, pr in props_seen)
    # Fences: the connection properties MUST be kept in the palette (otherwise in the game "bare sticks" until the
    # neighbor block update). One fence with all four sides is enough — this is a ring corner.
    ok &= any(nm == 'minecraft:oak_fence'
              and {'north', 'east', 'south', 'west'} <= set(k for k, _ in pr)
              for nm, pr in props_seen)
    # A slab roof: type=bottom/top is not the default, it must be written explicitly in the palette
    # (without properties the server will drop all slabs to the bottom ones — the slope "a step down"). Interior
    # oak_stairs are oriented by the layout (in the test scene all flights are along X) — not checked.
    slab_t = {v for nm, pr in props_seen if nm == 'minecraft:oak_slab'
              for k, v in pr if k == 'type'}
    print(f'  [{"OK " if slab_t >= {"bottom", "top"} else "MISS"}] '
          f'oak_slab type (palette): {sorted(slab_t)}')
    ok &= slab_t >= {'bottom', 'top'}
    # Inter-floor stairs inside a multi-storey house (a house 12x12x14, the spot x 149..163 z -43..-29):
    # it has no roof CODEs — inter-floor stairs are ALWAYS STONE (a user request:
    # "stairs must be stone in all buildings except the private sector"; the private sector is
    # one-storey, there are no inter-floor ones). A flight of 7 stairs × 2 rows.
    # The staircase goes FROM the walking row of the 1st floor: the flight bottom cannot be BELOW the ground level
    # (y=0 is the surface of the flat world, the flight start y=1 = the walking cell of the 1st floor; earlier
    # the city was sunk (sink) and the threshold was 4, after the sink was rolled back the floor walk stood at y=1).
    tower_st, foot_st, low_st = 0, 0, 99
    for x in range(148, 166):
        for z in range(4, 24):
            for y in range(0, 105):
                if cell(x, y, z) != 'minecraft:stone_stairs': continue
                tower_st += 1
                low_st = min(low_st, y)
                if cell(x, y - 1, z) != 'minecraft:air': foot_st += 1
    print(f'  [{"OK " if tower_st >= 10 and foot_st >= 1 and low_st >= 1 else "MISS"}] inter-floor flight (stone): '
          f'stairs {tower_st}, with a support below {foot_st}, bottom y={low_st} (want >= 1)')
    ok &= tower_st >= 10 and foot_st >= 1 and low_st >= 1
    # A small two-storey 10x10x12 (x 119..131, z -94..-83): a cavity of ~7x7 — the flight window
    # (storyP+1)x1 fits into it, a staircase of STONE stairs is mandatory.
    # In the scan window there are no roofs (the neighboring roofs are far), we count all the stairs.
    small_st, small_low = 0, 99
    for x in range(117, 134):
        for z in range(-46, -30):
            for y in range(0, 105):
                if cell(x, y, z) == 'minecraft:stone_stairs':
                    small_st += 1
                    small_low = min(small_low, y)
    print(f'  [{"OK " if small_st >= 4 and small_low >= 1 else "MISS"}] staircase in a small house (stone): '
          f'stairs {small_st}, bottom y={small_low} (want >= 1)')
    ok &= small_st >= 4 and small_low >= 1
    # A narrow two-storey 6x6x12 (world x ~134..140, z ~-95..-84): a stepped flight in a
    # cavity of ~4x4 has no room — by the user's rule ("did not fit — make a
    # vertical one") there must be a ladder; a FLIGHT (stone stairs — outside the private sector
    # flights are stone) must not be in the window at all. Oak singles are allowed here with
    # "Interiors" — these are chairs at dummy tables, not a flight.
    lad_n, st_n = 0, 0
    for x in range(131, 144):
        for z in range(-48, -29):
            for y in range(0, 105):
                nm = cell(x, y, z)
                if nm == 'minecraft:ladder': lad_n += 1
                elif nm == 'minecraft:stone_stairs': st_n += 1
    print(f'  [{"OK " if lad_n >= 5 and st_n == 0 else "MISS"}] a vertical ladder in a narrow house: '
          f'ladder {lad_n} (want >= 5), stone stairs {st_n} (want 0)')
    ok &= lad_n >= 5 and st_n == 0
    # Head room above stairs ("there must be no ceiling above a staircase"): the cell ABOVE every
    # stone stair must be air (otherwise the flight runs into the slab/ceiling).
    st_head_bad = 0
    st_total = counts.get('minecraft:stone_stairs', 0)
    st_chunks = {(cx, cz) for (cx, cz), secs in chunks.items()
                 if any(any(nm == 'minecraft:stone_stairs' for nm, _ in en) for en, _ in secs.values())}
    for (cx, cz) in sorted(st_chunks):
        for y in range(0, 105):
            for lx in range(16):
                for lz in range(16):
                    if cell(cx * 16 + lx, y, cz * 16 + lz) == 'minecraft:stone_stairs':
                        if cell(cx * 16 + lx, y + 1, cz * 16 + lz) != 'minecraft:air':
                            st_head_bad += 1
    print(f'  [{"OK " if st_total >= 20 and st_head_bad == 0 else "MISS"}] stone stairs: total {st_total} '
          f'(want >= 20), ceiling above a stair {st_head_bad} (want 0)')
    ok &= st_total >= 20 and st_head_bad == 0
    # Trees by the user's rules (step 3.51): a wheat field (>100000 m²) — groups of
    # 2-3 trees on a 44 grid; a tall_grass meadow (>=50000 m²) — single trees >=30 blocks.
    # oak_log in the field/meadow windows could appear only from the new rules (there are no forest spots there).
    # In the lawn window (25x20 — below the threshold) there must be no trees at all.
    def box_logs(bx0, bz0, bx1, bz1):
        n = 0
        for cx in range(bx0 >> 4, (bx1 >> 4) + 1):
            for cz in range(bz0 >> 4, (bz1 >> 4) + 1):
                for (entries, data) in chunks.get((cx, cz), {}).values():
                    li = {pi for pi, (nm, _) in enumerate(entries) if nm == 'minecraft:oak_log'}
                    if not li: continue
                    if data is None:
                        continue                     # a unary palette — definitely not a log
                    bits = max(4, (len(entries) - 1).bit_length()); per = 64 // bits
                    for i in range(4096):
                        if ((data[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1)) not in li: continue
                        x = cx * 16 + (i & 15); z = cz * 16 + ((i >> 4) & 15)   # the window borders by cells!
                        if bx0 <= x <= bx1 and bz0 <= z <= bz1: n += 1
        return n
    wheat_logs = box_logs(-521, -91, -171, 210)      # the field: fact z -140..159 + 50 (cy0 shifted because of the shopping center)
    mead_logs = box_logs(279, -111, 520, 110)        # the meadow: fact z -160..59 + 50
    lawn_logs = box_logs(39, 9, 65, 30)              # the lawn: fact z -41..-20 + 50 (+ a margin)
    print(f'  [{"OK " if wheat_logs >= 6 and mead_logs >= 5 and lawn_logs == 0 else "MISS"}] trees: '
          f'field {wheat_logs} (want >= 6, groups), meadow {mead_logs} (want >= 5), lawn {lawn_logs} (want 0)')
    ok &= wheat_logs >= 6 and mead_logs >= 5 and lawn_logs == 0
    # Shopping center CODE 44318000 (30x20x12): a PANORAMA (the user clarified: the shopping center — as before, 3×5 does not
    # touch it): a glass strip in bands of 5..8 columns (a period of 6..9, a mullion), the corner wraps around.
    # A horizontal glass run of 5..8. (The scene position floats: tf is centered by the bbox of all
    # objects — the scan windows were recalculated for the current layout with the shopping center.) In an ordinary house a window in
    # every 4th column — the horizontal run is no more than 2 (a regression for the high-rise).
    def pano_run(bx0, bz0, bx1, bz1):
        best, ng = 0, 0
        for y in range(0, 235):        # a 160 m high-rise: in the noise world (+~68) glass up to ~222
            for z in range(bz0, bz1 + 1):
                cur = 0
                for x in range(bx0, bx1 + 1):
                    if cell(x, y, z) == 'minecraft:glass': cur += 1; ng += 1
                    else: best = max(best, cur); cur = 0
                best = max(best, cur)
            for x in range(bx0, bx1 + 1):
                cur = 0
                for z in range(bz0, bz1 + 1):
                    if cell(x, y, z) == 'minecraft:glass': cur += 1; ng += 1
                    else: best = max(best, cur); cur = 0
                best = max(best, cur)
        return best, ng
    tc_run, tc_glass = pano_run(277, -205, 310, -180)   # shopping center: fact z -233..-252 (old) + 50
    tw_run, tw_glass = pano_run(148, 4, 165, 23)         # tower: fact z -45..-26 (old) + 50
    print(f'  [{"OK " if 5 <= tc_run <= 9 and tc_glass >= 150 and tw_run <= 2 and tw_glass >= 20 else "MISS"}] shopping center panorama: '
          f'run {tc_run} (want 5..9 = bands 5..8 + a corner column), glass {tc_glass} (want >= 150); tower run {tw_run} (want <= 2), glass {tw_glass} (want >= 20)')
    ok &= 5 <= tc_run <= 9 and tc_glass >= 150 and tw_run <= 2 and tw_glass >= 20
    # A residential high-rise 44400000 14x14x200 (39 floors): world x 169..182, z -104..-91 (flat; noise is raised
    # by ~+68). Square 2×2 windows — a horizontal glass run of exactly 2 (a pair of columns, mullions
    # in 2 columns), glass on tens of floor strips.
    hw_run, hw_glass = pano_run(167, -106, 184, -88)
    print(f'  [{"OK " if hw_run == 2 and hw_glass >= 200 else "MISS"}] 2x2 windows (residential high-rise 44400000): '
          f'run {hw_run} (want 2 = a pair of columns), glass {hw_glass} (want >= 200)')
    ok &= hw_run == 2 and hw_glass >= 200
    # The private sector (a request: "make 2×2 windows in the private sector — now 1×2, it looks bad"):
    # the cottage 10x10 x80..90 z0..10 and the orange one 27×27 x240..267 z39..66 — the wide2 profile
    # (a pair of glass columns). The horizontal run = 2 (not 1, as it was with 1×2, and not >2 —
    # mullions every 4 columns keep the strip fractional).
    cot_run, cot_glass = pano_run(79, -1, 91, 11)
    bp_run, bp_glass = pano_run(239, 38, 268, 67)
    good = cot_run == 2 and bp_run == 2 and cot_glass >= 6 and bp_glass >= 20
    print(f'  [{"OK " if good else "MISS"}] 2x2 windows (private sector): cottage run {cot_run} (want 2), '
          f'glass {cot_glass} (want >= 6); orange 27x27 run {bp_run} (want 2), glass {bp_glass} (want >= 20)')
    ok &= good
    # The count of the given block in a box (all heights — the noise world is raised by +62, the ceiling 105).
    def box_count(bx0, bz0, bx1, bz1, want_nm):
        n = 0
        for x in range(bx0, bx1 + 1):
            for z in range(bz0, bz1 + 1):
                for y in range(0, 105):
                    if cell(x, y, z) == want_nm: n += 1
        return n
    # Planters on window sills (30% of the lower window row, except the shopping center): there are pots, the flower is set by
    # a separate block minecraft:potted_<flower> (flattening 1.13+); here they are normalized to flower_pot (display.id /
    # DisplayBlock.Name / flower — see buildChunkNbt). There are no pots at all in the panoramic shopping center.
    # The BEs of pots in this world (with a flower) — the same allBe as the chests.
    nPots = counts.get('minecraft:flower_pot', 0)
    potFl = set(POT_FL)
    for b in allBe:
        if b.get('id') != 'minecraft:flower_pot':
            continue
        disp = b.get('display') or b.get('DisplayBlock') or {}
        nm = disp.get('id') or disp.get('Name') or b.get('flower')
        if nm: potFl.add(nm)
    tcPots = box_count(276, -206, 312, -178, 'minecraft:flower_pot')
    print(f'  [{"OK " if nPots >= 5 and len(potFl) >= 2 and tcPots <= 2 else "MISS"}] planters with flowers: '
          f'pots {nPots} (want >= 5), flowers in BE {len(potFl)} (want >= 2), in the shopping center {tcPots} (want <= 2: there are no window ones, but a tub in the hall — the 2% interior slot)')
    ok &= nPots >= 5 and len(potFl) >= 2 and tcPots <= 2
    # A shed CODE 44000000 (world x 150..160, z -18..-10): no windows at all + no planters.
    barnGlass = box_count(148, -21, 163, -7, 'minecraft:glass')
    barnPots = box_count(148, -21, 163, -7, 'minecraft:flower_pot')
    print(f'  [{"OK " if barnGlass == 0 and barnPots == 0 else "MISS"}] shed without windows: '
          f'glass {barnGlass} (want 0), planters {barnPots} (want 0)')
    ok &= barnGlass == 0 and barnPots == 0
    # A shed (44000000): walls and roof — 1 block (earlier 2); the section z=-14, x150..160
    barnWall = [cell(x, 2, -14) != 'minecraft:air' for x in range(150, 161)]
    barnRoof = [cell(155, y, -14) != 'minecraft:air' for y in range(3, 8)]
    good = barnWall[0] and barnWall[-1] and not any(barnWall[1:-1]) and not cell(151, 2, -14) != 'minecraft:air' and sum(barnRoof) == 1
    print(f'  [{"OK " if good else "MISS"}] shed: walls {sum(barnWall)} blocks in a row (want 2: one on each side), roof {sum(barnRoof)} layer (want 1)')
    ok &= good
    # there are none on the wheat field (the field is checked by the absence of tall_grass, we do not put flowers there).
    kFl = ('dandelion', 'poppy', 'cornflower', 'allium', 'azure_bluet', 'oxeye_daisy',
           'red_tulip', 'white_tulip', 'orange_tulip', 'pink_tulip')
    flCounts = [counts.get('minecraft:' + f, 0) for f in kFl]
    print(f'  [{"OK " if min(flCounts) >= 10 and sum(flCounts) >= 200 else "MISS"}] meadow flowers: '
          f'total {sum(flCounts)}, the minimum of a kind {min(flCounts)} (want >= 10 each)')
    ok &= min(flCounts) >= 10 and sum(flCounts) >= 200
    # Roads by codes (a user request): the roadway is NOT a carpet but a WOOL BLOCK in the surface layer
    # (sunk into the terrain), the marking — a carpet on top of the wool. The bike path 45261000 — dark red
    # wool without widening; the highway 45210000 — widened by ±2, dark gray, the marking
    # light_gray_carpet ONLY as a center line in the middle (the edges are not marked — a request);
    # the others — also widen and darken (gray_wool).
    bikeRed = box_count(208, -44, 242, -38, 'minecraft:red_wool')
    bikeGray = box_count(208, -44, 242, -38, 'minecraft:gray_wool')
    hwyGray = box_count(58, -20, 102, -12, 'minecraft:gray_concrete')      # motor roads — dark concrete
    hwyMark = box_count(58, -20, 102, -12, 'minecraft:white_carpet')      # the center marking — white
    hwyEdge = box_count(58, -20, 102, -12, 'minecraft:light_gray_carpet')  # curbs — light gray
    print(f'  [{"OK " if bikeRed >= 40 and bikeGray == 0 and hwyGray >= 100 and 40 <= hwyMark <= 50 and hwyEdge >= 10 else "MISS"}] roads (wool in the ground): '
          f'bike red_wool {bikeRed} (want >= 40) gray {bikeGray} (want 0); '
          f'highway gray_concrete {hwyGray} (want >= 100), white marking {hwyMark} (want 40..50), gray curb {hwyEdge} (want >= 10)')
    ok &= bikeRed >= 40 and bikeGray == 0 and hwyGray >= 100 and 40 <= hwyMark <= 50 and hwyEdge >= 10
    bikeCarpet = box_count(208, -44, 242, -38, 'minecraft:red_carpet')
    print(f'  [{"OK " if bikeCarpet >= bikeRed - 5 else "MISS"}] bike path: red_carpet on top of the wool {bikeCarpet} (want ~= red_wool {bikeRed})')
    ok &= bikeCarpet >= bikeRed - 5
    # Light gray codes 61310000/61320000/61330000 (a request "like 45260000"): three test
    # strips of a deliberately DARK color (70,70,70) — the export must lay podzol (dirt/field roads)
    # regardless of the object color; gray/red inside the strips is an error (want 0).
    lgBands = ((-50, -44), (-62, -56), (-74, -68))          # z-strips 6131/6132/6133 (x 210..230)
    lgN = [box_count(209, z0, 231, z1, 'minecraft:podzol') for z0, z1 in lgBands]
    lgBad = [box_count(209, z0, 231, z1, 'minecraft:gray_wool')
             + box_count(209, z0, 231, z1, 'minecraft:red_wool') for z0, z1 in lgBands]
    print(f'  [{"OK " if min(lgN) >= 120 and max(lgBad) == 0 else "MISS"}] light gray 613x: '
          f'podzol on the strips {lgN} (want >= 120 each), foreign wool {lgBad} (want 0)')
    ok &= min(lgN) >= 120 and max(lgBad) == 0
    # The roadway priority (a request): dark gray roads are more important than pedestrian/bike ones. The pedestrian road 45260000
    # crosses the highway and goes EARLIER in the list; in the intersection zone x75..81 the body strips of
    # the highway (z -15/-18 — lines without marking) must stay the DARK wool of the highway, not the
    # light gray wool of the sidewalk (repainting is not allowed by first-wins).
    # The pedestrian road stays only outside the roadways (z=-13; rows -20/-21 were taken by other gray roads — also
    # by the priority rule).
    xw0, xw1 = 75, 81
    interGray = (box_count(xw0, -15, xw1 - 1, -15, 'minecraft:gray_concrete')
                 + box_count(xw0, -18, xw1 - 1, -18, 'minecraft:gray_concrete'))
    interLg = (box_count(xw0, -15, xw1 - 1, -15, 'minecraft:light_gray_concrete')
               + box_count(xw0, -18, xw1 - 1, -18, 'minecraft:light_gray_concrete'))
    # The inclusive raster (a fix of corner columns) widened the highway strip to z=-13 — the nearest row of the
    # pedestrian road in the intersection zone gave priority to the highway, the sidewalk stayed at z=-12 (7 cells).
    pedFree = box_count(xw0, -12, xw1, -12, 'minecraft:light_gray_concrete')  # outside the roadways (pedestrian — light concrete)
    print(f'  [{"OK " if interGray == 12 and interLg == 0 and pedFree == 7 else "MISS"}] road priority: '
          f'in the intersection gray_concrete {interGray} (want 12), light {interLg} (want 0), '
          f'pedestrian road outside the highway {pedFree} (want 7)')
    ok &= interGray == 12 and interLg == 0 and pedFree == 7
    # A double track (a request: "instead of one roadway line — two parallel rrslin with an intermediate
    # gap of 2 blocks"): in the railway window there are exactly two rows of rails, the gap between the rows = 2 cells.
    railRows, railN = set(), 0
    for x in range(60, 122):
        for z in range(-37, -22):
            for y in range(0, 105):
                if cell(x, y, z) == 'minecraft:rail':
                    railRows.add(z); railN += 1
    rr = sorted(railRows)
    clusters = []
    for z in rr:
        if clusters and z - clusters[-1][-1] <= 1: clusters[-1].append(z)
        else: clusters.append([z])
    gap = (clusters[1][0] - clusters[0][-1] - 1) if len(clusters) == 2 else -1
    print(f'  [{"OK " if len(clusters) == 2 and gap == 2 and railN >= 100 else "MISS"}] double-track railway: '
          f'lines {len(clusters)} (want 2), gap {gap} (want 2), rails {railN} (want >= 100)')
    ok &= len(clusters) == 2 and gap == 2 and railN >= 100
    # chests: block_entities with a vanilla loot table (the BE key is LootTable, checked on the server;
    # the game will give the contents at the first opening), the block at the position is chest. Into the common list of BEs
    # we put pots too (flower_pot with a flower) — they are checked by the separate "planters" test above.
    chestBe = [b for b in allBe if b.get('id') == 'minecraft:chest']
    potBe = [b for b in allBe if b.get('id') == 'minecraft:flower_pot']
    paintBe = [b for b in allBe if b.get('id') == 'minecraft:painting']
    spawnBe = [b for b in allBe if b.get('id') == 'minecraft:mob_spawner']
    otherBe = [b for b in allBe if b.get('id') not in ('minecraft:chest', 'minecraft:flower_pot',
                                                       'minecraft:painting', 'minecraft:mob_spawner',
                                                       'minecraft:barrel', 'minecraft:sign', 'minecraft:bell', 'minecraft:banner', 'minecraft:creaking_heart', 'minecraft:lectern')]   # barrels, address signs with a LootTable (interiors)
    print(f'  block_entities: {len(allBe)} (chest {len(chestBe)}, flower_pot {len(potBe)}, '
          f'painting {len(paintBe)}, spawner {len(spawnBe)}, other {len(otherBe)})')
    ok &= len(chestBe) >= 1
    ok &= not otherBe                                   # no unaccounted block_entities
    for b in chestBe:
        loot = b.get('LootTable', '')
        if not str(loot).startswith('minecraft:chests/'):
            print(f'  BAD block_entity: {b}'); ok = False
        elif cell(b['x'], b['y'], b['z']) != 'minecraft:chest':
            print(f"  BAD chest at {b['x']},{b['y']},{b['z']}: {cell(b['x'], b['y'], b['z'])}"); ok = False
    nChest = sum(v for k, v in counts.items() if k == 'minecraft:chest')   # a double one = 2 blocks = 2 BEs
    print(f'  chest blocks={nChest} (BE==blocks: {len(chestBe) == nChest})')
    ok &= len(chestBe) == nChest
    # A single-track L-shaped railway (61122000): the axis to the east (x 60..90, z=-131), then to the north (x=90, z -131..-151).
    # Exactly one track (in the section x=75 one rail), the cells are connected, the shapes: straight + a north_west turn.
    railShapes = {v for nm, tp in props_seen if nm == 'minecraft:rail' for k, v in (tp or ()) if k == 'shape'}
    colRails = sum(1 for z in range(-140, -122) for y in range(0, 4) if cell(75, y, z) == 'minecraft:rail')
    legE = sum(1 for x in range(60, 91) for y in range(0, 4) if cell(x, y, -131) == 'minecraft:rail')
    legN = sum(1 for z in range(-151, -130) for y in range(0, 4) if cell(90, y, z) == 'minecraft:rail')
    good = (colRails == 1 and legE >= 28 and legN >= 18
            and {'east_west', 'north_south', 'north_west'} <= railShapes)
    print(f'  [{"OK " if good else "MISS"}] L-shaped single-track railway: rails in the section x=75 {colRails} (want 1), '
          f'the east branch {legE} (want >= 28), the north {legN} (want >= 18), shapes {sorted(railShapes)}')
    ok &= good
    # Building entrances: an entrance is mandatory (except an elevator shaft LIFT and a shed 44000000). The shopping center and the church — double-leaf
    # (4 door blocks) wooden; a big shopping center (plan >= 1200 m²) has stone plates in front of the leaves; the high-rise 44400000 —
    # from 1 to 5 entrances (2..10 blocks); the church — brick.
    def doors_in(x0_, x1_, z0_, z1_):
        names = []
        for x in range(x0_, x1_ + 1):
            for z in range(z0_, z1_ + 1):
                for y in range(0, 7):
                    nm = cell(x, y, z)
                    if nm and nm.endswith('_door'): names.append(nm)
        return names
    def count_in(x0_, x1_, z0_, z1_, name, ys=range(0, 7)):
        return sum(1 for x in range(x0_, x1_ + 1) for z in range(z0_, z1_ + 1) for y in ys if cell(x, y, z) == name)
    dLift = doors_in(148, 164, 6, 22)
    dBarn = doors_in(148, 163, -21, -7)
    dTc = doors_in(276, 314, -214, -186)
    dBig = doors_in(310, 360, -175, -125)
    dChurch = doors_in(-2, 16, 185, 207)
    dTower = doors_in(168, 186, -106, -88)
    dCube = doors_in(78, 92, -2, 12)
    dG = doors_in(58, 74, -83, -68)
    dAll = doors_in(-130, 500, -400, 300)
    plates = count_in(310, 360, -175, -125, 'minecraft:stone_pressure_plate', range(0, 4))
    platesSmall = count_in(276, 314, -214, -186, 'minecraft:stone_pressure_plate', range(0, 4))
    churchBricks = count_in(-2, 16, 185, 207, 'minecraft:stone_bricks', range(0, 12))
    good = (len(dLift) == 0 and len(dBarn) == 0
            and len(dTc) >= 4 and not any('iron' in n for n in dTc) and platesSmall == 0
            and len(dBig) >= 4 and plates >= 2
            and len(dChurch) >= 4 and not any('iron' in n for n in dChurch) and churchBricks >= 40
            and 2 <= len(dTower) <= 10 and len(dCube) >= 2 and len(dG) >= 2
            and sum(1 for n in dAll if 'iron' in n) >= 4)
    print(f'  [{"OK " if good else "MISS"}] entrances: lift {len(dLift)} (want 0), shed {len(dBarn)} (want 0), shopping center {len(dTc)} '
          f'(want >= 4, without iron), big shopping center {len(dBig)}+plates {plates} (want >= 4 and >= 2), small shopping center plates {platesSmall} (want 0), '
          f'church {len(dChurch)} (want >= 4, wood) brick {churchBricks} (want >= 40), high-rise {len(dTower)} (want 2..10), '
          f'cube {len(dCube)}, L-house {len(dG)} (want >= 2 — an entrance is mandatory), iron in total {sum(1 for n in dAll if "iron" in n)}')
    ok &= good
    # Shopping center filling: in a big shopping center (interior x 318..352, z -167..-133) there must be no empty areas >= 5x5 cells left
    # at the walking level (y=1); we count the 5x5 positions entirely of air at the feet level.
    def air_squares(x0_, x1_, z0_, z1_, y_, g=5):
        n = 0
        for x in range(x0_, x1_ - g + 2):
            for z in range(z0_, z1_ - g + 2):
                if all(cell(x + dx, y_, z + dz) == 'minecraft:air' for dx in range(g) for dz in range(g)):
                    n += 1
        return n
    gapsBig = air_squares(318, 352, -167, -133, 1)
    furnBig = sum(1 for x in range(318, 353) for z in range(-167, -132) for y in (1, 2)
                  if cell(x, y, z) in ('minecraft:bookshelf', 'minecraft:barrel', 'minecraft:oak_fence'))
    print(f'  [{"OK " if gapsBig <= 32 else "MISS"}] shopping center without voids: free 5x5 squares {gapsBig} (want <= 32: the remainder — staircase shafts and passages at the partition doors), furniture {furnBig}')
    ok &= gapsBig <= 32
    # Stands in the shopping center: item frames (item_frame) on plank panels. Swords/tools of different materials with a wear of
    # 2..36% of durability, occasionally golden apples/carrots; a frame hangs on a plank (a support from the opposite side).
    frs = [e for e in allEnt if e.get('id') == 'minecraft:item_frame']
    altarFr = [e for e in frs if e.get('Facing') == 1]
    frs = [e for e in frs if e.get('Facing') != 1]
    TMAX = {'wooden': 59, 'stone': 131, 'golden': 32, 'iron': 250, 'diamond': 1561}
    FOOD = ('minecraft:golden_apple', 'minecraft:golden_carrot')
    frBad = frNoSupport = frTools = frFood = 0
    matCnt = {}
    DIRV = {2: (0, -1), 3: (0, 1), 4: (-1, 0), 5: (1, 0)}
    for e in frs:
        it = e.get('Item') or {}
        iid = str(it.get('id', ''))
        bp = e.get('block_pos') or []
        fc = e.get('Facing')
        if iid in FOOD: frFood += 1
        else:
            try:
                mat, kind = iid.replace('minecraft:', '').split('_', 1)
                mx = TMAX[mat]; dm = (it.get('components') or {}).get('minecraft:damage')
                frTools += 1; matCnt[mat] = matCnt.get(mat, 0) + 1
                if kind not in ('sword', 'pickaxe', 'axe', 'shovel', 'hoe') or dm is None                         or not (mx - round(mx * 0.36) - 1 <= dm <= mx - 1): frBad += 1
            except Exception:
                frBad += 1
        if len(bp) == 3 and fc in DIRV:
            dx, dz = DIRV[fc]
            if cell(bp[0] - dx, bp[1], bp[2] - dz) != 'minecraft:oak_planks': frNoSupport += 1
        else:
            frBad += 1
    good = len(frs) >= 8 and frBad == 0 and frNoSupport == 0 and frTools >= 6
    print(f'  [{"OK " if good else "MISS"}] stands with frames: frames {len(frs)} (want >= 8), tools/swords {frTools}, food {frFood}, '
          f'materials {matCnt}, broken {frBad}, without support {frNoSupport} (want 0)')
    ok &= good
    # Sacrificial stands in the shopping center: an obsidian pedestal, on it a frame (Facing=1) with a sword.
    alBad = sum(1 for e in altarFr
                if not str((e.get('Item') or {}).get('id', '')).endswith('_sword')
                or len(e.get('block_pos') or []) != 3
                or cell(e['block_pos'][0], e['block_pos'][1] - 1, e['block_pos'][2]) != 'minecraft:obsidian')
    good = len(altarFr) >= 2 and alBad == 0
    print(f'  [{"OK " if good else "MISS"}] sacrificial stands: {len(altarFr)} (want >= 2), broken {alBad} (want 0)')
    ok &= good
    # "Villages" of private houses: in a cluster of private houses a square with a bell (the village center/the raid point), a well
    # and work blocks; not closer than 1000 blocks to each other (in the test — one per scene).
    bells = []
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0] == 'minecraft:bell' for k in entries): continue
            for i in range(4096):
                if decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15) == 'minecraft:bell':
                    bells.append((cx * 16 + (i & 15), sy * 16 + ((i >> 8) & 15), cz * 16 + ((i >> 4) & 15)))
    bellFar = all(((a[0] - b[0]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5 >= 500 for ia, a in enumerate(bells) for b in bells[ia + 1:])
    around = 0
    if bells:
        bx, by, bz = bells[0]
        around = sum(1 for dx in range(-5, 6) for dz in range(-5, 6) for dy in range(0, 3)
                     if cell(bx + dx, by + dy - 1, bz + dz) in ('minecraft:composter', 'minecraft:barrel', 'minecraft:smithing_table', 'minecraft:loom'))
    good = len(bells) >= 1 and bellFar and around >= 3
    print(f'  [{"OK " if good else "MISS"}] villages: bells {len(bells)} (want >= 1), from each other >= 500: {bellFar}, '
          f'work blocks around {around} (want >= 3), the first {bells[:1]}')
    ok &= good
    # Basements of private houses: a vertical ladder of 3 blocks (wy-3..wy-1), a cobblestone floor under it, a 3x3 room 2 high;
    # inside a chest or mobs (zombie/skeleton/spider in entities).
    lad = set()
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0] == 'minecraft:ladder' for k in entries): continue
            for i in range(4096):
                if decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15) == 'minecraft:ladder':
                    lad.add((cx * 16 + (i & 15), sy * 16 + ((i >> 8) & 15), cz * 16 + ((i >> 4) & 15)))
    bsm = []
    for (x, y, z) in lad:
        if (x, y + 1, z) in lad or (x, y - 1, z) not in lad or (x, y - 2, z) in lad: continue
        if cell(x, y - 2, z) == 'minecraft:cobblestone' and ((cell(x, y + 1, z) == 'minecraft:iron_trapdoor' and cell(x - 1, y, z) == 'minecraft:lever') or (cell(x, y + 1, z) == 'minecraft:oak_trapdoor' and 'carpet' in str(cell(x, y + 2, z)))):
            bsm.append((x, y, z))
    bsChest = sum(1 for (x, y, z) in bsm
                  if any(cell(x + dx, y - 1, z + dz) in ('minecraft:chest',) for dx in range(-2, 3) for dz in range(0, 4)))
    bsMobs = [e for e in allEnt if e.get('id') in ('minecraft:zombie', 'minecraft:skeleton', 'minecraft:spider', 'minecraft:witch')]
    bsFloor = [sum(1 for dx in range(-4, 5) for dz in range(0, 7) if cell(x + dx, y - 2, z + dz) == 'minecraft:cobblestone') for (x, y, z) in bsm]
    bsCarpet = [sum(1 for dx in range(-1, 2) for dz in range(-1, 2) if 'carpet' in str(cell(x + dx, y + 2, z + dz))) for (x, y, z) in bsm]
    good = bool(bsm) and all(f >= 18 for f in bsFloor) and all(c >= 4 for c in bsCarpet) and sum(1 for c in bsCarpet if c >= 7) >= 0.7 * len(bsCarpet)
    print(f'  [{"OK " if good else "MISS"}] basement: floor cells {bsFloor} (want >= 18), carpet above the hatch {bsCarpet} of 9 (want >= 4, and >= 7 of 9 for 70% of basements)')
    ok &= good
    bsChestTbl = []
    for b in allBe:
        if b.get('id') == 'minecraft:chest' and any(abs(b['x'] - x) <= 6 and abs(b['z'] - (z + 1)) <= 6 and b['y'] == y - 1 for (x, y, z) in bsm):
            bsChestTbl.append(str(b.get('LootTable', '')))
    richN = sum(1 for t in bsChestTbl if any(k in t for k in ('shipwreck_treasure', 'desert_pyramid', 'pillager_outpost')))
    print(f'  basement chests: {len(bsChestTbl)}, expensive {richN}, mobs in the world {len(bsMobs)} (for reference: an expensive one has 2–3 mobs nearby)')
    magma = counts.get('minecraft:magma_block', 0)
    lavaN = counts.get('minecraft:lava', 0)
    good = len(bsm) >= 1 and (bsChest + len(bsMobs)) >= 1
    print(f'  [{"OK " if good else "MISS"}] basements of private houses: descent ladders {len(bsm)} (want >= 1), with a chest {bsChest}, mobs {len(bsMobs)}, the first descent {sorted(bsm)[:1]}')
    ok &= good
    signBe = [b for b in allBe if b.get('id') == 'minecraft:sign']
    sgN = counts.get('minecraft:oak_wall_sign', 0)
    import json as _json
    def _sgline(m):
        m = str(m)
        if m.startswith('{'):                                  # legacy: a JSON component (also read by clients before 1.21.5)
            try: return _json.loads(m).get('text', '')
            except Exception: return m
        return m
    sgTxt = [[_sgline(m) for m in ((b.get('front_text') or {}).get('messages') or [])][:3] for b in signBe]
    sgWax = all(b.get('is_waxed') == 1 for b in signBe)
    sgOk = any(t == ['ул.', 'Тестовая', '12А'] for t in sgTxt)
    sgBack = sum(1 for b in signBe if cell(b['x'], b['y'], b['z']) != 'minecraft:oak_wall_sign')
    good = sgN >= 1 and len(signBe) == sgN and sgOk and sgBack == 0 and sgWax
    print(f'  [{"OK " if good else "MISS"}] address signs: blocks {sgN}, BE {len(signBe)} (== blocks), '
          f'text "ул."/"Тестовая"/"12А" {sgOk}, BE without a block {sgBack} (want 0); texts {sgTxt[:3]}, the first {[(b["x"], b["y"], b["z"]) for b in signBe[:1]]}')
    ok &= good
    postS = [t for t in sgTxt if t[:2] in (['ул.', 'Первая'], ['пр.', 'Вторая'], ['пер.', 'Третий'])]
    postNames = {tuple(t[:2]) for t in postS}
    swN = counts.get('minecraft:stone_brick_wall', 0)
    good = len(postNames) >= 2 and swN >= 4
    print(f'  [{"OK " if good else "MISS"}] signposts at intersections: street signs {len(postS)}, different names {len(postNames)} (want >= 2), stone_brick_wall blocks {swN} (want >= 4)')
    ok &= good
    # An elevator shaft on the host's roof: purple blocks (the shaft) only above the light blue ones (the host).
    mgY = []; lbY = []
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            nms = [k[0] for k in entries]
            hasM = 'minecraft:magenta_concrete' in nms; hasL = 'minecraft:light_blue_concrete' in nms
            if not (hasM or hasL): continue
            for i in range(4096):
                nm = decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15)
                if nm == 'minecraft:magenta_concrete': mgY.append(sy * 16 + ((i >> 8) & 15))
                elif nm == 'minecraft:light_blue_concrete': lbY.append(sy * 16 + ((i >> 8) & 15))
    mgByY = {}
    for y in mgY: mgByY[y] = mgByY.get(y, 0) + 1
    topY = max(mgY) if mgY else 0
    roof1 = bool(mgY) and mgByY.get(topY - 1, 0) < mgByY.get(topY, 0)          # under the ceiling only the wall remains, not a second slab
    wallsUp = bool(mgY) and all(mgByY.get(y, 0) >= 8 for y in range(min(mgY), topY + 1))   # the wall is solid up to the ceiling
    good = bool(mgY) and bool(lbY) and min(mgY) > max(lbY) and roof1 and wallsUp
    print(f'  [{"OK " if good else "MISS"}] elevator shaft on the roof: purple blocks {len(mgY)} (bottom y={min(mgY) if mgY else None}), host blocks {len(lbY)} (top y={max(lbY) if lbY else None}); want the shaft bottom > the host top; ceiling 1 block {roof1}, walls up to the ceiling {wallsUp}, layers {sorted(mgByY.items())}')
    ok &= good
    # A rdcpol parking lot: dark gray concrete in the top layer + light gray marking carpets on it; the lines do not cross
    # (a marking carpet has no more than 2 neighbors on 4 sides — at a crossing/T-junction there would be 3-4).
    lgc = set()
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0] == 'minecraft:white_carpet' for k in entries): continue
            for i in range(4096):
                if decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15) == 'minecraft:white_carpet':
                    lgc.add((cx * 16 + (i & 15), sy * 16 + ((i >> 8) & 15), cz * 16 + ((i >> 4) & 15)))
    onConc = {c for c in lgc if cell(c[0], c[1] - 1, c[2]) == 'minecraft:gray_concrete'}
    crossBad = sum(1 for (x, y, z) in onConc
                   if sum(1 for d in ((1, 0), (-1, 0), (0, 1), (0, -1)) if (x + d[0], y, z + d[1]) in onConc) >= 3)
    nConc = counts.get('minecraft:gray_concrete', 0)
    good = len(onConc) >= 15 and crossBad == 0 and nConc >= 150
    print(f'  [{"OK " if good else "MISS"}] rdcpol parking lot: marking carpets on concrete {len(onConc)} (want >= 15), with a crossing {crossBad} (want 0), gray_concrete {nConc} (want >= 150)')
    ok &= good
    # A school board: black concrete, a height of exactly 3, a width >= 4 in a line (replaces wall blocks)
    blk = set()
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0] == 'minecraft:black_concrete' for k in entries): continue
            for i in range(4096):
                if decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15) == 'minecraft:black_concrete':
                    blk.add((cx * 16 + (i & 15), sy * 16 + ((i >> 8) & 15), cz * 16 + ((i >> 4) & 15)))
    boards = []
    for (x, y, z) in blk:
        if (x, y - 1, z) in blk: continue                                   # the start of a board column
        h = 0
        while (x, y + h, z) in blk: h += 1
        boards.append((x, y, z, h))
    good = len(boards) >= 4 and all(b[3] == 3 for b in boards)               # a board: a height of exactly 3, a width (columns) >= 4
    print(f'  [{"OK " if good else "MISS"}] school board (black_concrete): blocks {len(blk)}, columns {len(boards)} (want >= 4), heights {sorted({b[3] for b in boards})} (want [3])')
    ok &= good
    banBe = [b for b in allBe if b.get('id') == 'minecraft:banner']
    raiders = {k: sum(1 for e in allEnt if e.get('id') == 'minecraft:' + k) for k in ('pillager', 'vindicator', 'evoker')}
    firstBan = [(b['x'], b['y'], b['z']) for b in banBe[:1]]
    firstPil = [e.get('Pos') for e in allEnt if e.get('id') == 'minecraft:pillager'][:1]
    firstEnd = [e.get('Pos') for e in allEnt if e.get('id') == 'minecraft:enderman'][:1]
    print('  [info] the first banner', firstBan, 'the first pillager', firstPil, 'enderman', firstEnd)
    capN = sum(1 for e in allEnt if e.get('id') == 'minecraft:pillager' and e.get('PatrolLeader') == 1)
    print(f'  bandit lairs (for reference; MCX_FORCE_LAIR=1 enables all): banners {len(banBe)} (each has {len((banBe[0].get("patterns") or [])) if banBe else 0} patterns), '
          f'raiders {raiders}, banner bearers {capN}, hay_block {counts.get("minecraft:hay_block", 0)}, black glass {counts.get("minecraft:black_stained_glass", 0)}, white_wall_banner {counts.get("minecraft:white_wall_banner", 0)}')
    # a church dungeon: a ladder column >= 5 blocks, the maze floor (cobblestone) around its bottom, mob types
    ladCols = {}
    for (x, y, z) in lad: ladCols.setdefault((x, z), []).append(y)
    shafts = [(x, z, min(ys), max(ys)) for (x, z), ys in ladCols.items() if len(ys) >= 5 and max(ys) - min(ys) == len(ys) - 1]
    mazeFloor = 0
    for (x, z, y0, y1) in shafts[:1]:
        mazeFloor = sum(1 for dx in range(-8, 9) for dz in range(-8, 9) if cell(x + dx, y0 - 1, z + dz) == 'minecraft:cobblestone')
    mobsAll = {}
    for e in allEnt:
        i = str(e.get('id', ''))
        if i in ('minecraft:zombie', 'minecraft:skeleton', 'minecraft:spider', 'minecraft:witch', 'minecraft:enderman'): mobsAll[i.split(':')[1]] = mobsAll.get(i.split(':')[1], 0) + 1
    for (x, z, y0, y1) in shafts[:1]:
        for zz in range(z - 2, z + 30):
            print('  [maze] ' + ''.join(('c' if cell(xx, y0, zz) == 'minecraft:chest' else ('#' if (cell(xx, y0 - 1, zz) == 'minecraft:cobblestone' and cell(xx, y0, zz) in ('minecraft:air', 'minecraft:ladder')) else '.')) for xx in range(x - 14, x + 22)))
    print(f'  church dungeon (for reference): ladder shafts >=5 blocks {len(shafts)}, maze floor cells at the bottom {mazeFloor}, mobs {mobsAll}')
    cy = set()
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0] == 'minecraft:cyan_concrete' for k in entries): continue
            for i in range(4096):
                if decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15) == 'minecraft:cyan_concrete':
                    cy.add((cx * 16 + (i & 15), sy * 16 + ((i >> 8) & 15), cz * 16 + ((i >> 4) & 15)))
    # A church of two parts (a nave + a bell tower from the south): the wall between them (z~188, x 3..9) at heights 2..5 is removed
    wallBetween = sum(1 for x in range(3, 10) for z in range(186, 191) for y in (2, 3, 4, 5) if cell(x, y, z) not in ('minecraft:air', 'minecraft:oak_door', 'minecraft:ladder', 'minecraft:cave_air') and not str(cell(x, y, z)).endswith('_door'))
    print(f'  [info] church complex: blocks in the junction zone of the nave and the bell tower {wallBetween} (for reference)')
    # An industrial zone complex (3 parts: 12, 8 and 16 m): the wall between parts A and B (x249..250) is removed at heights below the B roof (except floor slabs),
    # above the B roof it stays (the outer one); the floor slabs go through both parts.
    if cy:
        inner = sum(1 for z in range(-133, -121) for y in (1, 2, 3, 5, 6) for x in (249, 250) if (x, y, z) in cy)
        above = sum(1 for z in range(-133, -121) for y in (9, 10, 11) for x in (249, 250) if (x, y, z) in cy)
        slabsAB = sum(1 for z in range(-133, -121) for x in range(236, 262) if (x, 8, z) in cy)
        from collections import Counter as _C
        _cn = _C(cell(x, y, z) for x in range(230, 265) for z in range(-146, -119) for y in range(0, 5))
        print('  [info] complex blocks y0..4:', dict(_cn.most_common(25)))
        good = inner == 0 and above >= 12 and slabsAB >= 120
        print(f'  [{"OK " if good else "MISS"}] industrial zone: the inner wall between the parts {inner} (want 0), the outer wall above the roof of the low part {above} (want >= 12), the slab y8 through both parts {slabsAB} (want >= 120)')
        ok &= good
    _first = {}
    _want = ('minecraft:anvil', 'minecraft:blast_furnace', 'minecraft:grindstone', 'minecraft:stonecutter', 'minecraft:lava_cauldron')
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0] in _want for k in entries): continue
            for i in range(4096):
                nm = decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15)
                if nm in _want and nm not in _first:
                    _first[nm] = (cx * 16 + (i & 15), sy * 16 + ((i >> 8) & 15), cz * 16 + ((i >> 4) & 15))
    print('  [info] the first blocks of the industrial zone:', _first)
    print('  [info] industrial zone interiors: ' + ', '.join(f'{k.split(":")[1]} {counts.get(k, 0)}' for k in ('minecraft:anvil', 'minecraft:blast_furnace', 'minecraft:grindstone', 'minecraft:stonecutter', 'minecraft:lava_cauldron', 'minecraft:blackstone', 'minecraft:lava', 'minecraft:iron_block')))
    dp = counts.get('minecraft:dirt_path', 0); cd = counts.get('minecraft:coarse_dirt', 0)
    good = dp >= 20
    print(f'  [{"OK " if good else "MISS"}] trampled earth under the square: dirt_path {dp}, coarse_dirt {cd} (want dirt_path >= 20)')
    ok &= good
    print(f'  beds in total: {counts.get("minecraft:red_bed",0)+counts.get("minecraft:white_bed",0)} (for reference)')
    btn = counts.get('minecraft:stone_button', 0)
    irond = counts.get('minecraft:iron_door', 0) // 2
    good = btn >= 1 and btn >= irond * 0.5
    print(f'  [{"OK " if good else "MISS"}] buttons at iron doors: stone_button {btn}, iron doors {irond} (want buttons >= half of the doors)')
    ok &= good
    good = magma >= 1 and lavaN >= 1
    print(f'  [{"OK " if good else "MISS"}] lava pits at the portal: magma {magma}, lava {lavaN} (want >= 1 of both)')
    ok &= good
    # csrpol 51480000 (a lake 14x10, world x -120..-106, z 100..110): water in a pit and a fence around (like for sites).
    lakeW = sum(1 for x in range(-120, -105) for z in range(100, 111) for y in range(-8, 1)
                if cell(x, y, z) == 'minecraft:water')
    lakeF = sum(1 for x in range(-123, -102) for z in range(97, 114) for y in range(0, 4)
                if cell(x, y, z) in ('minecraft:oak_fence', 'minecraft:oak_fence_gate'))
    good = lakeW >= 100 and lakeF >= 20
    print(f'  [{"OK " if good else "MISS"}] csrpol lake 51480000: water {lakeW} (want >= 100), a fence around {lakeF} (want >= 20)')
    ok &= good
    # Iron golems on fenced csrpol sites (a chance of 15%, a field of 308 sites => tens).
    golems = sum(1 for e in allEnt if e.get('id') == 'minecraft:iron_golem')
    good = golems >= 40
    print(f'  [{"OK " if good else "MISS"}] iron golems on sites: {golems} (want >= 40: 2 rolls of 25% on each of 308 sites => ~150)')
    ok &= good
    # Monster spawners (every 15th "city" floor, 50/50, 70/15/15): the high-rise 44400000 gives
    # floors 12..42 of the all-city counter -> hits 15 and 30 INSIDE it (two 50/50 rolls).
    # The scene is deterministic: the result is stable between rebuilds (when the geometry changes —
    # revise want). v2 (a remark "did not meet a single one"): ONE BLOCK minecraft:spawner —
    # there are no zombie/skeleton_spawner blocks in the game (the server turned them into air); the monster type is
    # in the BE "SpawnData"{id}. We check both the blocks and the BEs (BE==blocks, the type is valid, the type is really 1/2/3).
    spN = counts.get('minecraft:spawner', 0)
    spTypes = [(((b.get('SpawnData') or {}).get('entity') or {}).get('id')) or b.get('EntityId') or ''
               for b in spawnBe]
    spBad = sum(1 for t in spTypes if t not in ('minecraft:zombie', 'minecraft:skeleton',
                                                'minecraft:spider'))
    spSet = set(spTypes)
    print(f'  [{"OK " if spN >= 1 and len(spawnBe) == spN and not spBad else "MISS"}] '
          f'monster spawners: blocks {spN} (want >= 1 — a high-rise of >15 floors), BE {len(spawnBe)} '
          f'(== blocks), broken types {spBad} (want 0), sets {sorted(spSet)}')
    ok &= spN >= 1 and len(spawnBe) == spN and spBad == 0
    # Portals to the Nether (the result: csrpol — NO; only vegpol forests 71111110/71126000, >= 5000
    # real m², one portal per forest and not closer than 1000 blocks; 10% active).
    # ACTIVE: 4 minecraft:portal blocks (2×2) in a frame of 14 obsidians — the portal blocks are
    # a multiple of 4; "ruined": the bottom row of the frame is whole, the posts/top with holes, mixed with
    # crying_obsidian. The test scene: three candidate forests (A 8000 m² — the only winner,
    # B 6300 — next to A, C 1200 — too small), hence exactly one frame; active 10% by hash —
    # want from the actual deterministic outcome (when the scene is rebuilt — revise).
    # church altar candles: under each — obsidian (we take it into account when counting the obsidian of portal frames)
    candles = []
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0] == 'minecraft:candle' for k in entries): continue
            for i in range(4096):
                if decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15) == 'minecraft:candle':
                    candles.append((cx * 16 + (i & 15), sy * 16 + ((i >> 8) & 15), cz * 16 + ((i >> 4) & 15)))
    candObs = sum(1 for (x, y, z) in candles if cell(x, y - 1, z) == 'minecraft:obsidian')
    good = len(candles) >= 3 and candObs == len(candles)
    print(f'  [{"OK " if good else "MISS"}] church altars: candles {len(candles)} (want >= 3), all on obsidian: {candObs == len(candles)}; '
          f'pressure plates (oak_pressure_plate) {counts.get("minecraft:oak_pressure_plate", 0)}, bookshelf {counts.get("minecraft:bookshelf", 0)}')
    ok &= good
    # the obsidian of church altars (patterns: a row, a cross, a trident...) — connected obsidian areas on which candles stand
    altarObs = set()
    stack = [(x, y - 1, z) for (x, y, z) in candles if cell(x, y - 1, z) == 'minecraft:obsidian']
    while stack:
        q = stack.pop()
        if q in altarObs: continue
        altarObs.add(q)
        for d in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
            n = (q[0] + d[0], q[1] + d[1], q[2] + d[2])
            if n not in altarObs and cell(*n) == 'minecraft:obsidian': stack.append(n)
    # A portal frame = a connected area of obsidian/crying obsidian with the dimensions 4×5×1 (a frame 4 wide, 5 high), of any horizontal orientation;
    # church altars (of different patterns, including without candles) and the shopping center stand pedestals — other shapes and are not counted.
    obsP = {}
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if not any(k[0] in ('minecraft:obsidian', 'minecraft:crying_obsidian') for k in entries): continue
            for i in range(4096):
                _nmv = decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15)
                if _nmv in ('minecraft:obsidian', 'minecraft:crying_obsidian'):
                    obsP[(cx * 16 + (i & 15), sy * 16 + ((i >> 8) & 15), cz * 16 + ((i >> 4) & 15))] = _nmv
    obN = crN = 0
    _seenP = set()
    for _p0 in list(obsP):
        if _p0 in _seenP: continue
        _st = [_p0]; _seenP.add(_p0); _comp = []
        while _st:
            q = _st.pop(); _comp.append(q)
            for _dd in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
                _n = (q[0] + _dd[0], q[1] + _dd[1], q[2] + _dd[2])
                if _n in obsP and _n not in _seenP: _seenP.add(_n); _st.append(_n)
        _bb = (max(q[0] for q in _comp) - min(q[0] for q in _comp) + 1, max(q[1] for q in _comp) - min(q[1] for q in _comp) + 1,
              max(q[2] for q in _comp) - min(q[2] for q in _comp) + 1)
        if _bb[1] == 5 and sorted((_bb[0], _bb[2])) == [1, 4]:
            obN += sum(1 for q in _comp if obsP[q] == 'minecraft:obsidian')
            crN += sum(1 for q in _comp if obsP[q] == 'minecraft:crying_obsidian')
    poN = counts.get('minecraft:portal', 0)
    poAx = {v for nm, tp in props_seen if nm == 'minecraft:portal'
            for k, v in (tp or ()) if k == 'axis'}
    # Portals are now in FORESTS (vegpol 71111110/71126000, >= 5000 real m², one per 1000 blocks):
    # the test scene gives exactly one suitable winner forest (A) -> one frame of 14 obsidian blocks
    # (in a "ruined" one part of the posts/top is knocked out and crying obsidian is mixed in); active 10% by hash, so
    # portal blocks are 0 or 4. There are no more portals on csrpol — the gate "csrpol 10%/10%" was removed.
    poOk = (8 <= obN + crN <= 14 and (poN == 0 or poN == 4)
            and (not poAx or poAx <= {'x', 'z'}))
    print(f'  [{"OK " if poOk else "MISS"}] a portal in a forest (one per 1000 blocks): portal blocks {poN} '
          f'(want 0 or 4), obsidian+crying {obN + crN} (want 8..14 — one frame), '
          f'axis in the palette {sorted(poAx)}')
    ok &= poOk
    # Shopping center mannequins = armor_stand entities (a request: "put mannequins… sometimes with armor").
    # They have no BE gates — we check the Entities tag: stands exist (we want >= 2 — the shopping center has two tiers),
    # Pos inside the shopping center, at least one with an item in equipment (otherwise "sometimes" is not met).
    stands = [e for e in allEnt if e.get('id') == 'minecraft:armor_stand']
    withArmor = 0
    badPos = 0
    badId = 0
    floatStands = 0                             # a stand without a floor under its feet (NoGravity — hangs)
    ARM = {f'minecraft:{m}_{p}' for m in ('leather', 'chainmail', 'iron', 'golden', 'diamond')
           for p in ('boots', 'leggings', 'chestplate', 'helmet')}
    for e in stands:
        pos = e.get('Pos') or []
        if len(pos) == 3 and cell(int(pos[0]), int(pos[1]) - 1, int(pos[2])) == 'minecraft:air':
            floatStands += 1
        for s in list((e.get('equipment') or {}).values()):
            if s and s.get('id') and str(s['id']) not in ARM:
                badId += 1                          # the server throws out an item with a broken id
        if len(pos) == 3 and ((276 <= pos[0] <= 314 and -212 <= pos[2] <= -176)          # the small shopping center
                              or (310 <= pos[0] <= 360 and -175 <= pos[2] <= -125)):     # the big shopping center 40x40
            if any((s or {}).get('id') for s in list((e.get('equipment') or {}).values())):
                withArmor += 1
        else:
            badPos += 1
    # Mannequin armor wear: EVERY item keeps 2 to 36% of durability (the component minecraft:damage).
    MAXD = {'leather': (65, 75, 80, 55), 'chainmail': (195, 225, 240, 165), 'iron': (195, 225, 240, 165),
            'golden': (91, 105, 112, 77), 'diamond': (429, 495, 528, 363)}
    PARTS = ('boots', 'leggings', 'chestplate', 'helmet')
    durN = durBad = 0
    for e in stands:
        for it in list((e.get('equipment') or {}).values()):
            if not it or not it.get('id'): continue
            mat, part = str(it['id']).replace('minecraft:', '').rsplit('_', 1)
            mx = MAXD[mat][PARTS.index(part)]
            dm = (it.get('components') or {}).get('minecraft:damage')
            durN += 1
            if dm is None or not (mx - round(mx * 0.36) - 1 <= dm <= mx - 1): durBad += 1
    armOld = sum(1 for e in stands for it in (e.get('ArmorItems') or []) if it and it.get('id'))
    print(f'  ArmorItems (old format, legacy only): items {armOld} (for reference: legacy > 0, 26.x == 0)')
    print(f'  [{"OK " if durN > 0 and durBad == 0 else "MISS"}] mannequin armor wear: items {durN}, outside 2..36% of durability {durBad} (want 0)')
    ok &= durN > 0 and durBad == 0
    print(f'  [{"OK " if len(stands) >= 2 and withArmor >= 1 and not badPos and not badId and not floatStands else "MISS"}] '
          f'mannequins (armor_stand): {len(stands)} (want >= 2, shopping center), with armor {withArmor} '
          f'(want >= 1), outside the shopping center {badPos} (want 0), broken armor ids {badId} (want 0), '
          f'without a floor {floatStands} (want 0)')
    ok &= len(stands) >= 2 and withArmor >= 1 and badPos == 0 and badId == 0 and floatStands == 0
    # Aviation beacons (>15 floors: tiers k=15,20,25,30 — a torch OUTSIDE the facade, a pair of diagonal
    # corners). We determine the high-rise outline DIRECTLY from the zip: the walking row of the facade — a continuous slab of
    # light_gray_concrete, there are no neighboring buildings next to the high-rise. The ROW IS ADAPTIVE: flat — y=2, noise
    # — the first one found in 62..74 (the city on a ledge, y=2 there is underground — with a hard two
    # the beacons in noise archives "disappeared", although they were exported honestly). Torches are counted
    # STRICTLY outside the outline (inside — the regular lighting). The scan of y: 30..280 (the noise tier k=30
    # reaches 256 — the former ceiling of 252 cut off a whole row).
    tb = None
    for fy in [2] + list(range(62, 75)):        # the walking row of the facade: flat y=2, noise ~67..74
        t = None
        for x in range(160, 196):
            for zz in range(-110, -84):
                if cell(x, fy, zz) == 'minecraft:light_gray_concrete':
                    t = [x, x, zz, zz] if t is None else \
                        [min(t[0], x), max(t[1], x), min(t[2], zz), max(t[3], zz)]
        if t is not None and tb is None:
            tb = t
    beac = 0
    if tb:
        for x in range(tb[0] - 3, tb[1] + 4):
            for zz in range(tb[2] - 3, tb[3] + 4):
                if tb[0] <= x <= tb[1] and tb[2] <= zz <= tb[3]:
                    continue                                      # an inner cell — not a beacon
                for y in range(30, 280):                          # noise: the beacon tier reaches 256
                    if cell(x, y, zz) == 'minecraft:wall_torch': beac += 1
    print(f'  [{"OK " if beac >= 2 else "MISS"}] aviation beacons: torches outside the high-rise outline {beac} (want >= 2)'
          + (f' [outline x {tb[0]}..{tb[1]} z {tb[2]}..{tb[3]}]' if tb else ' [OUTLINE NOT FOUND]'))
    ok &= beac >= 2
    # Dungeons (a user request): the underground of generated chunks is filled with caves from
    # cave_templates.bin — BUT ONLY IN NOISE (a user decision): in a flat world there is no terrain and
    # no seed, the underground must be solid rock (caves there only bit through the crust —
    # "bad" columns). Before the checkbox y<-32 was solid stone (0 air). The window of sections -4..-3
    # (y<-32) is reliable: all the extractor templates have air there (inspect_tpl measurements).
    if flatWorld:
        # Flat without caves (a user decision): the underground — solid rock. The map edge chamfer
        # legally breathes at y=-1/-2, so the sign of caves is deep air (secAir -4/-3).
        c_ok = (not caves)
        print(f'  [{"OK " if c_ok else "MISS"}] flat without caves (a user decision): air y<-32 '
              f'{secAir.get(-4, 0) + secAir.get(-3, 0)} (want 0)')
    else:
        deepTot = 4096 * 2 * len(chunks)
        deepAir = secAir.get(-4, 0) + secAir.get(-3, 0)
        frac = deepAir / deepTot if deepTot else 0
        # Coverage: in EVERY chunk underground (y<0) there is at least one cave cell — "all the generated chunks
        # must be filled". The window of sections -4..-1 (y<0): every template has air there.
        covMiss = sum(1 for k in chunks if k not in deepChunk)
        ds = counts.get('minecraft:deepslate', 0) + counts.get('minecraft:tuff', 0)
        c_ok = caves and 0.005 <= frac <= 0.45 and covMiss == 0 and ds > 0.05 * deepTot
        print(f'  [{"OK " if c_ok else "MISS"}] dungeons: air y<-32 {deepAir}/{deepTot} ({frac:.1%}, want 0.5..45%), '
              f'chunks without caves {covMiss} (want 0), deepslate+tuff {ds}')
    ok &= c_ok
    if env == 'noise':
        # the "normal world": the whole export is raised above sea level (62) — there must be no turf below y=60
        gmin = None
        for (cx, cz), secs in chunks.items():
            for sy, (entries, data) in secs.items():
                if sy < 3: continue
                if not any(k[0] == 'minecraft:grass_block' for k in entries): continue
                for i in range(4096):
                    if decode(entries, data, i & 15, (i >> 8) & 15, (i >> 4) & 15) == 'minecraft:grass_block':
                        y = sy * 16 + ((i >> 8) & 15)
                        if gmin is None or y < gmin: gmin = y
        print(f'  noise plane: min grass y = {gmin}')
        ok &= gmin is not None and gmin >= 60
    return ok

if __name__ == '__main__':   # otherwise import for the sake of parse/chunk_iter runs the whole check (see findchest.py)
    # Noise archives are NOT decoded by default (a user decision) — only the environment
    # config; they are checked by the server (TESTS.md → smoke_mc.py). --decode-noise enables the full
    # parsing of them too (long: cave sections × 5880 chunks). Flat archives are always full.
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    DN = '--decode-noise' in sys.argv
    print('== native ==')
    ok1 = check(args[0] if len(args) > 0 else os.path.join(WORLDS, 'mc_test.zip'), legacy=False)
    print('== legacy ==')
    ok2 = check(args[1] if len(args) > 1 else os.path.join(WORLDS, 'mc_test_legacy.zip'), legacy=True)
    print('== native noise ==')
    ok3 = check(args[2] if len(args) > 2 else os.path.join(WORLDS, 'mc_test_noise.zip'), legacy=False,
                env='noise', full=DN)
    print('== legacy noise ==')
    ok4 = check(args[3] if len(args) > 3 else os.path.join(WORLDS, 'mc_test_legacy_noise.zip'), legacy=True,
                env='noise', full=DN)
    print('== secret passages (stronghold + Ancient City) ==')
    import verify_lairs
    ok5 = verify_lairs.check(args[2] if len(args) > 2 else os.path.join(WORLDS, 'mc_test_noise.zip'))
    ok4 = ok4 and ok5
    print('== floating islands above parks, school and kindergarten zones ==')
    import verify_islands
    ok4 = ok4 and verify_islands.check(os.path.join(WORLDS, 'mc_test.zip'))
    print('== stairs between floors: the approaches are free ==')
    import verify_stairs
    ok4 = ok4 and verify_stairs.check(os.path.join(WORLDS, 'mc_test.zip'))
    print('== portal ruins: decoration and /locate ==')
    import verify_portal
    ok4 = ok4 and verify_portal.check(os.path.join(WORLDS, 'mc_test.zip'))
    print('== science buildings: underground laboratories ==')
    import verify_lab
    ok4 = ok4 and verify_lab.check(os.path.join(WORLDS, 'mc_test_noise.zip'))
    print('== bandit outposts and abandoned camps ==')
    import verify_patches
    ok4 = ok4 and verify_patches.check(os.path.join(WORLDS, 'mc_test_noise.zip'))
    print('== blkpol (asphalt, square) ==')
    import verify_blkpol
    ok4 = ok4 and verify_blkpol.check(os.path.join(WORLDS, 'mc_test.zip'))
    print('== carpet compositions ==')
    import verify_rugs
    ok4 = ok4 and verify_rugs.check(os.path.join(WORLDS, 'mc_test.zip'))
    print('== terrain (valleys are preserved, spawn on the surface) ==')
    import verify_relief
    ok4 = ok4 and verify_relief.check(os.path.join(WORLDS, 'mc_test_relief.zip'))
    print('OKS', ok1, ok2, ok3, ok4)
    print('RESULT', 'PASS' if (ok1 and ok2 and ok3 and ok4) else 'FAIL')
    sys.exit(0 if (ok1 and ok2 and ok3 and ok4) else 1)
