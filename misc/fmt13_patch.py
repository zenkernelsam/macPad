#!/usr/bin/env python3
# fmt13_patch.py — install DYLD_CHAINED_PTR_ARM64E_SHARED_CACHE (=13) support
# into the running xnu-8792 kernel's dyld_pager fixup dispatch.
#
# What it does (all addresses are IDB/static VAs; slide added at runtime):
#   1. Overwrites the dead case-2/case-3/case-6 inline fixup region inside
#      dyld_pager_data_request (0xfffffe0008066848..0x8066940) with the
#      format-13 handler assembled from misc/fmt13_cave.s.
#   2. Redirects the bounds-check B.HI at 0xfffffe00080667f4 from the
#      default/SLIDE_ERROR block (0x8066670) to the cave (0x8066848).
#   3. Repoints jump-table slots for formats 2,3,6 to the default block
#      (0xfffffe64) so dead formats keep the original failure semantics.
#
# The cave implements upstream xnu-12377 fixupCachePageAuth64() semantics:
#   rebase:  *p = image_address + (v & 0x3FFFFFFFF) + high8(v[34:41]<<56)
#   auth:    *p = ppl_sign(image_address + (v & 0x3FFFFFFFF), uVA,
#                         diversity, key=asda|asia)
# Failures branch to the original default block (SLIDE_ERROR triage +
# KERN_FAILURE) exactly like an unknown format.
#
# Caveat: formats 2/3/6 (DYLD_CHAINED_PTR_64/_32/_64_OFFSET) are dead on
# arm64e shared-cache fixups; after this patch they take the SLIDE_ERROR
# path instead of their (unreachable on this platform) native handlers.
#
# Usage on device:  python3 /var/mobile/fmt13_patch.py [--undo]
import ctypes, sys

jb  = ctypes.CDLL("/var/jb/basebin/libjailbreak.dylib")
xpc = ctypes.CDLL("/usr/lib/system/libxpc.dylib")
jb.jbclient_initialize_primitives()
k64=jb.kread64;k64.restype=ctypes.c_uint64;k64.argtypes=[ctypes.c_uint64]
k32=jb.kread32;k32.restype=ctypes.c_uint32;k32.argtypes=[ctypes.c_uint64]
w64=jb.kwrite64;w64.restype=ctypes.c_int;w64.argtypes=[ctypes.c_uint64,ctypes.c_uint64]
w32=jb.kwrite32;w32.restype=ctypes.c_int;w32.argtypes=[ctypes.c_uint64,ctypes.c_uint32]

jb.jbinfo_get_serialized.restype=ctypes.c_void_p
d=jb.jbinfo_get_serialized()
xpc.xpc_dictionary_get_uint64.restype=ctypes.c_uint64
xpc.xpc_dictionary_get_uint64.argtypes=[ctypes.c_void_p,ctypes.c_char_p]
slide=xpc.xpc_dictionary_get_uint64(d,b"kernelConstant.slide")
def rt(a): return a+slide
def R32(a): return k32(rt(a))
def W32(a,v): return w32(rt(a),v)
print(f"slide={slide:#x}")

# ---- static VAs ----
BHI_SITE   = 0xfffffe00080667f4   # B.HI default (bounds check fmt-1 <= 0xB)
CAVE_VA    = 0xfffffe0008066848   # dead case-2 stub .. 0x80669f0 window
CAVE_END   = 0xfffffe00080669f0
RET_OK     = 0xfffffe0008066694   # LABEL_68: paging_end + UPL completion
RET_DFL    = 0xfffffe0008066670   # default block: SLIDE_ERROR triage
PPL_SIGN   = 0xfffffe0007f2bff4   # ppl dispatch op 0x3c (signPointer)
JPT2       = 0xfffffe0008066c14   # jumptable fmt2 entry
JPT3       = 0xfffffe0008066c18   # jumptable fmt3 entry
JPT6       = 0xfffffe0008066c24   # jumptable fmt6 entry
DFLT_ENT   = 0xfffffe64           # jumptable value -> 0x8066670

# ---- cave bytes (misc/fmt13_cave.s, clang -arch arm64) ----
# offset 0xc4 = BL PPL_SIGN, 0xe8 = B RET_OK, 0xf4 = B RET_DFL (fixed below)
CAVE = bytes.fromhex(
    "7100355f 54000781 a9bd53f3 a9015bf5 "
    "aa0003f3 f94037f4 aa0803f5 f9401056 "
    "110004ad 8b2d448d 910059ad eb0d013f "
    "540005e3 8b25448d 79402dad 529fffee "
    "6b0e01bf 540004c0 8b2d228e eb0e029f "
    "540004e8 910021cd eb1501bf 54000488 "
    "f94001cb d374f970 b7f8010b 9240856d "
    "8b1601ad d36aa571 92481e31 8b0d022d "
    "f90001cd 14000014 cb1401ca 8b13014a "
    "d362c56c b3503d8a f24e017f 9a8c1142 "
    "d373cd61 d37ff821 9240856a 8b16014a "
    "b400010a a9bf43ee aa0a03e0 f9405ae3 "
    "b4000063 94000000 aa0003ea a8c143ee "
    "f90001ca 8b100dce b5fffbb0 a9415bf5 "
    "a8c353f3 52800019 14000000 a9415bf5 "
    "a8c353f3 14000000")
assert len(CAVE) == 0xf8
assert CAVE_VA + len(CAVE) <= CAVE_END

def b_imm(frm, to):   return 0x14000000 | (((to-frm)>>2) & 0x3ffffff)
def bl_imm(frm, to):  return 0x94000000 | (((to-frm)>>2) & 0x3ffffff)
def bhi_imm(frm, to): return 0x54000008 | (((to-frm)>>2 & 0x7ffff) << 5)

# ---- expected originals (verify-before-write, per AGENTS.md) ----
EXPECT = {
    BHI_SITE: 0x54fff3e8,   # B.HI 0x8066670
    CAVE_VA : 0xf9401fe0,   # LDR X0,[SP,#0x38]  (case-2 stub head)
    JPT2    : 0x3c,
    JPT3    : 0x54,
    JPT6    : 0x104,
}
# patched-state expectations for --verify / idempotency
CAVE0_WORD = 0x7100355f   # CMP W10,#0xd

undo = "--undo" in sys.argv

if undo:
    ok = True
    if R32(CAVE_VA) != CAVE0_WORD:
        print("cave not installed (first word mismatch) — nothing to undo")
        sys.exit(0)
    # restore original case-2/3/6 stub+body region? we don't keep a full
    # backup here — originals live in the IDB; this restores the dispatch
    # points only (cave bytes become dead code again).
    W32(BHI_SITE, 0x54fff3e8)
    W32(JPT2, 0x3c); W32(JPT3, 0x54); W32(JPT6, 0x104)
    print("dispatch restored (cave left as dead code)")
    sys.exit(0)

print("== verify ==")
bad = False
for a, exp in EXPECT.items():
    got = R32(a)
    st = "ok" if got == exp else "MISMATCH"
    print(f"  {a:#x}: got {got:#010x} want {exp:#010x} {st}")
    if got != exp: bad = True
if bad:
    print("ABORT: unexpected kernel bytes — check slide/kernel version")
    sys.exit(1)

# ---- build cave with fixed-up external branches ----
cave = bytearray(CAVE)
def put(off, word):
    cave[off:off+4] = word.to_bytes(4, "little")
put(0xc4, bl_imm(CAVE_VA + 0xc4, PPL_SIGN))
put(0xe8, b_imm (CAVE_VA + 0xe8, RET_OK))
put(0xf4, b_imm (CAVE_VA + 0xf4, RET_DFL))

print("== write cave ==")
for i in range(0, len(cave), 4):
    W32(CAVE_VA + i, int.from_bytes(cave[i:i+4], "little"))
    got = R32(CAVE_VA + i)
    want = int.from_bytes(cave[i:i+4], "little")
    if got != want:
        print(f"  write fail @{CAVE_VA+i:#x}: {got:#x} != {want:#x}")
        sys.exit(2)
print(f"  {len(cave)} bytes written + verified")

print("== patch dispatch ==")
W32(BHI_SITE, bhi_imm(BHI_SITE, CAVE_VA))     # B.HI -> cave
print(f"  B.HI {BHI_SITE:#x} -> {R32(BHI_SITE):#010x} (want {bhi_imm(BHI_SITE,CAVE_VA):#010x})")
W32(JPT2, DFLT_ENT); W32(JPT3, DFLT_ENT); W32(JPT6, DFLT_ENT)
print(f"  jpt[2,3,6] -> {R32(JPT2):#x} {R32(JPT3):#x} {R32(JPT6):#x}")
print("done — fmt13 pages now route to the cave handler")
