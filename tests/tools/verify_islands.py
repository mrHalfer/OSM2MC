# -*- coding: utf-8 -*-
# Floating islands above kindergartens (44313000): slime, tropics, a lake in the sky, slimes.
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V


def check(path):
    zf = zipfile.ZipFile(path)
    names = set()
    biomes = set()
    skyWater = 0                                   # sections where water adjoins the calcite/turf of the island
    for cx, cz, ch in V.chunk_iter(zf):
        for s in ch.get('sections') or []:
            pal = s['block_states']['palette']
            nm = [e if isinstance(e, str) else (e.get('id') or e.get('Name') or e.get('')) for e in pal]
            names.update(nm)
            biomes.update((s.get('biomes') or {}).get('palette') or [])
            if 'minecraft:water' in nm and 'minecraft:lily_pad' in nm:
                skyWater += 1
    glowTop = 0
    for cx, cz, ch in V.chunk_iter(zf):
        for s in ch.get('sections') or []:
            if s['Y'] < 0 or s['Y'] > 8: continue
            bs = s['block_states']
            nm = [e if isinstance(e, str) else (e.get('id') or e.get('Name') or e.get('')) for e in bs['palette']]
            if 'minecraft:glowstone' not in nm: continue
            d = bs.get('data')
            if d is None: continue
            bits = max(4, (len(nm) - 1).bit_length()); per = 64 // bits; gi = nm.index('minecraft:glowstone')
            for i in range(4096):
                if (d[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1) == gi: glowTop += 1
    animals = {}
    slimes = sizes = 0
    for root in V.ent_iter(zf):
        for e in root.get('Entities') or []:
            if e.get('id') in ('minecraft:wolf', 'minecraft:fox', 'minecraft:rabbit', 'minecraft:sheep', 'minecraft:pig', 'minecraft:chicken', 'minecraft:cow', 'minecraft:bee'):
                animals[e['id']] = animals.get(e['id'], 0) + 1
            if e.get('id') == 'minecraft:slime':
                slimes += 1
                sizes += e.get('Size', -1) >= 0
    ok = True
    def rep(good, msg):
        nonlocal ok
        print('  [%s] %s' % ('OK ' if good else 'MISS', msg))
        ok &= good
    need = ['minecraft:slime_block', 'minecraft:jungle_log', 'minecraft:jungle_leaves', 'minecraft:bamboo', 'minecraft:sugar_cane']
    miss = [n for n in need if n not in names]
    rep(not miss, 'an island above a park: slime, jungle, bamboo, reeds in the palette (missing: %s)' % (miss or '-'))
    rep(skyWater > 0, 'an island above a park: a lake in the sky (sections with water and lily pads: %d)' % skyWater)
    rep(slimes >= 3 and sizes == slimes, 'an island above a park: slimes %d (want >= 3), all have Size set' % slimes)
    rep(glowTop >= 20, 'lighting under the island: glowing tiles/posts on the park ground (glowstone on the ground, y 0..143): %d (want >= 20)' % glowTop)
    need2 = ['minecraft:birch_log', 'minecraft:birch_leaves', 'minecraft:poplar_log']
    miss2 = [n for n in need2 if n not in names]
    popColors = [n for n in ('minecraft:red_poplar_leaves', 'minecraft:orange_poplar_leaves', 'minecraft:yellow_poplar_leaves') if n in names]
    if len(popColors) < 1: miss2.append('a colored poplar (there are %s)' % popColors)
    rep(not miss2, 'zones around schools (birches) and kindergartens (multicolored poplars): trees in the palette (missing: %s)' % (miss2 or '-'))
    needB = ['minecraft:forest', 'minecraft:birch_forest', 'minecraft:dappled_forest', 'minecraft:flower_forest']
    missB = [b for b in needB if b not in biomes]
    rep(not missB, 'forest / birch / dappled forest / park (flower forest) biomes in the sections (missing: %s)' % (missB or '-'))
    rep(sum(animals.values()) >= 15 and len(animals) >= 4, 'animals in forest biomes: %d, kinds %d (want >= 15 / 4): %s' % (sum(animals.values()), len(animals), sorted(animals.items())))
    needF = ['minecraft:fern', 'minecraft:sweet_berry_bush', 'minecraft:leaf_litter']
    missF = [n for n in needF if n not in names]
    rep(not missF, 'flora of forest biomes (fern, berry bushes, litter) in the palette (missing: %s)' % (missF or '-'))
    return ok


if __name__ == '__main__':
    sys.exit(0 if check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')) else 1)
