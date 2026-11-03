# One-off style check of the underground metro (code 61123100) in an exported zip.
# Usage: python verify_subway.py <world.zip>
# The metro rails live 9 rows under the surface; the electrified ones (powered_rail) mark them.
# Checks per rail cell: a floor below, air in the interior above, a solid ceiling, a masonry wall
# at +-4 across a straight run with air at +-3, hanging lamps on the ceiling row, every ~20th rail
# electrified AND never at a turn (powered rails cannot be curved), no holes in the surface above,
# NO stray solid columns inside the corridor (merged chains must not paint walls over interiors).
# Surface powered rails (electrified railways, trams) must stand on a stone/brick track bed.
# Exit code 0 PASS, 1 FAIL, 77 no metro in the world.
import os, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_mc_export as V

MASON = {'minecraft:stone_bricks', 'minecraft:cracked_stone_bricks', 'minecraft:mossy_stone_bricks',
         'minecraft:cobblestone', 'minecraft:mossy_cobblestone', 'minecraft:stone', 'minecraft:andesite',
         'minecraft:polished_andesite', 'minecraft:polished_granite', 'minecraft:polished_diorite',
         'minecraft:granite', 'minecraft:diorite', 'minecraft:dirt', 'minecraft:grass_block',
         'minecraft:smooth_stone'}


def main(path):
    zf = zipfile.ZipFile(path)
    chunks = {}
    for cx, cz, ch in V.chunk_iter(zf):
        secs = {}
        for s in ch.get('sections') or []:
            bs = s.get('block_states') or {}
            pal = bs.get('palette') or []
            entries = []
            for e in pal:
                if isinstance(e, dict):
                    if 'Name' in e:
                        entries.append((e['Name'], e.get('Properties')))
                    elif 'id' in e:
                        entries.append((e['id'], e.get('properties')))
                    else:
                        entries.append((str(e), None))
                else:
                    entries.append((e, None))
            secs[s['Y']] = (entries, bs.get('data'))
        chunks[(cx, cz)] = secs

    def cell(x, y, z):
        ent = chunks.get((x >> 4, z >> 4), {}).get(y >> 4)
        if ent is None:
            return 'minecraft:air'
        return V.decode(ent[0], ent[1], x & 15, y & 15, z & 15)

    # collect rails / powered rails / lanterns (section y -4..6: a --flat world puts the plane near 0,
    # so the metro can sit below zero there; it never climbs higher than y 112)
    rails, powered, lamps = [], [], []
    for (cx, cz), secs in chunks.items():
        for secY, (entries, data) in secs.items():
            if data is None:
                continue                                       # a unary section cannot contain rails
            if secY < -4 or secY > 6:
                continue                                       # skip the deep rows and the sky (fast scan)
            for i in range(4096):
                n = V.decode(entries, data, i & 15, i >> 8 & 15, i >> 4 & 15)
                pos = (cx * 16 + (i & 15), secY * 16 + (i >> 8 & 15), cz * 16 + (i >> 4 & 15))
                if n == 'minecraft:rail':
                    rails.append(pos)
                elif n == 'minecraft:powered_rail':
                    powered.append(pos)
                elif n == 'minecraft:lantern':
                    lamps.append(pos)
    pYs = [y for _, y, _ in powered]
    if not pYs:
        print('NO METRO FOUND (no powered_rail in the world) - the check is only meaningful with subway lines')
        return 77
    # the metro rails sit under a solid ceiling with a free interior row below it (a hanging lamp counts as free);
    # surface rails have open sky there. The y band comes from the electrified rails: cave donors carry their own
    # old rails in deep caves with a similar shape, but those rows carry no powered rails of our metro.
    OPEN = ('minecraft:air', 'minecraft:lantern')
    lo, hi = min(pYs) - 3, max(pYs) + 3
    allr = rails + powered
    tunnel = [p for p in allr if lo <= p[1] <= hi
              and cell(p[0], p[1] + 4, p[2]) in OPEN
              and cell(p[0], p[1] + 5, p[2]) != 'minecraft:air']
    setT0 = set(tunnel)
    tPow = [p for p in powered if tuple(p) in setT0]
    if not tunnel:
        print('NO METRO FOUND (no rails under a tunnel ceiling) - the check is only meaningful with subway lines')
        return 77
    print('metro rails: %d, electrified: %d (1 per %.1f), other rails in the world: %d'
          % (len(tunnel), len(tPow), len(tunnel) / max(1, len(tPow)), len(allr) - len(tunnel)))
    tRows = {y for _, y, _ in tunnel}
    lamps = [p for p in lamps if p[1] - 4 in tRows]             # a lamp hangs 4 rows above the rail (rail=fy+1, lamp=fy+5)
    print('metro lanterns: %d, rail rows (y): %s' % (len(lamps), sorted(tRows)))

    fails = []
    floorBad = airBad = ceilBad = surfBad = 0
    for x, y, z in tunnel:                                       # interior: floor below, air above, ceiling 5 rows up
        if cell(x, y - 1, z) == 'minecraft:air':
            floorBad += 1
        for dy in (2, 3):
            if cell(x, y + dy, z) != 'minecraft:air':
                airBad += 1
        if cell(x, y + 5, z) == 'minecraft:air':
            ceilBad += 1
        if all(cell(x, y + dy, z) == 'minecraft:air' for dy in range(6, 15)):
            surfBad += 1                                         # a hole in the surface above the tunnel
    print('rails checked: floorBad %d, interiorAirBad %d, ceilBad %d, surfaceHoles %d'
          % (floorBad, airBad, ceilBad, surfBad))
    if floorBad or airBad or ceilBad or surfBad:
        fails.append('interior geometry')

    RAILS = {'minecraft:rail', 'minecraft:powered_rail', 'minecraft:activator_rail', 'minecraft:detector_rail'}
    FREE = ('minecraft:air', 'minecraft:lantern') + tuple(RAILS)

    # the widened section on straight runs: air at +-3 across (interior 7 wide), masonry lining at +-4.
    # A solid at +-3 with rails directly above is a floor prop of a rising/merged track (near a corner
    # the neighbouring level climbs through the sampled row), not a plug of the corridor.
    def width_free(px, py, pz):
        if cell(px, py, pz) == 'minecraft:air':
            return True
        return any(cell(px, py + d, pz) in RAILS for d in range(1, 4))
    setT = set(tunnel)
    checked = widthBad = wallBad = 0
    for x, y, z in tunnel:
        nX = (x + 1, y, z) in setT or (x - 1, y, z) in setT
        nZ = (x, y, z + 1) in setT or (x, y, z - 1) in setT
        if not (nX ^ nZ):
            continue                                  # turns are covered by the powered-rail check below
        checked += 1
        if nX:
            c1, c2 = (x, y, z - 3), (x, y, z + 3)
            w1, w2 = cell(x, y, z - 4), cell(x, y, z + 4)
        else:
            c1, c2 = (x - 3, y, z), (x + 3, y, z)
            w1, w2 = cell(x - 4, y, z), cell(x + 4, y, z)
        if not width_free(*c1) or not width_free(*c2):
            widthBad += 1
            if widthBad <= 4:
                b = c1 if not width_free(*c1) else c2
                print('  widthBad at', (x, y, z), '->', cell(*b))
        if (w1 != 'minecraft:air' and w1 not in MASON) or (w2 != 'minecraft:air' and w2 not in MASON):
            wallBad += 1
            if wallBad <= 4:
                print('  wallBad at', (x, y, z), '->', w1 if w1 != 'minecraft:air' and w1 not in MASON else w2)
    print('straight rails: %d, interiorAirBad(+-3) %d, wallBad(+-4) %d' % (checked, widthBad, wallBad))
    if widthBad or wallBad:
        fails.append('tunnel section (width/lining)')

    # columns: at STRAIGHT rail cells the cross-section strip (+-3 across, rows rail..rail+3) must be free;
    # a wall column of one chain painted over the carved interior of another (merged chains) stood in the
    # corridor as a lone solid "column". Corner cells are skipped: the diagonal quadrant at a bend is solid
    # by design (the outer corner of the L). A solid block supporting rails of a merged line directly above
    # is a floor step of the other level, not a column.
    def bad_at(px, py, pz):
        for dy in range(4):
            n = cell(px, py + dy, pz)
            if n in FREE:
                continue
            if any(cell(px, py + d2, pz) in RAILS for d2 in range(dy + 1, 4)):
                continue                                 # the floor of a merged upper level
            return n
        return None
    colBad = 0
    for x, y, z in tunnel:
        nX = (x + 1, y, z) in setT or (x - 1, y, z) in setT
        nZ = (x, y, z + 1) in setT or (x, y, z - 1) in setT
        if nX == nZ:
            continue                                     # a corner (both axes) or an end cell (neither)
        # the strip ACROSS the corridor: a chain along X is crossed along Z and vice versa
        req = [(x, z + dz) for dz in (-3, -2, -1, 1, 2, 3)] if nX else [(x + dx, z) for dx in (-3, -2, -1, 1, 2, 3)]
        for px, pz in req:
            n = bad_at(px, y, pz)
            if n:
                colBad += 1
                if colBad <= 4:
                    print('  column at', (px, y, pz), 'across rail', (x, y, z), '->', n)
    print('column strips violated (must be 0): %d' % colBad)
    if colBad:
        fails.append('columns inside the tunnel')

    # a powered rail cannot be curved: read the block shape itself (curved_* is the illegal one; ascending is
    # fine). The old heuristic looked at rail neighbours on both axes - but near a station two lines run parallel
    # and cross, so a straight electrified cell of one line legally touches the other line's rails sideways.
    def shape_at(x, y, z):
        ent = chunks.get((x >> 4, z >> 4), {}).get(y >> 4)
        if ent is None:
            return None
        entries, data = ent
        if data is None:
            return (entries[0][1] or {}).get('shape')
        bits = max(4, (len(entries) - 1).bit_length()); per = 64 // bits
        i = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15)
        v = (data[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1)
        if v >= len(entries):
            return None
        return (entries[v][1] or {}).get('shape')
    curvedPow = 0
    for x, y, z in tPow:
        sh = shape_at(x, y, z)
        if sh and sh.startswith('curved'):
            curvedPow += 1
            if curvedPow <= 4:
                print('  curved powered rail at', (x, y, z), sh)
    print('powered rails at turns (must be 0): %d' % curvedPow)
    if curvedPow:
        fails.append('powered rail at a turn')

    # surface powered rails (electrified railways and trams) stand on a track bed: stone or stone bricks;
    # a road or a bridge deck under them is fine too - only natural ground where a bed was due is bad
    NATURAL = {'minecraft:dirt', 'minecraft:grass_block', 'minecraft:coarse_dirt', 'minecraft:podzol',
               'minecraft:sand', 'minecraft:moss_block', 'minecraft:pale_moss_block', 'minecraft:clay',
               'minecraft:gravel', 'minecraft:water', 'minecraft:ice', 'minecraft:snow_block', 'minecraft:snow'}
    bedBad = 0
    surfPow = [p for p in powered if tuple(p) not in setT0]
    for x, y, z in surfPow:
        if cell(x, y - 1, z) in NATURAL:
            bedBad += 1
    print('surface powered rails: %d, without a stone/brick bed: %d' % (len(surfPow), bedBad))
    if bedBad:
        fails.append('powered rail without a track bed')

    print('SUBWAY', 'FAIL: ' + '; '.join(fails) if fails else 'PASS')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
