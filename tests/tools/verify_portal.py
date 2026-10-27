# -*- coding: utf-8 -*-
# Portal ruins in a forest: netherrack/magma/gold around the frame, a chest with ruined_portal loot, the structure start minecraft:ruined_portal (for /locate) and the structure_set in the datapack.
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V


def check(path):
    zf = zipfile.ZipFile(path)
    names = set()
    chest = starts = refs = 0
    for cx, cz, ch in V.chunk_iter(zf):
        for s in ch.get('sections') or []:
            for e in s['block_states']['palette']:
                names.add(e if isinstance(e, str) else (e.get('Name') or e.get('id') or e.get('')))
        for b in ch.get('block_entities') or []:
            if str(b.get('LootTable', '')).endswith('chests/ruined_portal'): chest += 1
        st = ch.get('structures') or {}
        if 'minecraft:ruined_portal' in (st.get('starts') or {}): starts += 1
        if 'minecraft:ruined_portal' in (st.get('References') or {}): refs += 1
    sets = [n for n in zf.namelist() if 'structure_set/ruined_portal_here_' in n]
    miss = [n for n in ('minecraft:netherrack', 'minecraft:gold_block', 'minecraft:magma_block') if n not in names]
    good = not miss and chest >= 1 and starts >= 1 and refs >= starts and len(sets) >= starts
    print('  [%s] portal ruins: missing blocks %s, ruined_portal chests %d, structure starts %d, chunks with references %d, structure_set %d' %
          ('OK ' if good else 'MISS', ','.join(miss) or '-', chest, starts, refs, len(sets)))
    return good


if __name__ == '__main__':
    sys.exit(0 if check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')) else 1)
