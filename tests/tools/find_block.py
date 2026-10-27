# find block coords of a given block name in exported zip (native layout)
import sys, io, struct, zipfile, zlib, gzip
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from dump121 import read_str

TARGET = sys.argv[2] if len(sys.argv) > 2 else 'minecraft:wheat'
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
        raise ValueError(tid)
    def comp():
        d={}
        while True:
            t=f.read(1)[0]
            if t==0: return d
            nm=read_str(f); d[nm]=val(t)
    assert f.read(1)[0]==10; read_str(f)
    return comp()

def chunks(zf):
    for n in [x for x in zf.namelist() if x.endswith('.mca')]:
        m = zf.read(n)
        rx, rz = map(int, n.split('/')[-1][2:-4].split('.'))
        for slot in range(1024):
            loc = struct.unpack('>I', m[slot*4:slot*4+4])[0]
            if not loc: continue
            so = loc >> 8
            size, fmt = struct.unpack('>IB', m[so*4096:so*4096+5])
            data = m[so*4096+5:so*4096+size+4]
            data = gzip.decompress(data) if data[:2] == b'\x1f\x8b' else zlib.decompress(data)
            yield (slot % 32) + (rx << 5), (slot // 32) + (rz << 5), parse(data)

zf = zipfile.ZipFile(sys.argv[1])
xs=[]; zs=[]
for cx, cz, ch in chunks(zf):
    for s in ch['sections']:
        bs = s.get('block_states', {}); pal = bs.get('palette') or []; d = bs.get('data')
        if d is None: continue
        names=[]
        for e in pal:
            if isinstance(e,dict):
                names.append(e.get('Name') or e.get('id') or e.get(''))
            else:
                names.append(e)
        if TARGET not in names: continue
        bits = max(4,(len(names)-1).bit_length()); per=64//bits; mask=(1<<bits)-1
        for i in range(4096):
            li=i//per; sh=(i%per)*bits
            v=(d[li]>>sh)&mask
            if v<len(names) and names[v]==TARGET:
                xs.append(cx*16+(i&15)); zs.append(cz*16+((i>>4)&15))
print(TARGET, 'x', min(xs), max(xs), 'z', min(zs), max(zs), 'count', len(xs))
