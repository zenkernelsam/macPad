#!/usr/bin/env python3
# E2 (variant b): patch the GLOBAL default task_exc_guard_default, then run the
# chroot repro.  Newly exec'd processes pick up the new default, so there is no
# timing problem (unlike patching a per-task field, which exec discards).
#
#   mode = run    : verify global is 0x99, write 0x90, then chroot+exec /bin/echo
#   mode = check  : just print the global
#   mode = restore: write 0x99 back
import ctypes, os, sys

MODE = sys.argv[1] if len(sys.argv) > 1 else "run"
ROOT = "/var/mnt/rootfs"
GUARD_RT = 0xFFFFFE002526EBE0        # task_exc_guard_default (slide 0x1a874000, verified)
PLATFORM_OLD = 0x00000099
PLATFORM_NEW = 0x00000090            # clear VM_DELIVER(0x01) | VM_FATAL(0x08)

L = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
L.jbclient_process_checkin.restype = ctypes.c_int
L.jbclient_process_checkin(ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_char_p()),
                          ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_bool()))
L.jbclient_initialize_primitives.restype = ctypes.c_int
L.jbclient_initialize_primitives()
L.kread32.restype = ctypes.c_uint32; L.kread32.argtypes = [ctypes.c_uint64]
L.kwrite32.restype = ctypes.c_int;   L.kwrite32.argtypes = [ctypes.c_uint64, ctypes.c_uint32]
K32, W32 = L.kread32, L.kwrite32

v = K32(GUARD_RT)
print("[e2b] task_exc_guard_default @%#x = %#010x" % (GUARD_RT, v), flush=True)

if MODE == "check":
    sys.exit(0)
if MODE == "restore":
    rc = W32(GUARD_RT, PLATFORM_OLD)
    print("[e2b] restore rc=%d -> %#010x" % (rc, K32(GUARD_RT)), flush=True)
    sys.exit(0)

if v != PLATFORM_OLD:
    print("[e2b] ABORT: expected %#x, refusing to write" % PLATFORM_OLD, flush=True)
    sys.exit(4)
rc = W32(GUARD_RT, PLATFORM_NEW)
after = K32(GUARD_RT)
print("[e2b] wrote %#x rc=%d -> %#010x %s"
      % (PLATFORM_NEW, rc, after, "OK" if after == PLATFORM_NEW else "FAILED"), flush=True)
if after != PLATFORM_NEW:
    sys.exit(5)

libc = ctypes.CDLL(None)
libc.chroot.argtypes = [ctypes.c_char_p]; libc.chroot.restype = ctypes.c_int
libc.chdir.argtypes = [ctypes.c_char_p];  libc.chdir.restype = ctypes.c_int
env = {
    "PATH": "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
    "HOME": "/Users/root", "USER": "root", "TMPDIR": "/tmp", "SHELL": "/bin/bash",
}
if os.path.exists(ROOT + "/usr/local/lib/libmachook.dylib"):
    env["DYLD_INSERT_LIBRARIES"] = "/usr/local/lib/libmachook.dylib"
r1 = libc.chroot(ROOT.encode()); r2 = libc.chdir(b"/")
print("[e2b] chroot rc=%d chdir rc=%d -> exec /bin/echo" % (r1, r2), flush=True)
if r1 or r2:
    sys.exit(3)
os.execve("/bin/echo", ["/bin/echo", "HI"], env)
