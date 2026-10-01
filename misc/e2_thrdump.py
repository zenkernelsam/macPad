#!/usr/bin/env python3
# Dump a hung process's threads via KRW (no lldb needed).
# Uses libjailbreak's proc_find (no hard-coded kernel slide) and scans the
# task for queue heads, then flags code-looking qwords inside each thread.
import ctypes, sys

RES = "S=(sshpass -p cisco ssh -o StrictHostKeyChecking=no -o ConnectTimeout=15 -p 2222 root@192.168.64.1); ${S[@]} ''"
PID = int(sys.argv[1]) if len(sys.argv) > 1 else 0

jb = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
jb.jbclient_process_checkin.restype = ctypes.c_int
jb.jbclient_process_checkin(ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_char_p()),
                            ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_bool()))
jb.jbclient_initialize_primitives.restype = ctypes.c_int
jb.jbclient_initialize_primitives()
for n, rt, at in (("kread64", ctypes.c_uint64, [ctypes.c_uint64]),
                  ("kread32", ctypes.c_uint32, [ctypes.c_uint64]),
                  ("proc_find", ctypes.c_uint64, [ctypes.c_int])):
    f = getattr(jb, n); f.restype = rt; f.argtypes = at
K64, K32, PF = jb.kread64, jb.kread32, jb.proc_find


def unpac(p):
    v = p & 0x3FFFFFFFFFF
    if v & 0x20000000000:
        v |= 0xFFFFFC0000000000
    return v


def is_kva(v):
    return 0xFFFFFE0000000000 <= v <= 0xFFFFFE7FFFFFFFFF


def is_code(v):
    return 0x100000000 <= v <= 0x1FFFFFFFFF or 0xFFFFFE0007000000 <= v <= 0xFFFFFE00B0000000


p = PF(PID)
if not p:
    print("proc_find(%d) = 0" % PID); sys.exit(1)
ro = unpac(K64(p + 0x18))
task = unpac(K64(ro + 0x8))
print("pid=%d proc=%#x ro=%#x task=%#x" % (PID, p, ro, task))
print("task+0x590 (threads head ptr) = %#x" % K64(task + 0x590))

# threads queue: walk the doubly-linked list anchored at task+0x590
head = task + 0x590
cur = K64(head)
seen = 0
while cur and cur != head and seen < 32 and is_kva(cur):
    print("  thread[%d] @ %#x   next=%#x  prev=%#x" % (seen, cur, K64(cur), K64(cur + 8)))
    # scan the thread struct for code-looking qwords (candidate saved PCs / LRs)
    cands = []
    for off in range(0, 0x400, 8):
        try:
            v = K64(cur + off)
        except Exception:
            break
        if is_code(v):
            cands.append((off, v))
    for off, v in cands[:8]:
        print("       thread+%#04x = %#x   <-- code-like" % (off, v))
    cur = K64(cur)
    seen += 1
print("threads seen:", seen)
