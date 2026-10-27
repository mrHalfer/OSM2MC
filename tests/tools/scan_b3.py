# Diagnostics of batch3: (a) stairs with a "ceiling", (b) roads (a pedestrian road vs a highway),
# (c) wall_torch around the high-rise (aviation beacons), (d) the shopping center panorama — runs and corners.
# Run: py -3 scan_b3.py [zip]   (by default mc_test.zip next to it)
import io, os, struct, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from dump121 import read_str

F = {1:'>b',2:'>h',3:'>i',4:'>q',5:'>f',6:'>d'}

def parse(raw):
    f = io.BytesIO(raw)
    def val(tid):
        if tid in F: return struct.unpack(F[tid], f.read(struct.calcsize(F[tid])))[0]
        if tid == 7: n=struct.unpack('>i',f.read(4))[0]; return f.read(n)
        if tid == 8: return read_str(f)
        if tid == 9:
            et=f.read(1)[0]; n=struct.unpack('>i',f.read(4))[0]
            return [val(et) for _ in range(n)]
        if tid == 10: return comp()
        if tid == 11:
            n=struct.unpack('>i',f.read(4))[0]; return list(struct.unpack('>'+'i'*n, f.read(4*n)))
        if tid == 12:
            n=struct.unpack('>i',f.read(4))[0]; return list(struct.unpack('>'+'q'*n, f.read(8*n)))
    def comp():
        d={}
        while True:
            t=f.read(1)[0]
            if t==0: return d
            nm=read_str(f); d[nm]=val(t)
    assert f.read(1)[0]==10; read_str(f)
    return comp()

def chunk_iter(zf):
    for n in [x for x in zf.namelist() if x.endswith('.mca')]:
        m = zf.read(n)
        rx, rz = map(int, n.split('/')[-1][2:-4].split('.'))
        for slot in range(1024):
            loc = struct.unpack('>I', m[slot*4:slot*4+4])[0]
            if not loc: continue
            so = loc >> 8
            size, fmt = struct.unpack('>IB', m[so*4096:so*4096+5])
            data = m[so*4096+5:so*4096+size+4]
            if data[:2] == b'\x1f\x8b':
                import gzip as _gz
                data = _gz.decompress(data)
            else:
                import zlib as _zl
                data = _zl.decompress(data)
            cx = (slot % 32) + (rx << 5)
            cz = (slot // 32) + (rz << 5)
            yield cx, cz, parse(data)

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(__import__('tcfg').WORLDS, 'mc_test.zip')
    zf = zipfile.ZipFile(path)
    chunks = {}
    for cx, cz, ch in chunk_iter(zf):
        secs = {}
        for s in ch['sections']:
            bs = s.get('block_states', {})
            pal = bs.get('palette') or []
            data = bs.get('data')
            entries = []
            for e in pal:
                if isinstance(e, dict):
                    if 'Name' in e: entries.append((e['Name'], e.get('Properties')))
                    elif 'id' in e: entries.append((e['id'], e.get('properties')))
                    else: entries.append((e[''], None))
                else:
                    entries.append((e, None))
            secs[s['Y']] = (entries, data)
        chunks[(cx, cz)] = secs

    def cell(x, y, z):
        ent = chunks.get((x >> 4, z >> 4), {}).get(y >> 4)
        if ent is None: return 'minecraft:air'
        entries, data = ent
        names = [e[0] for e in entries]
        if data is None: return names[0]
        bits = max(4, (len(names)-1).bit_length()); per = 64//bits; mask = (1 << bits)-1
        i = ((y & 15) << 8) | ((z & 15) << 4) | (x & 15)
        v = (data[i // per] >> ((i % per) * bits)) & mask
        return names[v] if v < len(names) else 'minecraft:air'

    stairs, torches, glass = [], [], []
    for (cx, cz), secs in chunks.items():
        for sy, (entries, data) in secs.items():
            if sy < -1 or sy > 16: continue
            names = [e[0] for e in entries]
            st = {pi for pi, n in enumerate(names) if n.endswith('stone_stairs')}
            tc = {pi for pi, n in enumerate(names) if n == 'minecraft:wall_torch'}
            gl = {pi for pi, n in enumerate(names) if 'glass' in n}
            if not (st or tc or gl) or data is None: continue
            bits = max(4, (len(names) - 1).bit_length()); per = 64 // bits; mask = (1 << bits) - 1
            for i in range(4096):
                v = (data[i // per] >> ((i % per) * bits)) & mask
                if v not in st and v not in tc and v not in gl: continue
                wx = cx * 16 + (i & 15)
                wy = sy * 16 + ((i >> 8) & 15)
                wz = cz * 16 + ((i >> 4) & 15)
                if v in st: stairs.append((wx, wy, wz))
                elif v in tc: torches.append((wx, wy, wz))
                else: glass.append((wx, wy, wz))
    out = io.open(os.path.join(HERE, 'scan_b3_out.txt'), 'w', encoding='utf-8')
    out.write(f'zip={os.path.basename(path)} stairs={len(stairs)} torches={len(torches)} glass={len(glass)}\n')

    # (a) stairs with a non-air block above the head
    bad = 0
    for x, y, z in stairs:
        up = cell(x, y + 1, z)
        if up != 'minecraft:air':
            bad += 1
            if bad <= 30:
                out.write(f'HEAD_BAD stair=({x},{y},{z}) above={up}\n')
    out.write(f'head_bad total={bad}\n')

    # (b) roads: the strip x 55..105, z -24..-8 — which wool is where at y=0/1
    row = {}
    for x in range(55, 106):
        for z in range(-24, -7):
            for y in (0, 1):
                n = cell(x, y, z)
                if 'wool' in n or 'carpet' in n:
                    row.setdefault((z, y, n.split(':')[-1].split('[')[0]), []).append(x)
    for k in sorted(row):
        xs = row[k]
        out.write(f'ROAD z={k[0]} y={k[1]} {k[2]} n={len(xs)} x={min(xs)}..{max(xs)}\n')

    # (c) wall_torch around the high-rise (the outline ~169..182 x -104..-91)
    ring = [(x, y, z) for x, y, z in torches if 160 <= x <= 192 and -112 <= z <= -83]
    out.write(f'TOWER torches in region={len(ring)}\n')
    for t in sorted(ring, key=lambda t: (t[2], t[0], t[1]))[:120]:
        out.write(f'TORCH ({t[0]},{t[1]},{t[2]})\n')

    # (d) the shopping center panorama: glass runs along every row of the x-scan and z-scan, corner cells
    gset = {(x, y, z) for x, y, z in glass if 277 <= x <= 312 and -213 <= z <= -187}
    def runs(line):
        r, best = 0, 0
        for on in line: best, r = max(best, r + 1 if on else 0), r + 1 if on else 0
        return best
    for y in range(1, 13):
        for z in range(-213, -187):
            r = runs([ (x, y, z) in gset for x in range(277, 313) ])
            if r >= 5: out.write(f'PANO y={y} z={z} runx={r}\n')
        for x in range(277, 313):
            r = runs([ (x, y, z) in gset for z in range(-213, -187) ])
            if r >= 5: out.write(f'PANO y={y} x={x} runz={r}\n')
    for cx, cz in ((281, -210), (310, -210), (281, -190), (310, -190)):
        col = ''.join('G' if (cx, y, cz) in gset else '.' for y in range(1, 13))
        out.write(f'CORNER ({cx},{cz}) y1..12 glass={col} '
                  f'below0={[cell(cx, y, cz).split(":")[-1] for y in (-1, 0)]}\n')
    out.close()
    print('scan_b3 done')

main()
