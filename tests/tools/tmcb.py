# -*- coding: utf-8 -*-
"""The TMCB building-points file (tmc_buildings.dat) — writing and reading, for the tests.

Format (little-endian), "TMCB" version 1:
    header 32 bytes: char[4] 'TMCB', u32 version, u32 cell size (microdegrees, 10000 = 0.01 degree), u32 number of cells,
                     u64 number of records, 8 bytes reserved
    index: number of cells x (i64 cell key, u64 number of the first record, u32 number of records) = 20 bytes; key = (ix << 32) | u32(iy),
           ix = floor(lon_u / size), iy = floor(lat_u / size); sorted by key
    records: 16 bytes, grouped by cells: i32 lat_u, i32 lon_u, u32 CODE, u16 REL (dm), u16 FLOOR
"""
import struct

MAGIC = b'TMCB'
VERSION = 1
CELL_MICRO = 10000                     # 0.01 degree
HEADER = struct.Struct('<4sIIIQ8x')    # 32 bytes
INDEX = struct.Struct('<qQI')          # 20 bytes
REC = struct.Struct('<iiIHH')          # 16 bytes


def cell_key(lat_u, lon_u):
    ix, iy = lon_u // CELL_MICRO, lat_u // CELL_MICRO
    return (ix << 32) | (iy & 0xFFFFFFFF)


def write_dat(path, records):
    """records: a list of (lat_u, lon_u, code, rel_dm, floor). Writes the file, grouping the records by cells."""
    recs = sorted(records, key=lambda r: (cell_key(r[0], r[1]), r[0], r[1]))
    index = []
    i = 0
    n = len(recs)
    while i < n:
        k = cell_key(recs[i][0], recs[i][1])
        j = i
        while j < n and cell_key(recs[j][0], recs[j][1]) == k:
            j += 1
        index.append((k, i, j - i))
        i = j
    with open(path, 'wb') as f:
        f.write(HEADER.pack(MAGIC, VERSION, CELL_MICRO, len(index), n))
        for k, off, cnt in index:
            f.write(INDEX.pack(k, off, cnt))
        for lat_u, lon_u, code, rel_dm, floor in recs:
            f.write(REC.pack(lat_u, lon_u, code, min(65535, max(0, rel_dm)), min(65535, max(0, floor))))
    return n, len(index)


def read_info(path):
    with open(path, 'rb') as f:
        magic, ver, cell, ncells, nrec = HEADER.unpack(f.read(HEADER.size))
        if magic != MAGIC:
            raise SystemExit('not a TMCB file')
        index = [INDEX.unpack(f.read(INDEX.size)) for _ in range(ncells)]
        first = REC.unpack(f.read(REC.size)) if nrec else None
    return ver, cell, ncells, nrec, first
