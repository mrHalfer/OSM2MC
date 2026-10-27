# -*- coding: utf-8 -*-
# There is no furniture in front of inter-floor stairs (stone stairs, ladders): within 1 cell of a stair/ladder at its heights there is no furniture.
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V

STAIRS = ('minecraft:stone_stairs', 'minecraft:ladder')
FURN = ('minecraft:bookshelf', 'minecraft:crafting_table', 'minecraft:cartography_table', 'minecraft:furnace', 'minecraft:barrel',
        'minecraft:cauldron', 'minecraft:composter', 'minecraft:loom', 'minecraft:grindstone', 'minecraft:smithing_table',
        'minecraft:oak_fence', 'minecraft:oak_pressure_plate', 'minecraft:oak_stairs')


def check(path):
    zf = zipfile.ZipFile(path)
    chunks = {}
    for cx, cz, ch in V.chunk_iter(zf):
        secs = {}
        for s in ch.get('sections') or []:
            bs = s['block_states']
            nm = [e if isinstance(e, str) else (e.get('Name') or e.get('id') or e.get('')) for e in bs['palette']]
            d = bs.get('data')
            if d is None:
                if nm[0] not in STAIRS and nm[0] not in FURN: continue
                secs[s['Y']] = (nm, None, 0, 0)
            else:
                bits = max(4, (len(nm) - 1).bit_length())
                secs[s['Y']] = (nm, d, bits, 64 // bits)
        chunks[(cx, cz)] = secs

    def cell(x, y, z):
        s = chunks.get((x >> 4, z >> 4), {}).get(y >> 4)
        if not s: return 'minecraft:air'
        nm, d, bits, per = s
        if d is None: return nm[0]
        i = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15)
        return nm[(d[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1)]

    nSt = bad = 0
    ex = []
    for (cx, cz), secs in chunks.items():
        for Y, (nm, d, bits, per) in secs.items():
            if d is None or Y < 4 or not any(n in STAIRS for n in nm): continue
            for i in range(4096):
                n = nm[(d[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1)]
                if n not in STAIRS: continue
                x, y, z = cx * 16 + (i & 15), Y * 16 + (i >> 8), cz * 16 + ((i >> 4) & 15)
                nSt += 1
                for dx in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for dy in (0, 1, 2):
                            if cell(x + dx, y + dy, z + dz) in FURN:
                                bad += 1
                                if len(ex) < 4: ex.append((x, y, z, cell(x + dx, y + dy, z + dz)))
    good = nSt > 0 and bad == 0
    print('  [%s] stairs between floors: stairs/ladders %d, furniture nearby %d (want 0) %s' % ('OK ' if good else 'MISS', nSt, bad, ex))
    return good


if __name__ == '__main__':
    sys.exit(0 if check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')) else 1)
