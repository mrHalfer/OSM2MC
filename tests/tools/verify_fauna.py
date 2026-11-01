# One-off style check of the aquatic fauna ENTITIES (step 3.585) in an exported zip.
# Usage: python verify_fauna.py <world.zip> [tundra|temperate|tropical]
# Reads entities/r.*.mca (the server spawns mobs ONLY from there) and checks that every aquatic
# mob persists (PersistenceRequired=1), carries its species field, and that the fish species
# matches the climate zone when the zone is given. Exit code 0 PASS, 1 FAIL, 77 no aquatic mobs.
import os, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_mc_export as V

AQUA = {'minecraft:cod', 'minecraft:salmon', 'minecraft:tropical_fish', 'minecraft:pufferfish',
        'minecraft:axolotl', 'minecraft:squid'}
WARM = {'minecraft:tropical_fish', 'minecraft:pufferfish'}    # in warm waters a body gets one of the two
SPECIES_FIELD = {'minecraft:tropical_fish': 'BucketVariantTag', 'minecraft:axolotl': 'Variant',
                 'minecraft:pufferfish': 'PufferFishSize'}


def main(path, zone):
    want = {'tundra': 'minecraft:salmon', 'tropical': 'minecraft:tropical_fish',
            'temperate': 'minecraft:cod'}.get(zone)
    zf = zipfile.ZipFile(path)
    per = {}                      # species -> [count, ymin, ymax]
    bad_fields = []
    for root in V.ent_iter(zf):
        for e in root.get('Entities') or []:
            eid = e.get('id', '')
            if eid not in AQUA:
                continue
            pos = e.get('Pos')
            y = float(pos[1]) if pos else 0.0
            if e.get('PersistenceRequired') != 1:
                bad_fields.append(f'{eid} @{pos} no PersistenceRequired')
            fld = SPECIES_FIELD.get(eid)
            if fld and fld not in e:
                bad_fields.append(f'{eid} @{pos} no {fld}')
            if eid == 'minecraft:axolotl' and 'Variant' in e and not (0 <= int(e['Variant']) <= 4):
                bad_fields.append(f'{eid} @{pos} Variant out of 0..4')
            rec = per.setdefault(eid, [0, y, y])
            rec[0] += 1
            rec[1] = min(rec[1], y)
            rec[2] = max(rec[2], y)
    if not per:
        print('no aquatic entities found (a world without water bodies - SKIP)')
        return 77
    for sp, (n, ya, yb) in sorted(per.items()):
        print(f'  {sp:32} x{n} (y {ya:.1f}..{yb:.1f})')
    fails = list(bad_fields[:8])
    if zone:
        for sp, (n, _, _) in per.items():
            if sp in ('minecraft:cod', 'minecraft:salmon', 'minecraft:tropical_fish', 'minecraft:pufferfish'):
                ok = sp in WARM if zone == 'tropical' else sp == want
                if not ok:
                    fails.append(f'{sp} outside the {zone} zone')
    print('FAUNA', 'FAIL: ' + '; '.join(fails) if fails else 'PASS')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ''))
