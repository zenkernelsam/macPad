#!/usr/bin/env python3
# vnstate.py — open the dsc file, find its vnode via own fd table, dump
# cs_blobs + ui_control object->code_signed + pager name.
import ctypes, sys, os
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
rt=lambda a:a+slide
def K(a):
    if not (0xfffffe0000000000<=a<0xfffffe8000000000): return 0
    return k64(a)
def K32(a):
    if not (0xfffffe0000000000<=a<0xfffffe8000000000): return 0
    return k32(a)
def kstr(a,n=32):
    b=b""
    for i in range(0,n,8):
        b+=K(a+i).to_bytes(8,"little")
    return b.split(b"\0")[0].decode("ascii","replace")
def unpac(p):
    v=p&0x3ffffffffff
    if v&0x20000000000: v|=0xfffffc0000000000
    return v

path=sys.argv[1] if len(sys.argv)>1 else "/var/mnt/rootfs/macdsc/dyld_shared_cache_arm64e"
fd=os.open(path, os.O_RDONLY)
pid=os.getpid()
TBL=rt(0xfffffe00079874d0); MSK=rt(0xfffffe00079874d8)
t=K(TBL);m=K32(MSK);c=K(t+(m&pid)*8)
for _ in range(1024):
    if not c: break
    if K32(c+0x60)==pid and K(c+0x18): break
    c=K(c+0xa0)
if not c: print("proc not found"); sys.exit(1)
fdtab = unpac(K(c+0xf8))
fp = K(fdtab+fd*8)
fg = unpac(K(fp+0x10))
vp = unpac(K(fg+0x38))
print(f"fd={fd} fp={fp:#x} fg={fg:#x} vnode={vp:#x} vtype={K32(vp+0x70)&0xffff:#x}")
ubc=unpac(K(vp+0x78))
print(f"ubc_info={ubc:#x}")
if ubc:
    csb=K(ubc+0x10)   # ubc_info.cs_blobs — verify offset
    uic=unpac(K(ubc+0x08))
    print(f"ui_control={uic:#x} cs_blobs={csb:#x}")
    # dump ubc_info first 0x40 bytes to sanity-check offsets
    for off in range(0,0x30,8):
        print(f"  ubc+{off:#x} = {K(ubc+off):#x}")
    if csb:
        # cs_blob (RO copy): csb_base_offset, csb_start/end_offset, csb_cd, csb_cdhash
        for off in range(0,0x60,8):
            print(f"  blob+{off:#x} = {K(csb+off):#x}")
    if uic:
        # ui_control is memory_object_control -> vm_object? Actually ui_control IS
        # the control port; kobject is the vm_object. memory_object_control_to_vm_object
        # = ((memory_object_control)->object?) — try control+0x8 mobject or control IS object
        print("--- ui_control object dump ---")
        for off in range(0,0xb0,8):
            print(f"  ctl+{off:#x} = {K(uic+off):#x}")
os.close(fd)
print("done")
