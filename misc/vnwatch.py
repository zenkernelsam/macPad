#!/usr/bin/env python3
# vnwatch.py — watch the cache vnode's ubc object resident_page_count while a
# child runs, to determine whether the child ever faults in cache pages.
import ctypes, sys, os, time, subprocess
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
def unpac(p):
    v=p&0x3ffffffffff
    if v&0x20000000000: v|=0xfffffc0000000000
    return v

path="/var/mnt/rootfs/macdsc/dyld_shared_cache_arm64e"
fd=os.open(path, os.O_RDONLY)
pid=os.getpid()
TBL=rt(0xfffffe00079874d0); MSK=rt(0xfffffe00079874d8)
t=K(TBL);m=K32(MSK);c=K(t+(m&pid)*8)
for _ in range(1024):
    if not c: break
    if K32(c+0x60)==pid and K(c+0x18): break
    c=K(c+0xa0)
fdtab=unpac(K(c+0xf8))
fg=unpac(K(K(fdtab+fd*8)+0x10))
vp=unpac(K(fg+0x38))
ubc=unpac(K(vp+0x78))
uic=unpac(K(ubc+0x08))
print(f"cache vnode={vp:#x} object={uic:#x} resident={K32(uic+0x2c)}")

# run child while polling resident count
p=subprocess.Popen(["/var/mobile/run_dbg_v11","/var/jb/usr/bin/chroot","/var/mnt/rootfs","/bin/echo","HI"],
                   stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
t0=time.time(); maxres=0; first=None
while time.time()-t0<3:
    r=K32(uic+0x2c)
    if r>maxres:
        maxres=r
        if first is None: first=(time.time()-t0,r)
    time.sleep(0.001)
p.wait()
print(f"resident before=? max_during={maxres} after={K32(uic+0x2c)}")
print("done")
