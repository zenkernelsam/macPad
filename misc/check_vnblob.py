#!/usr/bin/env python3
import ctypes, ctypes.util, sys
jb = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
jb.jbclient_initialize_primitives()
kr64 = jb.kread64; kr64.restype = ctypes.c_uint64; kr64.argtypes=[ctypes.c_uint64]
kr32 = jb.kread32; kr32.restype = ctypes.c_uint32; kr32.argtypes=[ctypes.c_uint64]
KSLIDE=0x1a129000
PIDHASH_TBL=0xfffffe00079874D0+KSLIDE; PIDHASH_MSK=0xfffffe00079874D8+KSLIDE
def find_proc(pid):
    t=kr64(PIDHASH_TBL); m=kr64(PIDHASH_MSK); c=kr64(t+(m&pid)*8)
    for _ in range(512):
        if not c: return 0
        if kr32(c+0x60)==pid and kr64(c+0x18): return c
        c=kr64(c+0xA0)
def unpac(p):
    v = p & 0x000003ffffffffff
    if v & 0x0000020000000000:
        v |= 0xfffffc0000000000
    return v

pid=int(sys.argv[1])
p=find_proc(pid)
print(f"proc={p:#x}")
fd=kr64(p+0xf8); print(f"filedesc={fd:#x}")
fd=unpac(fd); print(f"filedesc={fd:#x}")
for o in range(0,0x90,8):
    print(f"  +{o:#04x}: {kr64(fd+o):#x}")
ofiles=fd  # proc+0xf8 IS the fd_ofiles array on this kernel
for i in range(0,16):
    fp=kr64(ofiles+i*8)
    if fp==0 or not (0xfffffe0000000000<=fp<=0xfffffe7fffffffff): continue
    fg=unpac(kr64(fp+0x10))
    if fg==0 or not (0xfffffe0000000000<=fg<=0xfffffe7fffffffff):
        print(f"fd{i}: fp={fp:#x} fg_raw={kr64(fp+0x10):#x}")
        continue
    vp=unpac(kr64(fg+0x38))
    if not (0xfffffe0000000000 <= vp <= 0xfffffe7fffffffff):
        print(f"fd{i}: fp={fp:#x} fg={fg:#x} data={vp:#x} (not vnode)")
        continue
    vtype=kr32(vp+0x70)&0xffff
    ubc=unpac(kr64(vp+0x78))
    blob=kr64(ubc+0x50) if ubc else 0
    u8=kr64(ubc+8) if ubc else 0
    u10=kr64(ubc+0x10) if ubc else 0
    print(f"fd{i}: vnode={vp:#x} type={vtype} ubc={ubc:#x} ubc+8={u8:#x} ubc+10={u10:#x} cs_blob={blob:#x}")
    if blob and 0xfffffe0000000000<=blob<=0xfffffe7fffffffff:
        bp=unpac(blob)
        print(f"     blob={bp:#x} +38(cov_end)={kr64(bp+0x38):#x} +40={kr64(bp+0x40):#x}")
