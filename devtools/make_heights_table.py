# -*- coding: utf-8 -*-
"""make_heights_table.py — FOR THE DEVELOPER ONLY (not included in the release).

Builds a compact table of typical heights heights.csv from the full classifier clutters.dbf: only the CODES that really
occur in the layers in use (bldpol, csrpol, rdcpol, rdslin, rrslin, blkpol, hdrpol, hdrlin, vegpol, vgbpol) get into it:
  1) in the shape files of your data (all directories found, passed through --data);
  2) in the OSM rules (osm_rules.json: the CODE fields) and in the enhancer data (11 building codes);
  3) in the codes known to the exporter (roads, sites, vegetation, shopping centers/churches etc.) — the list below.
Only heights.csv (CODE;HIGH;OBJNAME50) goes into the release, the full clutters.dbf — does not.

    py -3 make_heights_table.py --dbf clutters.dbf --data <data_directory> ... -o ..\\resources\\heights.csv
"""
import argparse, collections, csv, json, os, re, struct

LAYERS = ('bldpol', 'csrpol', 'rdcpol', 'rdslin', 'rrslin', 'blkpol', 'hdrpol', 'hdrlin', 'vegpol', 'vgbpol')

# codes that have explicit logic in the exporter / are needed as a reserve (see mcworldexporter.cpp, mcscene.cpp)
EXPORTER_CODES = [
    44000000, 44310000, 44311000, 44312000, 44313000, 44314000, 44315000, 44316000, 44317000, 44318000, 44319000, 44340000,
    44400000, 44400001, 44400002, 44400100, 44400110, 44350000,                                   # buildings (including private variants from the database)
    45210000, 45220000, 45230000, 45240000, 45260000, 45261000, 45262000, 45270000, 45300000,       # streets, alleys, bike paths, a zebra, squares
    61210000, 61220000, 61230000, 61231000, 61232000, 61310000, 61320000, 61330000, 61420000,       # motor roads, dirt roads, trails
    61111000, 61124000, 61112000, 61113000,                                                          # railways
    62230000, 62231000,                                                                              # parking lots, asphalt
    53400000, 53510000, 53530000, 53600000, 53700000, 53710000, 52100000, 52400000, 51111000, 51123000,
    51130000, 51220000, 51230000, 51133000, 51410000, 51460000, 51480000, 53110000, 71323000,       # csrpol: territories and structures
    71111110, 71111120, 71113000, 71211100, 71211200, 71122000, 71221000, 71321000, 71314000,       # vegetation (allowed for the Pale Garden)
    71126000, 71329000, 71313000, 71328000, 71324000, 71123000, 71222000, 31132000, 31131000,
]


# internal tMC codes absent from the classifier
INTERNAL = {45262000: (0.0, 'Pedestrian crossing (zebra), an internal tMC code')}


def read_dbf(path, want_fields):
    f = open(path, 'rb').read()
    n = struct.unpack('<I', f[4:8])[0]; hl = struct.unpack('<H', f[8:10])[0]; rl = struct.unpack('<H', f[10:12])[0]
    fields = []; p = 32
    while f[p] != 0x0D:
        fields.append((f[p:p + 11].split(b'\0')[0].decode('latin1'), f[p + 16])); p += 32
    off, offs = 1, {}
    for nm, l in fields:
        offs[nm.upper()] = (off, l); off += l
    cols = [w for w in want_fields if w.upper() in offs]
    for i in range(n):
        r = f[hl + i * rl:hl + (i + 1) * rl]
        yield {w.upper(): r[offs[w.upper()][0]:offs[w.upper()][0] + offs[w.upper()][1]].decode('cp1251', 'replace').strip() for w in cols}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dbf', required=True, help='the full clutters.dbf')
    ap.add_argument('--data', nargs='*', default=[], help='directories with shape files (searched recursively)')
    ap.add_argument('--rules', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'resources', 'osm_rules.json'))
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()

    used = collections.Counter()
    # 1) data
    pat = re.compile(r'\.(%s)\.dbf$' % '|'.join(LAYERS), re.I)
    nfiles = 0
    for root in a.data:
        for d, _, files in os.walk(root):
            for fn in files:
                if pat.search(fn):
                    nfiles += 1
                    try:
                        for row in read_dbf(os.path.join(d, fn), ['CODE']):
                            c = row.get('CODE', '')
                            if c.isdigit() and int(c) > 0:
                                used[int(c)] += 1
                    except Exception as e:
                        print('skipped', fn, e)
    print('shape files viewed: %d, different codes in the data: %d' % (nfiles, len(used)))
    from_data = set(used)
    # 2) OSM rules
    from_rules = set()
    if os.path.exists(a.rules):
        for r in json.load(open(a.rules, encoding='utf-8'))['rules']:
            c = (r.get('fields') or {}).get('CODE', '')
            if c.isdigit():
                from_rules.add(int(c))
    # 3) the exporter
    allc = from_data | from_rules | set(EXPORTER_CODES)
    # the full classifier
    table = {}
    for row in read_dbf(a.dbf, ['CODE', 'OBJNAME50', 'HIGH']):
        if row['CODE'].isdigit():
            table[int(row['CODE'])] = (float(row['HIGH'] or 0), row['OBJNAME50'])
    table.update(INTERNAL)
    rows = [(c, table[c][0], table[c][1]) for c in sorted(allc) if c in table]
    missing = sorted(allc - set(table))
    with open(a.out, 'w', encoding='utf-8-sig', newline='') as f:
        w = csv.writer(f, delimiter=';')
        w.writerow(['CODE', 'HIGH', 'OBJNAME50'])
        for c, h, nm in rows:
            w.writerow([c, ('%g' % h), nm])
    print('codes in the table: %d of %d in the full classifier; from the data %d, from the OSM rules %d, from the exporter list %d'
          % (len(rows), len(table), len(from_data), len(from_rules), len(EXPORTER_CODES)))
    if missing:
        print('not in the classifier (without height):', missing)


if __name__ == '__main__':
    main()
