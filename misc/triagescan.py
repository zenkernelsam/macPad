#!/usr/bin/env python3
# triagescan.py — dump kdebug triage records.
# B0[idx*16] -> region base; region = 8 blocks x 131104B.
# block: +4 = record count (<=2047), records at +0x40, stride 0x40:
#   rec+0 eventid (class<<24|code<<2), +8 tid, +0x20 ts, +0x28 arg
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
def rt(a): return a+slide
def K(a):
    if not (0xfffffe0000000000<=a<0xfffffe8000000000): return 0
    return k64(a)
def K32(a):
    if not (0xfffffe0000000000<=a<0xfffffe8000000000): return 0
    return k32(a)
print(f"slide={slide:#x}")

GLOB = rt(0xfffffe000aa540b0)
ARR0 = K(GLOB)                 # maybe ptr to array
bases = set()
for src in (GLOB, ARR0):
    if not src: continue
    for idx in range(0, 256):
        g = K(src + idx*16)
        if g == 0:
            if idx > 8: break
            continue
        if 0xfffffe0000000000 <= g < 0xfffffe8000000000:
            bases.add(g)
        elif idx > 16:
            break
print(f"candidate region bases: {len(bases)}")
BLKSZ = 131104
events = []
seen_ev = {}
for grp in sorted(bases):
    for b in range(8):
        blk = grp + b*BLKSZ
        cnt = K32(blk+4)
        if cnt == 0 or cnt > 2047: continue
        for r in range(cnt):
            rec = blk + 0x40 + (r << 6)
            ev = K(rec); tid=K(rec+8); ts=K(rec+0x20); a3=K(rec+0x28)
            cls=(ev>>24)&0xff; code=(ev>>2)&0x3fff
            if cls==0 or cls>9 or (ev&3) or (ev&0xff0000): continue
            events.append((ts,ev,tid,a3,grp,b,r))
            seen_ev[ev]=seen_ev.get(ev,0)+1

VM = {1:"NO_DATA",2:"TEXT_CORRUPT",3:"ADDR_NOT_FOUND",4:"PROT_FAIL",5:"FAULT_MEM_SHORT",6:"COPY_MEM_SHORT",9:"INTERRUPTED",10:"SUCCESS_NO_PAGE",11:"GUARDPAGE",13:"BUSYPAGE_WAIT",14:"PURGEABLE",15:"SHADOW_SEVERED",16:"NOT_ALIVE",17:"NO_PAGER",18:"PAGE_HAS_ERROR",19:"PAGE_RESTART",20:"IMMUTABLE_W",21:"NX_EXEC",22:"PMAP_RSC",23:"COMP_OOR",24:"COMP_NOPAGE",25:"COMP_DECOMP",26:"SUBMAP_NOCOW",27:"SUBMAP_SLOW",28:"SUBMAP_STRAT",29:"CLREAD_NO_UPL",30:"NO_UBCINFO",31:"FSPAGEIN_FAIL",32:"VPAGEIN_NO_UPL"}
DP = {1:"NO_UPL",2:"SLIDE_ERROR",3:"MEMORY_SHORTAGE"}
CL = {1:"VM",4:"DYLD_PAGER"}
print(f"events: {len(events)}")
for ev,c in sorted(seen_ev.items(), key=lambda x:-x[1]):
    cls=(ev>>24)&0xff; code=(ev>>2)&0x3fff
    nm = VM.get(code,"") if cls==1 else (DP.get(code,"") if cls==4 else "")
    print(f"  ev={ev:#x} {CL.get(cls,cls)}/{code} {nm} x{c}")
want_tid = int(sys.argv[1],16) if len(sys.argv)>1 else 0
print("recent events:")
for ts,ev,tid,a3,grp,b,r in sorted(events)[-25:]:
    cls=(ev>>24)&0xff; code=(ev>>2)&0x3fff
    nm = VM.get(code,"") if cls==1 else (DP.get(code,"") if cls==4 else "")
    mark = " <== CHILD" if want_tid and tid==want_tid else ""
    print(f"  ev={ev:#x} {CL.get(cls,cls)}/{code} {nm} tid={tid:#x} arg={a3:#x} ts={ts:#x} grp={grp:#x}/b{b}/r{r}{mark}")
print("done")
