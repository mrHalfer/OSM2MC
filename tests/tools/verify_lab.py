# -*- coding: utf-8 -*-
# Science building 44315000: an underground laboratory with redstone mechanisms (lamps, levers, repeaters, note blocks, pistons, an iron door, wires).
import os, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_mc_export as V

NEED = ['minecraft:redstone_lamp', 'minecraft:lever', 'minecraft:redstone_wire', 'minecraft:repeater', 'minecraft:note_block',
        'minecraft:piston', 'minecraft:iron_door', 'minecraft:redstone_block']


def check(path):
    zf = zipfile.ZipFile(path)
    names = set()
    litLamps = 0
    for cx, cz, ch in V.chunk_iter(zf):
        for s in ch.get('sections') or []:
            for e in s['block_states']['palette']:
                if isinstance(e, dict):
                    n = e.get('Name') or e.get('id') or e.get('')
                else:
                    n = e
                names.add(n)
    miss = [n for n in NEED if n not in names]
    good = not miss
    print('  [%s] science building: a laboratory with redstone mechanisms in the palette (missing: %s)' % ('OK ' if good else 'MISS', ','.join(miss) or '-'))
    return good


if __name__ == '__main__':
    sys.exit(0 if check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test_noise.zip')) else 1)
