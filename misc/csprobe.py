#!/usr/bin/env python3
# csprobe.py — inspect a frozen child's cache-file vnode CS state via KRW.
# Usage: python3 csprobe.py <pid>
# Dumps: proc->fds->vnode->ubc_info{ui_control(=vm_object),cs_blobs}, blob coverage,
#        vm_object.code_signed bit, and pmap_cs_entry field.
import ctypes, sys

jb = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
xpc = ctypes.CDLL("/usr/lib/system/libxpc.dylib")
jb.jbclient_initialize_primitives()
kr64 = jb.kread64; kr64.restype = ctypes.c_uint64; kr64.argtypes=[ctypes.c_uint64]
kr32 = jb.kread32; kr32.restype = ctypes.c_uint32; kr32.argtypes=[ctypes.c_uint64]
kr8  = jb.kread8;  kr8.restype  = ctypes.c_uint8;  kr8.argtypes=[ctypes.c_uint64]

# --- kernel slide from jbinfo serialized dict ---
jb.jbinfo_get_serialized.restype = ctypes.c_void_p
d = jb.jbinfo_get_serialized()
xpc.xpc_dictionary_get_uint64.restype = ctypes.c_uint64
xpc.xpc_dictionary_get_uint64.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
slide  = xpc.xpc_dictionary_get_uint64(d, b"kernelConstant.slide")
kbase  = xpc.xpc_dictionary_get_uint64(d, b"kernelConstant.base")
print(f"slide={slide:#x} kbase={kbase:#x}")

def unpac(p):
    v = p & 0x000003ffffffffff
    if v & 0x0000020000000000:
        v |= 0xfffffc0000000000
    return v

def K( a ): return kr64(a)
def K32(a): return kr32(a)

IDB = 0xfffffe0007004000  # idb imagebase
def rt(idb_addr): return idb_addr + slide

# sysctl-discovered globals
CS_DEBUG            = rt(0xfffffe000aa54188)
CS_EXEC_FAIL        = rt(0xfffffe000aa54190)
CS_MMAP_FAIL        = rt(0xfffffe000aa54194)
print("cs_debug=%u exec_fail=%u mmap_fail=%u" % (K32(CS_DEBUG), K32(CS_EXEC_FAIL), K32(CS_MMAP_FAIL)))

# --- find proc via pidhash ---
PIDHASH_TBL = rt(0xfffffe00079874d0); PIDHASH_MSK = rt(0xfffffe00079874d8)
pid = int(sys.argv[1])
def find_proc(pid):
    t=K(PIDHASH_TBL); m=K32(PIDHASH_MSK); c=K(t+(m&pid)*8)
    for _ in range(1024):
        if not c: return 0
        if K32(c+0x60)==pid and K(c+0x18): return c
        c=K(c+0xA0)
    return 0

p = find_proc(pid)
print(f"proc={p:#x}")
if not p: sys.exit(1)

fdtab = unpac(K(p+0xf8))
print(f"fdtab={fdtab:#x}")
for i in range(0,24):
    fp = K(fdtab+i*8)
    if not (0xfffffe0000000000 <= fp <= 0xfffffe7fffffffff): continue
    fg = unpac(K(fp+0x10))
    if not (0xfffffe0000000000 <= fg <= 0xfffffe7fffffffff): continue
    vp = unpac(K(fg+0x38))
    if not (0xfffffe0000000000 <= vp <= 0xfffffe7fffffffff):
        continue
    vtype = K32(vp+0x70) & 0xffff
    ubc = unpac(K(vp+0x78))
    print(f"fd{i}: vnode={vp:#x} vtype={vtype:#x} ubc={ubc:#x}")
    if not ubc: continue
    ui_pager   = K(ubc+0x00)
    ui_control = unpac(K(ubc+0x08))
    blobs      = unpac(K(ubc+0x50))
    ui_flags   = K32(ubc+0x28)
    print(f"    ui_pager={ui_pager:#x} ui_control(obj)={ui_control:#x} ui_flags={ui_flags:#x} cs_blobs={blobs:#x}")
    # vm_object fields (xnu-8792): +0x50 pager, +0x7c flags1(internal bit16), +0xac flags2(code_signed bit8)
    if 0xfffffe0000000000 <= ui_control <= 0xfffffe7fffffffff:
        f1 = K32(ui_control+0x7c)
        f2 = K32(ui_control+0xac)
        pager = K(ui_control+0x50)
        print(f"    vm_object: flags1={f1:#x} internal={(f1>>16)&1} pager_ready={(f1>>13)&1} pager={pager:#x} code_signed={(f2>>8)&1} flags2={f2:#x}")
    b = blobs
    n = 0
    while b and 0xfffffe0000000000 <= b <= 0xfffffe7fffffffff and n < 8:
        n += 1
        base  = K(b+0x28); start = K(b+0x30); end = K(b+0x38)
        memsz = K(b+0x40); memoff= K(b+0x48); kaddr= K(b+0x50)
        flags = K32(b+0x20); ht = K(b+0x70)
        cd    = K(b+0xa0)
        pmapcse = K(b+0xe0)
        cdh = b''.join(K32(b+0x58+j*4).to_bytes(4,'little') for j in range(5)).hex()
        print(f"    blob{n} @{b:#x}: base={base:#x} start={start:#x} end={end:#x} memsize={memsz:#x} kaddr={kaddr:#x} cd={cd:#x} flags={flags:#x} pmap_cs_entry={pmapcse:#x}")
        print(f"          cdhash={cdh}")
        b = unpac(K(b+0x00))
print("done")
