# -*- coding: utf-8 -*-
# Compare two exported world archives element-by-element (refactoring safety net).
# Reference vs candidate: identical names and identical bytes of every entry, except:
#   - export_info.json / ATTRIBUTION.txt (version and timestamp dependent);
#   - level.dat / level.dat_old (store the export wall-clock time);
#   - the mtime table of every .mca header: bytes 4096..8192 hold per-chunk modification
#     seconds (the chunk payloads after byte 8192 are compared fully). Verified: with these
#     masked, two runs of the same input are byte-identical without QT_HASH_SEED.
# Usage: py -3 cmp_worlds.py <before.zip> <after.zip> [--all]
#   --all  do not exclude anything (strict mode, for the determinism self-check)
import sys, zipfile

MTIME = (4096, 8192)   # the .mca header table that holds real-time seconds


def normalize(name, data):
    if name.endswith('.mca'):
        return data[:MTIME[0]] + b'\0' * (MTIME[1] - MTIME[0]) + data[MTIME[1]:]
    return data

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    strict = '--all' in sys.argv
    if len(args) != 2:
        print(__doc__); return 2
    a, b = (zipfile.ZipFile(p) for p in args)
    na, nb = set(a.namelist()), set(b.namelist())
    bad = []
    for miss in sorted(na - nb): bad.append('MISSING in B: ' + miss)
    for extra in sorted(nb - na): bad.append('EXTRA in B: ' + extra)
    skip = () if strict else ('export_info.json', 'ATTRIBUTION.txt', 'level.dat', 'level.dat_old')
    same = 0
    for n in sorted(na & nb):
        if any(n.endswith(s) or s in n for s in skip):
            continue
        ba, bb = normalize(n, a.read(n)), normalize(n, b.read(n))
        if ba != bb:
            d = next((i for i in range(min(len(ba), len(bb))) if ba[i] != bb[i]), min(len(ba), len(bb)))
            bad.append('DIFF %s (len %d vs %d, first byte %d)' % (n, len(ba), len(bb), d))
        else:
            same += 1
    print('entries compared: %d, mismatches: %d' % (same, len(bad)))
    for x in bad[:40]:
        print(' ', x)
    return 0 if not bad else 1

if __name__ == '__main__':
    sys.exit(main())
