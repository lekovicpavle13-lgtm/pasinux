import struct
pe = open('C:/Users/lekov/pasinux/pasinux/kernel/kernel.pe','rb').read()
e_lfanew = struct.unpack_from('<I', pe, 0x3C)[0]
print('e_lfanew', hex(e_lfanew))
machine, nsec, tdat, tptr, tsym, optsize, chars = struct.unpack_from('<HHIIIHH', pe, e_lfanew+4)
print(f'machine={machine:#x} nsec={nsec} optsize={optsize}')
opt_off = e_lfanew + 24
print('opt_off', hex(opt_off), 'opt size', optsize)
sec_off = opt_off + optsize
print('sec_off', hex(sec_off))
for i in range(nsec):
    off = sec_off + i*40
    name = pe[off:off+8].split(b'\x00',1)[0].decode('ascii','replace')
    vsize, vaddr, rsize, roff = struct.unpack_from('<IIII', pe, off+8)
    ch = struct.unpack_from('<I', pe, off+36)[0]
    print(f'  {name!r} vsize={vsize:#x} vaddr={vaddr:#x} rsize={rsize:#x} roff={roff:#x} chars={ch:#x}')
    if vsize > 0:
        print(f'    end = {vaddr+vsize:#x}')
