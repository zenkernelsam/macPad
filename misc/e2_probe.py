#!/usr/bin/env python3
# S1/S2 probe. Always clears the guard default first so the process survives
# long enough to report, then chroot+execve /bin/echo with a variant env.
#   mode = noinsert  : no DYLD_INSERT_LIBRARIES   -> is the 112B blob from libmachook?
#   mode = cacheempty: DYLD_SHARED_CACHE_DIR=<empty dir> + DYLD_PRINT_LIBRARIES
#   mode = plain     : baseline with the guard cleared, no extra env
import ctypes, os, sys

MODE = sys.argv[1] if len(sys.argv) > 1 else "plain"
ROOT = "/var/mnt/rootfs"
GUARD_RT = 0xFFFFFE002526EBE0
EMPTY_DIR = "/tmp/dsc_none"          # created by the caller, empty

L = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
L.jbclient_process_checkin.restype = ctypes.c_int
L.jbclient_process_checkin(ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_char_p()),
                          ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_bool()))
L.jbclient_initialize_primitives.restype = ctypes.c_int
L.jbclient_initialize_primitives()
L.kread32.restype = ctypes.c_uint32; L.kread32.argtypes = [ctypes.c_uint64]
L.kwrite32.restype = ctypes.c_int;   L.kwrite32.argtypes = [ctypes.c_uint64, ctypes.c_uint32]
v = L.kread32(GUARD_RT)
if v == 0x99:
    L.kwrite32(GUARD_RT, 0x90)
print("[probe] %s: guard %#x -> %#x" % (MODE, v, L.kread32(GUARD_RT)), flush=True)

libc = ctypes.CDLL(None)
libc.chroot.argtypes = [ctypes.c_char_p]; libc.chroot.restype = ctypes.c_int
libc.chdir.argtypes = [ctypes.c_char_p];  libc.chdir.restype = ctypes.c_int

env = {
    "PATH": "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
    "HOME": "/Users/root", "USER": "root", "TMPDIR": "/tmp", "SHELL": "/bin/bash",
}
if MODE != "noinsert" and os.path.exists(ROOT + "/usr/local/lib/libmachook.dylib"):
    env["DYLD_INSERT_LIBRARIES"] = "/usr/local/lib/libmachook.dylib"
if MODE == "cacheempty":
    env["DYLD_SHARED_CACHE_DIR"] = EMPTY_DIR
    env["DYLD_PRINT_LIBRARIES"] = "1"
    print("[probe] DYLD_SHARED_CACHE_DIR=%s (exists in chroot: %s)"
          % (EMPTY_DIR, os.path.exists(ROOT + EMPTY_DIR)), flush=True)

r1 = libc.chroot(ROOT.encode()); r2 = libc.chdir(b"/")
print("[probe] chroot rc=%d chdir rc=%d -> exec" % (r1, r2), flush=True)
if r1 or r2:
    sys.exit(3)
os.execve("/bin/echo", ["/bin/echo", "HI"], env)
