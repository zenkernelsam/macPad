#!/usr/bin/env python3
# Find the kernel slide by matching a known function's prologue bytes.
#   needle = first 16 bytes of vm_shared_region_create (IDB 0xfffffe0008060fd0)
# The project's kfind_slide.py stepped 0x200000, which cannot see a slide like
# 0x15948000 (0x15948000 % 0x200000 != 0), hence its "no hit".
import ctypes

L = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
L.jbclient_process_checkin.restype = ctypes.c_int
L.jbclient_process_checkin(ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_char_p()),
                          ctypes.byref(ctypes.c_char_p()), ctypes.byref(ctypes.c_bool()))
L.jbclient_initialize_primitives.restype = ctypes.c_int
L.jbclient_initialize_primitives()
L.kread32.restype = ctypes.c_uint32; L.kread32.argtypes = [ctypes.c_uint64]
K32 = L.kread32

IDB_FUNC = 0xFFFFFE0008060FD0                     # vm_shared_region_create
NEEDLE = [0xd503237f, 0xd10303ff, 0x6d0523e9, 0xa9066ffc]
IDB_GUARD = 0xFFFFFE000A9FABE0                    # task_exc_guard_default

hit = None
for slide in range(0, 0x30000000, 0x1000):
    a = IDB_FUNC + slide
    if K32(a) == NEEDLE[0] and K32(a + 4) == NEEDLE[1] and \
       K32(a + 8) == NEEDLE[2] and K32(a + 12) == NEEDLE[3]:
        hit = slide
        break

if hit is None:
    print("NO_SLIDE_HIT")
else:
    print("SLIDE = 0x%x" % hit)
    print("  vm_shared_region_create rt = 0x%x" % (IDB_FUNC + hit))
    g = IDB_GUARD + hit
    v = K32(g)
    print("  task_exc_guard_default  rt = 0x%x  value = 0x%08x  (low=0x%02x, third=0x%02x)"
          % (g, v, v & 0xFF, (v >> 8) & 0xFF))
    print("PLATFORM_GUARD=0x%02x" % (v & 0xFF))
