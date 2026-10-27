# -*- coding: utf-8 -*-
# A check of the secret passages of churches (a noise archive): the stronghold with a portal to the End and the Ancient City (Deep Dark).
# Fast: looks only at the section palettes, biomes and block_entities (without a full decoding of blocks).
#  - stronghold: end_portal_frame portal frames (12 pcs, each has facing/eye), a silverfish spawner
#  - Ancient City: sculk_shrieker with can_summon=true, sculk_catalyst, reinforced_deepslate, the deep_dark biome, ancient_city chests
#  - shafts: ladders from the church down >= 20 blocks (checked indirectly: a hatch in the palette + the ladder in the column is not checked here)
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V


def pal_names(pal):
    out = []
    for e in pal:
        if isinstance(e, str):
            out.append((e, {}))
        else:
            out.append((e.get('id') or e.get('Name') or e.get('') or '', e.get('properties') or e.get('Properties') or {}))
    return out


def scan(path):
    zf = zipfile.ZipFile(path)
    names = set()
    shriek_true = frames = 0
    frame_chunks = set()
    deep_chunks = set()
    spawners = []
    loots = set()
    biomes = set()
    minY_frames = None
    for cx, cz, ch in V.chunk_iter(zf):
        for s in ch.get('sections') or []:
            bs = s.get('block_states') or {}
            for nm, pr in pal_names(bs.get('palette') or []):
                names.add(nm)
                if nm == 'minecraft:end_portal_frame':
                    frame_chunks.add((cx, cz, s['Y']))
                if nm == 'minecraft:sculk_shrieker' and pr.get('can_summon') == 'true':
                    shriek_true += 1
            bp = ((s.get('biomes') or {}).get('palette')) or []
            biomes.update(bp)
            if 'minecraft:deep_dark' in bp:
                deep_chunks.add((cx, cz))
        for be in ch.get('block_entities') or []:
            if be.get('id') == 'minecraft:mob_spawner':
                ent = ((be.get('SpawnData') or {}).get('entity') or {}).get('id')
                spawners.append((be.get('y'), ent))
            lt = be.get('LootTable')
            if lt:
                loots.add(lt)
    return names, shriek_true, frame_chunks, deep_chunks, spawners, loots, biomes


def mansion_count(path):
    """the number of separate (disconnected, with a gap of up to 8 blocks) clusters of columns with dark oak flooring — forest mansions"""
    zf = zipfile.ZipFile(path)
    import collections
    cnt = collections.Counter()
    for cx, cz, ch in V.chunk_iter(zf):
        for s in ch.get('sections') or []:
            bs = s['block_states']
            nm = [e if isinstance(e, str) else (e.get('id') or e.get('Name') or e.get('')) for e in bs['palette']]
            if 'minecraft:dark_oak_planks' not in nm or s['Y'] < 4: continue
            d = bs.get('data')
            if d is None: continue
            bits = max(4, (len(nm) - 1).bit_length()); per = 64 // bits; gi = nm.index('minecraft:dark_oak_planks')
            for i in range(4096):
                if (d[i // per] >> ((i % per) * bits)) & ((1 << bits) - 1) == gi:
                    cnt[((cx * 16 + (i & 15)) // 8, (cz * 16 + ((i >> 4) & 15)) // 8)] += 1
    cols = set(cnt)
    seen = set(); n = 0
    for c in cols:
        if c in seen: continue
        st = [c]; seen.add(c); tot = 0
        while st:
            q = st.pop(); tot += cnt[q]
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    k = (q[0] + dx, q[1] + dz)
                    if k in cols and k not in seen: seen.add(k); st.append(k)
        if tot >= 1000: n += 1   # a mansion — thousands of planks; bandit outposts (hundreds) are not counted
    return n


def check(path):
    names, shriek, frames, deep, spawners, loots, biomes = scan(path)
    ok = True
    def rep(good, msg):
        nonlocal ok
        print('  [%s] %s' % ('OK ' if good else 'MISS', msg))
        ok &= good
    rep('minecraft:end_portal_frame' in names, 'stronghold: portal frames in the palette (sections %d)' % len(frames))
    rep(any(e == 'minecraft:silverfish' for _, e in spawners), 'stronghold: a silverfish spawner, spawners in total %d' % len(spawners))
    rep('minecraft:sculk_shrieker' in names and shriek > 0, 'city: sculk_shrieker can_summon=true (sections %d)' % shriek)
    rep('minecraft:sculk_catalyst' in names and 'minecraft:reinforced_deepslate' in names, 'city: catalyst + reinforced_deepslate')
    rep(len(deep) >= 4, 'city: chunks with the deep_dark biome %d (want >= 4)' % len(deep))
    rep(any('ancient_city' in l for l in loots), 'city: ancient_city chests in block_entities')
    rep('minecraft:pale_oak_log' in names and 'minecraft:pale_oak_leaves' in names and 'minecraft:pale_moss_block' in names,
        'Pale Garden: pale oak, leaves and moss in the palette')
    rep('minecraft:creaking_heart' in names, 'Pale Garden: a creaking heart in the trunks')
    rep('minecraft:leaf_litter' in names and 'minecraft:vine' in names, 'Pale Garden: the real terrain/trees of the donor (leaf litter, vines)')
    rep('minecraft:spruce_leaves' in names and 'minecraft:spruce_log' in names, 'spruces: forests 71111110 with an area of 5000 or more — spruce_log/spruce_leaves')
    rep('minecraft:taiga' in biomes, 'spruces: the taiga biome in the sections')
    rep('minecraft:pale_garden' in biomes, 'Pale Garden: the pale_garden biome in the sections')
    nM = mansion_count(path)
    rep(nM <= 1, 'Pale Garden: no more than one forest mansion (clusters found: %d)' % nM)
    zf = zipfile.ZipFile(path)
    sets = [n for n in zf.namelist() if n.startswith('datapacks/tmc_lairs/data/tmc/worldgen/structure_set/') and 'ruined_portal' not in n and '/tpl_' not in n]
    rep(len(sets) == 2 and 'datapacks/tmc_lairs/pack.mcmeta' in zf.namelist(), 'the /locate datapack: structure_set of the stronghold and the city (%d)' % len(sets))
    return ok


if __name__ == '__main__':
    p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test_noise.zip')
    sys.exit(0 if check(p) else 1)
