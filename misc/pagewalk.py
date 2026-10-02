#!/usr/bin/env python3
# pagewalk.py <pid> <vaddr> [target_obj_off]
# Walk the faulting entry's object chain -> find vnode object -> walk memq
# -> locate page at given object offset -> dump vmp flag bits.
import ctypes, sys

jb  = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
xpc = ctypes.CDLL("/usr/lib/system/libxpc.dylib")
jb.jbclient_initialize_primitives()
k64=jb.kread64;k64.restype=ctypes.c_uint64;k64.argtypes=[ctypes.c_uint64]
k32=jb.kread32;k32.restype=ctypes.c_uint32;k32.argtypes=[ctypes.c_uint64]
jb.jbinfo_get_serialized.restype=ctypes.c_void_p
d=jb.jbinfo_get_serialized()
xpc.xpc_dictionary_get_uint64.restype=ctypes.c_uint64
xpc.xpc_dictionary_get_uint64.argtypes=[ctypes.c_void_p,ctypes.c_char_p]
slide=xpc.xpc_dictionary_get_uint64(d,b"kernelConstant.slide")
KBASE=0xfffffe0007004000
def rt(a): return a+slide

def K(a):
    if not (0xfffffe0000000000<=a<0xfffffe8000000000): return 0
    return k64(a)
def K32(a):
    if not (0xfffffe0000000000<=a<0xfffffe8000000000): return 0
    return k32(a)
def unpac(p):
    v=p&0x3ffffffffff
    if v&0x20000000000: v|=0xfffffc0000000000
    return v
LOWGLO = rt(0xfffffe000a9f4000)
VMPAGES = K(LOWGLO+0x88)             # lgPmapMemStartAddr
VMPGSZ  = K(LOWGLO+0x98)             # lgPmapMemPagesize (0x30)
print(f"vm_pages base={VMPAGES:#x} stride={VMPGSZ:#x}")
def unpack_page(p32):
    # vm_page_packed_t: bit31 -> index into vm_pages[] array, else <<6 in zone
    if p32==0: return 0
    if p32 & 0x80000000:
        return VMPAGES + (p32 & 0x7fffffff)*VMPGSZ
    return (p32<<6) + 0xfffffe0000000000
def unobj(p32):
    if not p32: return 0
    return (p32<<6) + 0xfffffe0000000000

pid=int(sys.argv[1])
vaddr=int(sys.argv[2],16)
t_off=int(sys.argv[3],16) if len(sys.argv)>3 else ((vaddr & ~0x3fff) - 0x180000000)

# proc
TBL=rt(0xfffffe00079874d0); MSK=rt(0xfffffe00079874d8)
t=K(TBL);m=K32(MSK);c=K(t+(m&pid)*8)
for _ in range(1024):
    if not c: break
    if K32(c+0x60)==pid and K(c+0x18): break
    c=K(c+0xa0)
assert c
ro=unpac(K(c+0x18)); task=unpac(K(ro+0x8))
vmap=unpac(K(task+0x28))
e=unpac(K(vmap+0x18)); hdr=vmap+0x10
found=0
while e and e!=hdr:
    st=K(e+0x10); en=K(e+0x18)
    if st<=vaddr<en:
        found=e; break
    e=unpac(K(e+0x8))
assert found, "no entry"
obj=unobj(K32(e+0x3c))
print(f"entry_obj={obj:#x}")

# walk chain to deepest object (vnode obj)
o=obj; depth=0
while o and depth<8:
    res=K32(o+0x2c); pager=K(o+0x48); shadow=unpac(K(o+0x40))
    bf2=K32(o+0xa4)
    print(f"obj[{depth}]={o:#x} resident={res} code_signed={(bf2>>8)&1} pager={pager:#x}")
    if not shadow: break
    o=shadow; depth+=1

# o = bottom object (vnode object). Dump paging_offset + vnode cs_blob + CD header
vobj=o
paging_off = K(vobj+0x58)
print(f"bottom obj paging_offset={paging_off:#x}")

# resolve vnode via pager (vnode_pager.vnode_handle @ pager+0x18)
pager = K(vobj+0x48)
vnode = 0
if pager:
    vnode = unpac(K(pager+0x18))
    print(f"pager={pager:#x} vnode={vnode:#x}")
if vnode:
    ubc = unpac(K(vnode+0x78))
    blobs = unpac(K(ubc+0x50))
    print(f"ubc={ubc:#x} cs_blobs={blobs:#x}")
    b = blobs
    for i in range(4):
        if not b: break
        print(f" blob[{i}]@{b:#x} flags={K(b+0x10):#x} base={K(b+0x28):#x} start={K(b+0x30):#x} "
              f"end={K(b+0x38):#x} memsz={K(b+0x40):#x} memoff={K(b+0x48):#x} kaddr={K(b+0x50):#x}")
        ht = K(b+0x70); cd = K(b+0x80); pgs = K32(b+0x78)
        print(f"   hashtype={ht:#x} pageshift={pgs} cd={cd:#x}")
        if cd:
            import struct as st
            def bu32(o): return st.unpack(">I", st.pack("<I",K32(cd+o)))[0]
            def bu8(o):  return (K32(cd+o)>>((3-(o&3))*8))&0xff
            magic=bu32(0); ln=bu32(4); ver=bu32(8); fl=bu32(0xc)
            ho=bu32(0x10); ns=bu32(0x18); nc=bu32(0x1c); cl=bu32(0x20)
            packed=bu32(0x24); hs=packed>>24; htt=(packed>>16)&0xff; pf=(packed>>8)&0xff; ps=packed&0xff
            sc=bu32(0x2c); tm=bu32(0x30)
            print(f"   CD magic={magic:#x} len={ln:#x} ver={ver:#x} flags={fl:#x} hashOff={ho:#x} "
                  f"nSpec={ns} nCode={nc} codeLimit={cl:#x} hashSize={hs} hashType={htt} "
                  f"pageSize=2^{ps} scatterOff={sc:#x} teamOff={tm:#x}")
            # hash slot for our page (page idx = offset>>ps, scatter-aware)
            idx = t_off >> ps
            if sc:
                # per cs_validate_hash decompile: entry = {count:u32@0, base:u32@4,..} stride 24B
                # slotbase accumulates counts until base+count >= idx
                e = cd + sc
                acc = 0
                found = None
                while True:
                    cnt_ = st.unpack(">I", st.pack("<I",K32(e)))[0]
                    base_ = st.unpack(">I", st.pack("<I",K32(e+4)))[0]
                    if cnt_ == 0: break
                    if base_ + cnt_ >= idx:
                        found = (acc, base_); break
                    acc += cnt_; e += 24
                if found:
                    acc, base_ = found
                    slot = acc + (idx - base_)
                    haddr = cd + ho + slot*hs
                    hval = b"".join(st.pack("<I",K32(haddr+k*4)) for k in range(hs//4))
                    print(f"   scatter base={base_:#x} slot={slot} hash@{haddr:#x} = {hval.hex()}")
                else:
                    print(f"   scatter: idx {idx:#x} not covered")
            else:
                haddr = cd + ho + idx*hs
                hval = b"".join(st.pack("<I",K32(haddr+k*4)) for k in range(hs//4))
                print(f"   slot[{idx}] @{haddr:#x} = {hval.hex()}")
        b = unpac(K(b+0x0))

    # per-vnode cs validation bitmap: ubc+0x71 bit0x40 enables, ubc+0x58 = bitmap
    ub = ubc
    bmp_en = K32(ub+0x70)
    bmp    = unpac(K(ub+0x58))
    nchunk = K(ub+0x60)
    print(f"ubc bitmap: flags@70={bmp_en:#x} ptr@58={bmp:#x} bound@60={nchunk:#x}")
    if bmp:
        byte_i = t_off >> 17
        bit_i  = (t_off >> 14) & 7
        byte_v = K32(bmp + byte_i) if byte_i < nchunk else -1
        # byte read as 32 bits — pick right byte
        byte_v = (K32(bmp + (byte_i & ~3)) >> ((byte_i&3)*8)) & 0xff
        print(f"   bitmap[{byte_i:#x}]={byte_v:#x} bit{bit_i} validated={(byte_v>>bit_i)&1}")

# memq walk (may be incomplete if pages not resident)
head=vobj
nxt=K32(vobj+0x0)
print(f"memq.next={nxt:#x} memq.prev={K32(vobj+0x4):#x} head={head:#x}")
cnt=0
targ=0
while cnt<40000:
    pg=unpack_page(nxt)
    if not pg or pg==head: break
    off=K(pg+0x18)
    if cnt<6:
        print(f"  page[{cnt}]@{pg:#x} off={off:#x} q0={K32(pg):#x} listq={K32(pg+8):#x} fl={K32(pg+0x2c):#x}")
    if off==t_off:
        targ=pg
        fl=K32(pg+0x2c)
        objp=K32(pg+0x20)
        print(f"PAGE @{pg:#x} off={off:#x} flags={fl:#x}")
        print(f"  busy={fl&1} pmapped={(fl>>6)&1} xpmapped={(fl>>7)&1} wpmapped={(fl>>8)&1} "
              f"error={(fl>>11)&1} dirty={(fl>>12)&1} absent={(fl>>10)&1} unusual={(fl>>17)&1}")
        print(f"  validated={((fl>>18)&0xf):#x} tainted={((fl>>22)&0xf):#x} nx={((fl>>26)&0xf):#x} "
              f"reusable={(fl>>30)&1} wrkern={(fl>>31)&1}")
        print(f"  packed_obj={objp:#x} -> obj={(objp<<6)+0xfffffe0000000000:#x}")
        break
    nxt=K32(pg+0x08)
    cnt+=1
if not targ:
    print(f"page off={t_off:#x} not resident ({cnt} pages walked)")
