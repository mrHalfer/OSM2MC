# -*- coding: utf-8 -*-
"""compare_worlds.py <reference.zip> <new.zip> <second_run_of_reference.zip|-> [...triples]

Compares two worlds by CONTENT. Needed for refactoring: after code changes the world with the same input data and seed must stay the same.
- ordinary files — the SHA-256 of the content (the time and the zip compression are ignored);
- region/entities (*.mca) — parsed by chunks; the parsed NBT is compared. ONLY what is different every time is ignored:
  the timestamp table in the .mca header, the UUID/UUIDMost/UUIDLeast fields of entities and time marks (LastUpdate, LastModified).
Exit code 0 — the worlds are the same.
"""
import gzip, hashlib, os, struct, sys, zipfile, zlib
import tcfg

sys.path.insert(0, tcfg.TOOLS)
from verify_mc_export import parse

IGNORE_KEYS = {'UUID', 'UUIDMost', 'UUIDLeast', 'LastUpdate', 'LastModified', 'Timestamp', 'LastPlayed'}


def strip(v):
    if isinstance(v, dict):
        return {k: strip(x) for k, x in v.items() if k not in IGNORE_KEYS}
    if isinstance(v, list):
        return [strip(x) for x in v]
    return v


def mca_chunks(raw):
    out = {}
    for slot in range(1024):
        loc = struct.unpack('>I', raw[slot * 4:slot * 4 + 4])[0]
        if not loc:
            continue
        so = loc >> 8
        size, fmt = struct.unpack('>IB', raw[so * 4096:so * 4096 + 5])
        data = raw[so * 4096 + 5:so * 4096 + size + 4]
        data = gzip.decompress(data) if data[:2] == b'\x1f\x8b' else zlib.decompress(data)
        out[slot] = strip(parse(data))
    return out


def load(path):
    items = {}
    with zipfile.ZipFile(path) as z:
        for n in z.namelist():
            if n.endswith('/'):
                continue
            raw = z.read(n)
            if n.endswith('.mca'):
                items[n] = mca_chunks(raw)
            elif n.startswith('level.dat') or n.endswith('/level.dat'):
                items[n] = strip(parse(gzip.decompress(raw)))
            else:
                items[n] = hashlib.sha256(raw).hexdigest()
    return items


def compare(a, b, run2=None):
    """run2 — a second run of THE SAME code that gave a: chunks that differ between a and run2 are unstable by themselves and are not compared."""
    da, db = load(a), load(b)
    unstable = set()
    if run2:
        dr = load(run2)
        for n in da:
            if isinstance(da[n], dict) and isinstance(dr.get(n), dict):
                unstable |= {(n, s) for s in set(da[n]) | set(dr[n]) if da[n].get(s) != dr[n].get(s)}
            elif da[n] != dr.get(n):
                unstable.add((n, None))
    only_a = sorted(set(da) - set(db)); only_b = sorted(set(db) - set(da))
    diff = []
    for n in sorted(set(da) & set(db)):
        if (n, None) in unstable:
            continue
        if isinstance(da[n], dict) and isinstance(db[n], dict):
            bad = [s for s in set(da[n]) | set(db[n]) if da[n].get(s) != db[n].get(s) and (n, s) not in unstable]
            if bad:
                diff.append('%s (chunks differ: %d of %d)' % (n, len(bad), len(da[n])))
        elif da[n] != db[n]:
            diff.append(n)
    ok = not only_a and not only_b and not diff
    print('%s  %s vs %s: files %d / %d, differences %d, only in the reference %d, only in the new one %d, unstable places (excluded) %d'
          % ('SAME' if ok else 'DIFF', os.path.basename(a), os.path.basename(b), len(da), len(db), len(diff), len(only_a), len(only_b), len(unstable)))
    for n in diff[:8]:
        print('   differs:', n)
    for n in only_a[:4]:
        print('   only in the reference:', n)
    for n in only_b[:4]:
        print('   only in the new one:', n)
    return ok


if __name__ == '__main__':
    args = sys.argv[1:]
    if len(args) < 3 or len(args) % 3:
        sys.exit(__doc__ + "\nGroups of three arguments: reference new second_run_of_reference ('-' — do not use)")
    res = [compare(args[i], args[i + 1], None if args[i + 2] == '-' else args[i + 2]) for i in range(0, len(args), 3)]
    sys.exit(0 if all(res) else 1)
