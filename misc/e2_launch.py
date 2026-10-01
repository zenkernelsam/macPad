#!/usr/bin/env python3
# E2 test launcher (device-side).
#   baseline : just chroot + execve            -> expect the kill
#   patch    : clear own task->task_exc_guard, then chroot + execve
#   check    : print own task->task_exc_guard and exit (used after a re-exec)
#   reexec   : clear own guard, then execve THIS script in check mode (no chroot)
#              -> proves whether exec() preserves task_exc_guard
import ctypes, os, sys

MODE = sys.argv[1] if len(sys.argv) > 1 else "baseline"
ROOT = "/var/mnt/rootfs"
KBITS = 0x09          # TASK_EXC_GUARD_VM_DELIVER(0x01) | VM_FATAL(0x08)
SELF = "/var/mobile/e2_launch.py"
PY = "/var/jb/usr/bin/python3"

libc = ctypes.CDLL(None)
libc.chroot.argtypes = [ctypes.c_char_p]; libc.chroot.restype = ctypes.c_int
libc.chdir.argtypes = [ctypes.c_char_p];  libc.chdir.restype = ctypes.c_int


def krw():
    L = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
    L.jbclient_process_checkin.restype = ctypes.c_int
    L.jbclient_process_checkin(ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_char_p()),
                              ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_bool()))
    L.jbclient_initialize_primitives.restype = ctypes.c_int
    L.jbclient_initialize_primitives()
    for n, rt, at in (("kread64", ctypes.c_uint64, [ctypes.c_uint64]),
                      ("kread32", ctypes.c_uint32, [ctypes.c_uint64]),
                      ("kwrite32", ctypes.c_int, [ctypes.c_uint64, ctypes.c_uint32]),
                      ("proc_self", ctypes.c_uint64, [])):
        f = getattr(L, n); f.restype = rt; f.argtypes = at
    def strip(v):
        return (0xffff800000000000 | (v & 0x7FFFFFFFFFFF)) if (v >> 56) else v
    p = L.proc_self()
    ro = strip(L.kread64(p + 0x18))
    t = strip(L.kread64(ro + 0x8))
    return L, t


if MODE == "check":
    L, t = krw()
    print("[e2] check pid=%d task=0x%x guard=0x%02x" % (os.getpid(), t, L.kread32(t + 0x5C4)), flush=True)
    sys.exit(0)

if MODE == "reexec":
    L, t = krw()
    before = L.kread32(t + 0x5C4)
    rc = L.kwrite32(t + 0x5C4, before & ~KBITS)
    print("[e2] reexec pid=%d task=0x%x guard 0x%02x - rc%d -> 0x%02x"
          % (os.getpid(), t, before, rc, L.kread32(t + 0x5C4)), flush=True)
    os.execve(PY, [PY, SELF, "check"], {"PATH": "/var/jb/usr/bin:/usr/bin:/bin"})

if MODE == "patch":
    L, t = krw()
    before = L.kread32(t + 0x5C4)
    rc = L.kwrite32(t + 0x5C4, before & ~KBITS)
    after = L.kread32(t + 0x5C4)
    print("[e2] mode=patch pid=%d task=0x%x guard 0x%02x - rc%d -> 0x%02x"
          % (os.getpid(), t, before, rc, after), flush=True)
    if after & KBITS:
        print("[e2] WARNING: bits still set", flush=True)
else:
    print("[e2] mode=baseline pid=%d" % os.getpid(), flush=True)

env = {
    "PATH": "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
    "HOME": "/Users/root", "USER": "root", "TMPDIR": "/tmp", "SHELL": "/bin/bash",
}
if os.path.exists(ROOT + "/usr/local/lib/libmachook.dylib"):
    env["DYLD_INSERT_LIBRARIES"] = "/usr/local/lib/libmachook.dylib"

rc1 = libc.chroot(ROOT.encode())
rc2 = libc.chdir(b"/")
print("[e2] chroot rc=%d chdir rc=%d" % (rc1, rc2), flush=True)
if rc1 != 0 or rc2 != 0:
    sys.exit(3)
os.execve("/bin/echo", ["/bin/echo", "HI"], env)
