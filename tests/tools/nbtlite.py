import os, struct, zlib
# ---------------- NBT ----------------
def u8(b, p): return b[p], p + 1
def rstr(b, p):
    n, p = struct.unpack_from('>H', b, p)[0], p + 2
    return b[p:p + n].decode('utf-8', 'replace'), p + n

def read_tag(b, t, p):
    if t == 0: return None, p
    if t == 1: return struct.unpack_from('>b', b, p)[0], p + 1
    if t == 2: return struct.unpack_from('>h', b, p)[0], p + 2
    if t == 3: return struct.unpack_from('>i', b, p)[0], p + 4
    if t == 4: return struct.unpack_from('>q', b, p)[0], p + 8
    if t == 5: return struct.unpack_from('>f', b, p)[0], p + 4
    if t == 6: return struct.unpack_from('>d', b, p)[0], p + 8
    if t == 7:
        n, p = struct.unpack_from('>i', b, p)[0], p + 4; return b[p:p + n], p + n
    if t == 8: return rstr(b, p)
    if t == 9:
        et, p = u8(b, p); n, p = struct.unpack_from('>i', b, p)[0], p + 4
        lst = []
        for _ in range(n):
            v, p = read_tag(b, et, p); lst.append(v)
        return lst, p
    if t == 10:
        d = {}
        while True:
            tt, p = u8(b, p)
            if tt == 0: break
            k, p = rstr(b, p)
            d[k], p = read_tag(b, tt, p)
        return d, p
    if t == 11:
        n, p = struct.unpack_from('>i', b, p)[0], p + 4
        return list(struct.unpack_from('>%di' % n, b, p)) if n else [], p + 4 * n
    if t == 12:
        n, p = struct.unpack_from('>i', b, p)[0], p + 4
        # long arrays (section data) are left as raw bytes — numpy decodes them
        return ('LONGS', b[p:p + 8 * n]), p + 8 * n
    raise ValueError('tag %d' % t)

def read_nbt(b):
    t, p = u8(b, 0); _, p = rstr(b, p)
    return read_tag(b, t, p)[0]

def region_chunks(path):
    rx = int(os.path.basename(path).split('.')[1])
    rz = int(os.path.basename(path).split('.')[2])
    with open(path, 'rb') as f:
        head = f.read(4096)
        for i in range(1024):
            v = struct.unpack_from('>I', head, i * 4)[0]
            off = (v >> 8) * 4096
            if v == 0 or off == 0: continue
            f.seek(off)
            ln, comp = struct.unpack('>IB', f.read(5))
            if ln <= 1: continue
            payload = f.read(ln - 1)
            try:
                nbt = zlib.decompress(payload) if comp in (2, 8) else payload
            except zlib.error:
                continue
            yield rx * 32 + (i % 32), rz * 32 + (i // 32), read_nbt(nbt)

