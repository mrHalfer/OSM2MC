"""Dump NBT tag structure of a 1.21.11 world: level.dat and region chunk.
Usage:
  python dump121.py level   <path-to-level.dat>
  python dump121.py chunk   <path-to-r.X.Z.mca> [cx cz]
"""
import gzip, struct, sys, io, mmap

TYPES = {0:'END',1:'Byte',2:'Short',3:'Int',4:'Long',5:'Float',6:'Double',7:'ByteArray',8:'String',9:'List',10:'Compound',11:'IntArray',12:'LongArray'}
FMT = {1:'>b',2:'>h',3:'>i',4:'>q',5:'>f',6:'>d'}

def read_str(f):
    n = struct.unpack('>H', f.read(2))[0]
    return f.read(n).decode('utf-8', 'replace')

class D:
    def __init__(self, quiet_list_max=6, valstrings=True):
        self.q = quiet_list_max
    def skip(self, f, tid):
        if tid in FMT: f.read(struct.calcsize(FMT[tid]))
        elif tid == 7:
            n = struct.unpack('>i', f.read(4))[0]; f.read(n)
        elif tid == 8: read_str(f)
        elif tid == 9:
            et = f.read(1)[0]; n = struct.unpack('>i', f.read(4))[0]
            for _ in range(n): self.skip(f, et)
        elif tid == 10:
            self.compound(f, 0, None, skip=True)
        elif tid in (11,12):
            n = struct.unpack('>i', f.read(4))[0]; f.read(n*(4 if tid==11 else 8))
        else: raise ValueError('bad tag %d' % tid)
    def val(self, f, tid, depth, name):
        """returns printable value or None (already printed nested)"""
        if tid in FMT:
            return struct.unpack(FMT[tid], f.read(struct.calcsize(FMT[tid])))[0]
        if tid == 8:
            return repr(read_str(f))
        if tid == 7:
            n = struct.unpack('>i', f.read(4))[0]; f.read(n); return f'<ByteArray {n} bytes>'
        if tid == 9:
            et = f.read(1)[0]; n = struct.unpack('>i', f.read(4))[0]
            print('  '*depth + f'{name}: List<{TYPES.get(et,"?")}>({n})')
            # peek first element structure for lists of compounds/strings
            for i in range(n):
                if et == 10:
                    self.compound(f, depth+1, f'[{i}]')
                elif et == 8:
                    s = read_str(f)
                    if i < self.q: print('  '*(depth+1) + f'[{i}] = {s!r}')
                    elif i == self.q: print('  '*(depth+1) + '...')
                else:
                    self.skip(f, et)
            return None
        if tid == 10:
            print('  '*depth + f'{name}: Compound {{')
            self.compound(f, depth+1, None)
            print('  '*depth + '}')
            return None
        if tid in (11,12):
            n = struct.unpack('>i', f.read(4))[0]
            sz = 4 if tid==11 else 8
            buf = f.read(n*sz)
            code = 'i' if tid==11 else 'q'
            m = min(n, 8)
            sample = struct.unpack('>'+code*m, buf[:m*sz]) if m else ()
            return f'{TYPES[tid]}({n}) {sample}'
        raise ValueError(tid)
    def compound(self, f, depth, prefix, skip=False):
        while True:
            b = f.read(1)
            if not b: raise EOFError
            tid = b[0]
            if tid == 0: return
            name = read_str(f)
            if skip:
                self.skip(f, tid)
                continue
            v = self.val(f, tid, depth, name)
            if v is not None:
                print('  '*depth + f'{name}: {TYPES[tid]} = {v}')

def dump_gz_leveldat(path):
    raw = gzip.decompress(open(path, 'rb').read())
    f = io.BytesIO(raw)
    assert f.read(1)[0] == 10
    read_str(f)
    D().compound(f, 0, None)

def dump_region_chunk(path, cx=0, cz=0):
    with open(path, 'rb') as fh:
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        loc = struct.unpack('>I', mm[(cx + 31*cz)*4:(cx + 31*cz)*4+4])[0]
        assert loc, 'chunk not stored'
        sec_off, sec_cnt = loc >> 8, loc & 0xFF
        size, fmt = struct.unpack('>IB', mm[sec_off*4096:sec_off*4096+5])
        data = mm[sec_off*4096+5:sec_off*4096+4+size]
        if fmt == 2:
            import zlib
            try: raw = gzip.decompress(data)
            except Exception: raw = zlib.decompress(data)   # 1.21.11+: fmt 2 = zlib!
        elif fmt == 1:
            import zlib; raw = zlib.decompress(data)
        elif fmt == 0: raw = data
        else: raise ValueError(f'unknown chunk fmt {fmt}')
    f = io.BytesIO(raw)
    assert f.read(1)[0] == 10
    read_str(f)
    D(quiet_list_max=3).compound(f, 0, None)

if __name__ == '__main__':
    mode = sys.argv[1]
    if mode == 'level':
        dump_gz_leveldat(sys.argv[2])
    elif mode == 'chunk':
        a = [int(x) for x in sys.argv[3:5]] or [0,0]
        dump_region_chunk(sys.argv[2], *(a if a else [0,0]))
