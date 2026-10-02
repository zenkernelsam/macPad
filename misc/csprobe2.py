#!/usr/bin/env python3
# csprobe2.py — walk a frozen child's vm_map for the faulting VA, dump the
# entry and the full vm_object shadow chain (pager/control/code_signed/blob).
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
print(f"slide={slide:#x}")

def K(a):
    if not (0xfffffe0000000000<=a<0xfffffe8000000000): return 0
    return k64(a)
def K32(a):
    if not (0xfffffe0000000000<=a<0xfffffe8000000000): return 0
    return k32(a)
def kstr(a,n=32):
    b=b""
    for i in range(0,n,8):
        v=K(a+i)
        b+=v.to_bytes(8,"little")
    return b.split(b"\0")[0].decode("ascii","replace")
def unpac(p):
    v=p&0x3ffffffffff
    if v&0x20000000000: v|=0xfffffc0000000000
    return v
def unobj(p32):
    if not p32: return 0
    if p32 & 0x80000000:
        return None  # vm_pages array element — shouldn't happen for objects
    return (p32<<6) + 0xfffffe0000000000

pid=int(sys.argv[1])
vaddr=int(sys.argv[2],16) if len(sys.argv)>2 else 0x1ee188000

TBL=rt(0xfffffe00079874d0); MSK=rt(0xfffffe00079874d8)
t=K(TBL);m=K32(MSK);c=K(t+(m&pid)*8)
for _ in range(1024):
    if not c: break
    if K32(c+0x60)==pid and K(c+0x18): break
    c=K(c+0xa0)
if not c: print("proc not found"); sys.exit(1)
print(f"proc={c:#x}")
ro=unpac(K(c+0x18))          # proc+0x18 = proc_ro
task=unpac(K(ro+0x8))        # proc_ro+0x8  = pr_task  (doc: ro+0x8 -> task)
print(f"ro={ro:#x} task={task:#x}")

# locate task->map: scan task fields for a pointer whose target is a vm_map.
# Signature: links.next points to an entry whose links.prev == &hdr.links (map+0x10).
def looks_like_map(p):
    if not (0xfffffe0000000000<=p<0xfffffe8000000000): return False
    nxt=unpac(K(p+0x18))
    if not (0xfffffe0000000000<=nxt<0xfffffe8000000000): return False
    if nxt == p+0x10: return False          # empty map: next->own head
    back=unpac(K(nxt))                       # entry.links.prev
    return back == p+0x10

vmap=unpac(K(task+0x28))     # doc: task+0x28 (PAC) -> map
if looks_like_map(vmap):
    print(f"task+0x28 -> map={vmap:#x} nentries={K32(vmap+0x30):#x} min={K(vmap+0x20):#x} max={K(vmap+0x28):#x}")
else:
    print(f"task+0x28 map={vmap:#x} fails closure check; scanning")
    vmap=0
    for off in range(0x18,0x200,8):
        p=unpac(K(task+off))
        if looks_like_map(p):
            vmap=p
            print(f"task+{off:#x} -> map={p:#x} nentries={K32(p+0x30):#x} min={K(p+0x20):#x} max={K(p+0x28):#x}")
            break
if not vmap: print("no vm_map found"); sys.exit(1)

# vm_map_entry layout (xnu-8792, RB_ENTRY=24B):
#   links 0x00(32B) | store 0x20(24B) | union{obj|ctx+obj} 0x38(8B)
#   flags1(alias:12|vme_offset:52) 0x40 | flags2(32 bools) 0x48 | wired 0x50
FL2=["is_shared","unused1","in_transition","needs_wakeup","b0","b1","needs_copy",
     "prot0","prot1","prot2","tpro","max0","max1","max2","max3","inh0","inh1",
     "use_pmap","no_cache","permanent","superpage","map_aligned","zero_wired",
     "jit","pmap_cs","iokit","resilient_cs","resilient_media","u28","no_copy_rd",
     "translated_x","kernel_obj"]
def dec2(f):
    return " ".join(nm for i,nm in enumerate(FL2) if (f>>i)&1)

# 1) FULL walk: print every entry that is a submap OR carries pmap_cs/permanent,
#    and always the faulting entry.
e=unpac(K(vmap+0x18)); hdr=vmap+0x10
n=0; found=0; submaps=[]; flagged=[]
obj=0; objpack=0
while e and e!=hdr and n<4000:
    n+=1
    st=K(e+0x10); en=K(e+0x18)
    ctx=K32(e+0x38); issub=(ctx>>1)&1
    flags=K32(e+0x48)
    if issub:
        sub=unpac(K(e+0x38)) & ~0xffff  # vme_submap: low bits hold ctx flags
        submaps.append((e,st,en,sub))
    if flags & ((1<<24)|(1<<19)):
        flagged.append((e,st,en,flags,issub))
    if st<=vaddr<en:
        found=e
        aoff=K(e+0x40)
        voff=(aoff>>12)&((1<<52)-1)
        prot=(flags>>7)&7; maxp=(flags>>11)&0xf
        print(f"ENTRY {e:#x} {st:#x}..{en:#x}")
        print(f"  ctx={ctx:#x} is_submap={issub} atomic={ctx&1}")
        print(f"  vme_offset={voff:#x} flags2={flags:#x}")
        print(f"  prot={prot:#x} maxprot={maxp:#x} [{dec2(flags)}]")
        objpack=K32(e+0x3c)   # LP64 ctx member: ctx@+0x38 u32, vme_object@+0x3c
        obj=unobj(objpack)
        if obj is None:
            obj=unpac(K(e+0x38))
        print(f"  vme_object={obj:#x}")
    e=unpac(K(e+0x8))

print(f"-- {n} entries walked")
for e_,st,en,sub in submaps:
    print(f"SUBMAP entry {e_:#x} {st:#x}..{en:#x} submap={sub:#x}")
for e_,st,en,fl,issub in flagged:
    print(f"FLAGGED {e_:#x} {st:#x}..{en:#x} flags2={fl:#x} sub={issub} [{dec2(fl)}]")

# 2) task->shared_region: scan task for ptr to obj w/ sr_base=0x180000000, sr_size=0x100000000
print("-- shared_region scan in task --")
for off in range(0x100,0x600,8):
    p=unpac(K(task+off))
    if not (0xfffffe0000000000<=p<0xfffffe8000000000): continue
    if K(p+0x30)==0x180000000 and K(p+0x38)==0x100000000:
        print(f"task+{off:#x} -> SR {p:#x} ref={K32(p)} slide={K32(p+4):#x} "
              f"first_mapping={K(p+0x28):#x} nesting={K(p+0x40):#x}+{K(p+0x48):#x} "
              f"mem_entry={unpac(K(p+0x20)):#x} root_dir={unpac(K(p+0x10)):#x}")
        me=unpac(K(p+0x20))
        if me:
            # named entry: backing.map @ +0x20 (mach_memory_entry.is_map_submap path varies; scan)
            for moff in range(0x10,0x60,8):
                cand=unpac(K(me+moff))
                if looks_like_map(cand):
                    print(f"  SR mem_entry+{moff:#x} -> submap {cand:#x} nentries={K32(cand+0x30):#x}")
                    se=unpac(K(cand+0x18)); shdr=cand+0x10; m=0
                    while se and se!=shdr and m<64:
                        m+=1
                        sst=K(se+0x10); sen=K(se+0x18); sfl=K32(se+0x48); sctx=K32(se+0x38)
                        print(f"    SR-submap entry {se:#x} {sst:#x}..{sen:#x} ctx={sctx:#x} flags2={sfl:#x} [{dec2(sfl)}]")
                        se=unpac(K(se+0x8))
        break

if not found:
    print(f"no entry covers {vaddr:#x}")
    sys.exit(0)

# object chain: entry obj -> shadow -> shadow ... each with pager/paging_offset
o=obj; depth=0
while o and 0xfffffe0000000000<=o<0xfffffe8000000000 and depth<8:
    refc=K32(o+0x28); res=K32(o+0x2c)
    copy=unpac(K(o+0x38)); shadow=unpac(K(o+0x40))
    pager=K(o+0x48); shoff=K(o+0x50); poff=K(o+0x58); pctl=unpac(K(o+0x60))
    bf1=K32(o+0x74); bf2=K32(o+0xa4)
    print(f"obj[{depth}]={o:#x} ref={refc} resident={res} copy={copy:#x} shadow={shadow:#x}")
    print(f"   pager={pager:#x} pager_ctl={pctl:#x} shadow_off={shoff:#x} paging_off={poff:#x}")
    print(f"   bf1={bf1:#x} pager_created={bf1>>11&1} pager_init={bf1>>12&1} pager_ready={bf1>>13&1} trusted={bf1>>14&1} persist={bf1>>15&1} internal={bf1>>16&1} private={bf1>>17&1} pageout={bf1>>18&1} alive={bf1>>19&1} shadowed={bf1>>24&1} severed={bf1>>28&1} named={bf1>>27&1}")
    print(f"   bf2={bf2:#x} wimg={bf2&0xff:#x} code_signed={(bf2>>8)&1} transposed={(bf2>>9)&1} map_in_prog={(bf2>>10)&1}")
    if pager:
        # pager is memory_object_t: {ikot@0,ref@4, ops@8, control@0x10}
        ops=K(pager+0x8)
        name=kstr(K(ops+0x68)) if ops else ""
        ctl=K(pager+0x10)
        print(f"   mo.ikot={K32(pager):#x} ops={ops:#x} mo_control={ctl:#x} name={name!r}")
        if name=="vnode pager":
            vn=unpac(K(pager+0x18))
            ubc=unpac(K(vn+0x78)) if vn else 0
            uic=unpac(K(ubc+0x08)) if ubc else 0
            csb=unpac(K(ubc+0x50)) if ubc else 0
            print(f"   vnode={vn:#x} ubc={ubc:#x} ui_control={uic:#x} ui_flags={K32(ubc+0x28):#x} cs_blobs={csb:#x}")
            # cs_blob: csb_next@0x0? verify by dumping head fields
            bl=csb; bn=0
            while bl and bn<4:
                print(f"   blob[{bn}]@{bl:#x}: next={unpac(K(bl)):#x} vnode={unpac(K(bl+8)):#x} "
                      f"flags={K32(bl+0x20):#x} base={K(bl+0x28):#x} start={K(bl+0x30):#x} end={K(bl+0x38):#x}")
                print(f"      mem_size={K(bl+0x40):#x} mem_off={K(bl+0x48):#x} mem_kaddr={unpac(K(bl+0x50)):#x} "
                      f"hashtype={unpac(K(bl+0x70)):#x} pageshift={K32(bl+0x98)} cd={unpac(K(bl+0xa0)):#x} pmap_cs_entry={unpac(K(bl+0xe0)):#x}")
                bl=unpac(K(bl+0x00)); bn+=1
        if name=="dyld":
            bk=unpac(K(pager+0x20))   # dyld_backing_object
            print(f"   dyld_backing_object={bk:#x}")
            if bk:
                bref=K32(bk+0x28); bres=K32(bk+0x2c); vosz=K(bk+0x18)
                bpg=K(bk+0x48); bpgctl=unpac(K(bk+0x60)); bb1=K32(bk+0x74); bb2=K32(bk+0xa4)
                bops=K(bpg+0x8) if bpg else 0
                bname=kstr(K(bops+0x68)) if bops else ""
                print(f"   backing: ref={bref} resident={bres} vo_size={vosz:#x} ({vosz*0x4000:#x} bytes)")
                print(f"   backing: pager={bpg:#x} ctl={bpgctl:#x} ops={bops:#x} name={bname!r}")
                print(f"   backing: bf1={bb1:#x} internal={bb1>>16&1} ready={bb1>>13&1} bf2={bb2:#x} code_signed={(bb2>>8)&1}")
                if bname=="vnode pager":
                    bvn=unpac(K(bpg+0x18))   # vnode_pager.vnode_handle @+0x18
                    bubc=unpac(K(bvn+0x78)) if bvn else 0
                    buc=unpac(K(bubc+0x08)) if bubc else 0
                    print(f"   backing vnode={bvn:#x} ubc={bubc:#x} ui_control={buc:#x} (self-check: should eq backing obj {bk:#x})")
                # dump more dyld_pager fields: link_info, num_range, file_offset/address/size tables
                li=unpac(K(pager+0x28)); lsz=K32(pager+0x30); nr=K32(pager+0x34)
                print(f"   link_info={li:#x} sz={lsz:#x} num_range={nr}")
                for i in range(min(nr,8)):
                    fo=K(pager+0x38+i*8); va=K(pager+0x38+nr*8+i*8); sz=K(pager+0x38+nr*16+i*8)
                    print(f"   range[{i}] file_off={fo:#x} va={va:#x} size={sz:#x}")
                # mwl_info_hdr: version@0 u32, page_size@4 u16, pointer_format@6 u16,
                # binds_off@8, binds_cnt@0xc, chains_off@0x10, chains_size@0x14,
                # slide@0x18, image_address@0x20
                ver=K32(li); pgsz=K32(li+4)&0xffff; pfmt=K32(li+6)&0xffff
                boff=K32(li+8); bcnt=K32(li+0xc); coff=K32(li+0x10); csz=K32(li+0x14)
                mslide=K(li+0x18); img=K(li+0x20)
                print(f"   mwli: ver={ver} pgsz={pgsz:#x} pfmt={pfmt} binds={boff:#x}+{bcnt} chains={coff:#x}+{csz:#x}")
                print(f"   mwli: slide={mslide:#x} image_address={img:#x}")
                si=li+coff
                segcnt=K32(si)     # dyld_chained_starts_in_image: seg_count@0, seg_info_offset[]@4
                print(f"   startsInfo@{si:#x} seg_count={segcnt}")
                uva=vaddr
                for si_ in range(min(segcnt,12)):
                    soff=K32(si+4+si_*4)
                    seg=si+soff
                    segsz=K32(seg); sps=K32(seg+4)&0xffff; sfmt=K32(seg+6)&0xffff
                    seg_off=K(seg+8); mvp=K32(seg+0x10); spc=K32(seg+0x14)&0xffff
                    sstart=img+seg_off; send=sstart+spc*sps
                    cov="COVERS" if sstart<=uva<send else ""
                    pidx=(uva-sstart)//sps if sps else -1
                    ps=K32(seg+0x16+pidx*2)&0xffff if (cov and sstart<=uva and pidx<spc and seg+0x16+pidx*2+2<=li+lsz) else None
                    print(f"   seg[{si_}]@{seg:#x} size={segsz:#x} pgsz={sps:#x} fmt={sfmt} seg_off={seg_off:#x} pages={spc} va={sstart:#x}..{send:#x} {cov} pageidx={pidx} page_start={ps}")
    o=shadow; depth+=1

# dump child fd table vnodes for comparison with backing vnode
fdtab = unpac(K(c+0xf8))
print(f"fdtab={fdtab:#x}")
for i in range(0,24):
    fp = K(fdtab+i*8)
    if not (0xfffffe0000000000 <= fp <= 0xfffffe7fffffffff): continue
    fg = unpac(K(fp+0x10))
    if not (0xfffffe0000000000 <= fg <= 0xfffffe7fffffffff): continue
    vp = unpac(K(fg+0x38))
    if not (0xfffffe0000000000 <= vp <= 0xfffffe7fffffffff): continue
    vtype = K32(vp+0x70) & 0xffff
    ubc = unpac(K(vp+0x78))
    ucsz = unpac(K(ubc+0x08)) if ubc else 0
    print(f"fd{i}: vnode={vp:#x} vtype={vtype:#x} ubc={ubc:#x} ui_obj={ucsz:#x}")
print("done")
