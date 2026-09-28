#!/usr/bin/env python3
"""Build a patched macOS 15.6.1 arm64e dyld thin slice from the pristine baseline.

Usage:
  python3 build_dyld.py <out_name> [patch_key ...]
  python3 build_dyld.py list
If no keys given, uses the DEFAULT set.

Patches operate on the arm64e thin slice (analysis/dyld_15.6.1_arm64e_thin).
Every encoding here is byte-verified against IDA Instance1.
"""
import sys, os, struct


def _mkmark(ch, site, cave, replay):
    """Register-safe marker: saves x0,x1,x2,x8,x9,x13,x14,x15,x16, writes
    '<ch>\\n' + 6 NUL to fd2, restores, replays <replay> insn word, branches
    back to site+4. Returns (entry_bytes, cave_bytes)."""
    mark = ord(ch) | (0x0a << 8)
    entry = struct.pack('<I', 0x14000000 | ((cave - site) // 4) & 0x3ffffff)
    insns = [
        0xd10143ff,                      # sub sp,#0x50
        0xa90007e0,                      # stp x0,x1,[sp]
        0xa90123e2,                      # stp x2,x8,[sp,#0x10]
        0xa9023bed,                      # stp x13,x14,[sp,#0x20]
        0xa90343ef,                      # stp x15,x16,[sp,#0x30]
        0x52800008 | ((mark & 0xffff) << 5),  # movz w8,#mark
        0xb90403e8,                      # str w8,[sp,#0x40]
        0x910103e1,                      # add x1,sp,#0x40
        0xd2800040,                      # mov x0,#2
        0xd2800082,                      # mov x2,#8
        0xd2800090,                      # mov x16,#4
        0xd4001001,                      # svc #0x80
        0xa94007e0,                      # ldp x0,x1,[sp]
        0xa94123e2,                      # ldp x2,x8,[sp,#0x10]
        0xa9423bed,                      # ldp x13,x14,[sp,#0x20]
        0xa94343ef,                      # ldp x15,x16,[sp,#0x30]
        0x910143ff,                      # add sp,#0x50
        replay,
        0x14000000 | (((site + 4 - (cave + 0x48)) // 4) & 0x3ffffff),
    ]
    return entry, b''.join(struct.pack('<I', w) for w in insns)


def _le(hexstr):
    """Disassembler word-order hex (e.g. 'd10043ff') -> little-endian file bytes.
    Single-insn P entries already store LE bytes; ONLY the multi-insn cave bodies
    were mistakenly written in word-order (caused SIGILL 132) -- wrap them with _le."""
    h = hexstr.replace(" ", "")
    out = bytearray()
    for i in range(0, len(h), 8):
        out += struct.pack('<I', int(h[i:i + 8], 16))
    return bytes(out)


ROOT = os.path.dirname(os.path.abspath(__file__))
# analysis/dyldwork -> analysis
ANALYSIS = os.path.dirname(ROOT)
PRISTINE = os.path.join(ANALYSIS, "dyld_15.6.1_arm64e_thin")

# name -> (offset, bytes, comment)
P = {
    # crossarch trap stub: svc #0x80 -> mov x0,xzr; ret. Missing => every exec SIGSYS(140).
    "crossarch":   (0x76270, bytes.fromhex("e0031faa"), "svc->mov x0,xzr (crossarch trap)"),
    # plataccept: validPlatform() — accept iOS dyld cache on macOS process.
    # NOP the `B.NE -> altPlatform-check` at 0x35c24 so header.platform is
    # never compared; iOS cache then only needs simulator==0 (it has).
    "plataccept":  (0x35c24, bytes.fromhex("1f2003d5"), "nop B.NE @validPlatform (accept iOS cache)"),
    # imgplat: per-image platform gate in JustInTimeLoader::makeJustInTimeLoaderDyldCache
    #   (thin 0x2b580). Source: `if(!cacheMH->loadableIntoProcess(process.platform,...)){
    #   diag.error("wrong platform to load into process"); }`. NOP the `TBZ W0,#0,error`
    #   so an image found in the dyld cache is accepted regardless of its platform
    #   (iOS cache libSystem/libdyld into a macOS chroot process). Surgical: only the
    #   cache-image call site; disk slice-selection platform checks stay intact.
    "imgplat":     (0x2b580, bytes.fromhex("1f2003d5"), "nop TBZ @cache-image platform gate"),
    # norootdir: suppress PathOverrides rootdir candidates so a protected
    #   libSystem path resolves to the mapped cache (not "on disk" override).
    #   0x9e20 CBNZ X8,loc_A100 -> NOP (macOS-platform path);
    #   0x9f54 CBZ X9,loc_A1B4 -> B loc_A1B4 (iOS/catalyst path).
    "norootdir":   (0x9e20, bytes.fromhex("1f2003d5"), "NOP CBNZ rootdir gate (mac path)"),
    "norootdir2":  (0x9f54, bytes.fromhex("98000014"), "B loc_A1B4 skip rootdir (ios path)"),
    # dyldcompat: accept iOS libdyld helper version (CMP X0,#6 -> NOP the B.LS
    #   reject at 0x7a68 so version<=6 still proceeds).
    "dyldcompat":  (0x7a68, bytes.fromhex("1f2003d5"), "NOP B.LS libdyld compat gate"),
    # flatfb: on 2-level targeted miss (hasExportedSymbol==0 @0x23c68 ->
    #   fail path at 0x23c90), fall back to the flat-namespace all-loaders
    #   search (a4==-2 entry at 0x23b24). Lets DYLD_INSERT_LIBRARIES gap
    #   dylibs satisfy symbols the target image lacks (e.g. malloc_type_*
    #   missing in iOS 16.3 libSystem for macOS binaries).
    "flatfb":      (0x23c90, bytes.fromhex("a5ffff17"), "B loc_23B24 flat search on miss"),
    "dyldcompat2": (0x7a24, bytes.fromhex("1f2003d5"), "NOP B.NE __helper size check"),
    "dyldcompat3": (0x7a60, bytes.fromhex("1f2003d5"), "NOP B.NE __dyld_apis size check"),
    # segorder: dsc-extracted cache dylibs keep non-monotonic VAs (__DATA at
    #   0x1ec.. after __AUTH at 0x1ee..); accept any VA order so extracted
    #   15.6.1 dylibs load from disk.
    "segorder":    (0x7b7d4, bytes.fromhex("09000014"), "B loc_7B7F8 always (accept VA order)"),
    # cachegate: force ProcessConfig+0x208/0x230 = 0 so libSystem resolves from
    #   the mapped dyld cache even when DYLD_* path-override envs are present.
    #   @0x9418 CBZ W0,loc_9428 -> B loc_9428 (skip both STRB).
    "cachegate":   (0x9418, bytes.fromhex("04000014"), "B past STRB @0x9420/24 (cache libSystem)"),
    # hasExistingDyldCache -> return 0 : avoids the dynamicRegion() deref on a populated region.
    "hasexisting": (0x30140, bytes.fromhex("00008052c0035fd6"), "hasExistingDyldCache(){ret 0}"),
    # pre-reuse call (BL reuseExistingCache @0x34298) -> movz w0,#0 : force syscall map path.
    "prereuse":    (0x34298, bytes.fromhex("00008052"), "pre-reuse -> 0 (force map path)"),
    # files_count: ldr w28,[x19,#0x1a8] -> movz w28,#1 : submit main cache only.
    "filescount1": (0x3538c, bytes.fromhex("3c008052"), "files_count=1 (main only)"),
    # dynregion submit VA: ldr x9,[sp,#0x1ec0] -> movz x9,#0x7800,lsl#16 (+add x11 @0x35fd8 kept)
    #   => regionBase(0x180000000)+0x78000000 = 0x1f8000000 (inside 4GB region gap).
    "dynoff":      (0x35fc8, bytes.fromhex("0900afd2"), "dynregion VA offset -> 0x78000000"),
    "dynoff0":     (0x35fc8, bytes.fromhex("090080d2"), "dynregion VA offset -> 0 (offset-0 probe)"),
    # dynfix270: absolute dyn-region VA = 0x270000000 (inside iOS 4GB shared region
    #   [0x180000000,0x280000000), above macOS main tail 0x22560C000). movz x9,#0x27,lsl#32.
    "dynfix270":   (0x35fc8, bytes.fromhex("e904c0d2"), "dynregion VA -> 0x270000000 abs"),
    "accessor0":   (0x50dfc, bytes.fromhex("080080d2"), "dynamicRegion() off -> 0"),
    # dynamicRegion() accessor: ldr x8,[x0,#0x1f0] -> movz x8,#0x7800,lsl#16 (keep consistent).
    "accessor":    (0x50dfc, bytes.fromhex("0800afd2"), "dynamicRegion() off -> 0x78000000"),
    # preflightCacheFile in-chroot fcntl(F_ADDFILESIGS_RETURN) always EPERM; host cachereg
    #   already attached the blob, so dyld's redundant attach checks are bypassable.
    "fcntl_nop":   (0x35d70, bytes.fromhex("1f2003d5"), "nop B.EQ (ignore fcntl==-1)"),
    "cover_b":     (0x35d80, bytes.fromhex("07000014"), "B.CS -> B (skip coverage check)"),
    # DIAGNOSTIC: mach_o::Header::loadableIntoProcess -> true. Process platform is
    #   iOS (kernel-set), so macOS dylibs get "wrong platform to load into process".
    "platstub":    (0x7e970, _le("20008052c0035fd6"), "DIAG loadableIntoProcess(){ret 1}"),
    # DIAGNOSTIC probe: exit(syscall_ret & 0xff) right after __shared_region_map_and_slide_2_np
    #   at mapSplitCacheSystemWide. 134? use 0..255. Exit AFTER the syscall is safe (never 137).
    "errprobe":    (0x35698, _le("12001c00d2800030d4001001"), "exit(ret&0xff) after syscall 536"),
    # errprobeW: write(2,&x0,8) then exit(42) — full 64-bit 536 return value.
    "errprobeW":   (0x35698, _le(
        "ff8300d1"    # sub sp,#0x20
        "e00300f9"    # str x0,[sp]
        "e1030091"    # mov x1,sp
        "400080d2"    # mov x0,#2
        "020180d2"    # mov x2,#8
        "900080d2"    # mov x16,#4
        "011000d4"    # svc #0x80
        "40058052"    # mov w0,#42   (0x52800540)
        "200080d2"    # mov x16,#1
        "011000d4"    # svc #0x80
    ), "write(2,&x0,8);exit(42) after syscall 536"),
    # errprobeSP: exit((sp&0xf)|((x0&0xf)<<4)) — low nibble = SP alignment,
    #   high nibble = 536 ret low bits. Decodes alignment+success in one exit.
    # errno probe at the 536 stub's SVC return split (0x76e00 = B.CC): x0 holds
    #   either 0 (success) or the raw kernel errno. exit(x0&0xff) — no SP use.
    "errno536":    (0x76e00, _le(
        "001c0012"    # and w0,w0,#0xff
        "200080d2"    # mov x16,#1
        "011000d4"    # svc #0x80
    ), "exit(errno&0xff) @536-stub error path"),
    # probeE: @0x35698 (MOV X23,X0 after BL 536) -> exit(errno-ish ret&0xff).
    #   eats 3 instrs (MOV X23,X0 / MOV X0,X22 / BL free) — no cave, no return path.
    # probeS: @0x47c0 (__dyld_start) -> exit(0x41). If this doesn't fire,
    #   dyld never starts -> exec admission kills it, not our patch.
    # noslide: NOP the '|= VM_PROT_SLIDE' @0x35eec -> kernel skips slide pass.
    #   DIAGNOSTIC: distinguishes 'slide parse fails' vs other EINVAL branches.
    "noslide":     (0x35eec, bytes.fromhex("1f2003d5"), "NOP |=VM_PROT_SLIDE @0x35eec"),
    # fc0: MOV W28,#0 @0x3538c — files=[dynamic-anon-only], files_count=1,
    #   mappings_count=1. BISECT: isolates fd=-1 anonymous mapping path.
    "fc0":         (0x3538c, bytes.fromhex("1c008052"), "MOV W28,#0 (files=[dyn-anon only])"),
    # nodyn: ADD W9,W28,#1 -> MOV W9,W28 @0x354ec — files_count = numFiles
    #   (no +1 for dynamic). With filescount1 (W28=1): files=[main] only,
    #   kernel ignores the appended dynamic entry. BISECT: isolates real-file path.
    "nodyn":       (0x354ec, bytes.fromhex("e9031c2a"), "files_count=W28 (drop dynamic entry)"),
    # map1: LDUR W13,[X10,#-8] -> MOV W13,#1 @0x3553c — files[i].mappings_count
    #   forced to 1 (feeds both the sf entry AND the W23 total). BISECT: kernel
    #   maps ONLY mappings[0] -> distinguishes file-level setup vs per-mapping.
    "map1":        (0x3553c, bytes.fromhex("2d008052"), "MOV W13,#1 (files[i].count=1)"),
    # mapN variant: same site -> MOV W13,#4 maps only first 4 mappings.
    "map4":        (0x3553c, bytes.fromhex("4d008052"), "MOV W13,#4 (files[i].count=4)"),
    # slide0: LDR W9,[X19,#0x1780] -> MOV W9,#0 @0x3552c — files[0].sf_slide=0.
    #   kernel picks slide_amount=0 -> preferred addresses (no ASLR slide).
    #   BISECT: if map succeeds, EINVAL/EFAULT was random slide landing outside
    #   the 4GB shared-region window.
    "slide0":      (0x3552c, bytes.fromhex("29008052"), "MOV W9,#0 (files[0].sf_slide=0)"),
    # zf0: ORR W16,W16,W0 -> MOV W16,#0x407 @0x35ef0 — init_prot = VM_PROT_ZF|RWX
    #   for every copied mapping. Kernel takes anonymous vm_map_enter path at the
    #   SAME target_address (no file object). BISECT: if anon enter succeeds at
    #   submap offset 0, EFAULT is the file-object/mem_object side, not the VA.
    "zf0":         (0x35ef0, bytes.fromhex("70808052"), "MOV W16,#0x407 (init_prot=ZF|RWX)"),
    # clrslide: cave @0x970 — truly clear bit0x20 (VM_PROT_SLIDE) in BOTH
    #   sms_max_prot(+0x28) and sms_init_prot(+0x2c) of every submitted mapping,
    #   then redo MOV X3,X26 and jump back to the BL at 0x35694. Unlike `noslide`
    #   (which only NOPs the extra |=0x20), this strips SLIDE even if present in
    #   the record's own prot fields. BISECT: if errno changes, the failure is in
    #   vm_shared_region_slide/copyin(slide_info) — not the direct map enter.
    "clrslide_e":  (0x35690, bytes.fromhex("b82cff17"), "@0x35690 b 0x970 (clr SLIDE bits)"),
    "clrslide_c":  (0x970, _le(
        "aa1a03e9"    # mov x9,x26          (mappings base)
        "aa1903ea"    # mov x10,x25         (mappings_count)
        "b940292b"    # ldr w11,[x9,#0x28]  (max_prot)
        "121a796b"    # and w11,w11,#0xffffffdf
        "b900292b"    # str w11,[x9,#0x28]
        "b9402d2b"    # ldr w11,[x9,#0x2c]  (init_prot)
        "121a796b"    # and w11,w11,#0xffffffdf
        "b9002d2b"    # str w11,[x9,#0x2c]
        "9100c129"    # add x9,x9,#0x30
        "f100054a"    # subs x10,x10,#1
        "54ffffa1"    # b.ne ldr
        "aa1a03e3"    # mov x3,x26
        "1400d33d"    # b 0x35694
    ), "cave@0x970: clr SLIDE bit in all mappings; b 0x35694"),
    # shiftaddr: cave @0x970 — mappings[0].sms_address += 0x40000000 (lands at
    #   submap offset 0x40000000 instead of 0). BISECT: if enter succeeds at a
    #   nonzero submap offset, "offset 0" is specifically blocked; if EFAULT
    #   persists, the failure is deeper in vm_map_enter for this submap.
    "shiftaddr_e": (0x35690, bytes.fromhex("9e0d0014"), "@0x35690 b 0x38d08 (shift m0 addr)"),
    "shiftaddr_c": (0x38d08, _le(
        "f9400349"    # ldr x9,[x26]        (mappings[0].sms_address)
        "0800a8d2"    # mov x8,#0x40000000
        "8b080129"    # add x9,x9,x8
        "f9000349"    # str x9,[x26]
        "aa1a03e3"    # mov x3,x26
        "5ef2ff17"    # b 0x35694
    ), "cave@0x38d08: mappings[0].sms_address += 0x40000000; b 0x35694"),
    "probeS":      (0x47c0, _le(
        "28088052"    # mov w0,#0x41
        "300080d2"    # mov x16,#1  (exit)
        "011000d4"    # svc #0x80
    ), "@__dyld_start exit(0x41)"),
    "probeE":      (0x35698, _le(
        "001c0012"    # and w0,w0,#0xff
        "300080d2"    # mov x16,#1   (exit)  [FIXED: was d2800020 = mov x0,#1]
        "011000d4"    # svc #0x80
    ), "@0x35698 and w0,#0xff; exit() -> rc=536ret&0xff"),
    # errno536b: @0x35698 read global _errno(0xa92c4) set by cerror, exit(errno&0xff)
    "errno536b":   (0x35698, _le(
        "a8030090"    # adrp x8,#0xa9000  (PC-rel: 0xa9000-0x35000=0x74000)
        "00c542b9"    # ldr w0,[x8,#0x2c4]  (=&_errno)
        "001c0012"    # and w0,w0,#0xff
        "200080d2"    # mov x16,#1
        "011000d4"    # svc #0x80
    ), "exit(_errno&0xff) after 536"),
    # check_np probe at mapfn entry 0x35380 (proven-writable site): calls
    #   shared_region_check_np(&base) and writes {ret, base, magic[16]} (40B)
    #   to fd2 — reveals whether the chroot task already holds a shared region
    #   and which cache it is (iOS vs macOS magic).
    "cknpentry":   (0x35380, bytes.fromhex("c4470014"), "@0x35380 b 0x47290 (check_np probe)"),
    "cknpcave":    (0x47290, _le(
        "d10403ff"    # sub sp,#0x100
        "f9007fe0"    # str x0,[sp,#0xf8]   (save arg)
        "910003e0"    # mov x0,sp           (&out)
        "d28024d0"    # mov x16,#294        (__shared_region_check_np)
        "d4001001"    # svc #0x80
        "f9000be0"    # str x0,[sp,#0x10]   (ret)
        "f94003e9"    # ldr x9,[sp]         (base)
        "f9000fe9"    # str x9,[sp,#0x18]
        "b4000049"    # cbz x9,+8
        "a9402d2a"    # ldp x10,x11,[x9]
        "a9023dea"    # stp x10,x11,[sp,#0x20]  (magic 16B)
        "d2800040"    # mov x0,#2
        "910043e1"    # add x1,sp,#0x10
        "d2800502"    # mov x2,#0x28
        "d2800090"    # mov x16,#4
        "d4001001"    # svc #0x80           write(2,buf,40)
        "f9407fe9"    # ldr x9,[sp,#0xf8]   (restore arg)
        "910403ff"    # add sp,#0x100
        "aa0903e8"    # mov x8,x9           (replay MOV X8,X0)
        "17ffb82a"    # b 0x35384
    ), "cave@0x47290: check_np dump {ret,base,magic}; resume"),
    # APP-ENTRY marker @0x6b94 (BLRAAZ X8 -> bl cave@0x970): writes 'E\n'+x8(8B)+'\n'
    #   to fd 2 right before calling app entry. 'E' proves dyld finished ALL setup;
    #   absence => SIGSEGV inside dyld post-cache code. First insn uses SP => also
    #   an SP-alignment check (SIGILL if SP already corrupt here).
    "entrymark":   (0x6b94, struct.pack('<I', 0x14000000 | (((0x970 - 0x6b94) // 4) & 0x3ffffff)),
                    "@0x6b94 bl 0x970 (app-entry marker)"),
    "entrycave":   (0x970, _le(
        "ff0301d1"    # sub sp,sp,#0x40
        "e8a703a9"    # stp x8,x9,[sp,#0x18]
        "e00705a9"    # stp x0,x1,[sp,#0x28]
        "e20f07a9"    # stp x2,x3,[sp,#0x38]
        "aa488152"    # mov w10,#0xa45        ('E\n')
        "ea030079"    # strh w10,[sp]
        "e80700f9"    # str x8,[sp,#8]        (entry addr -> msg+8)
        "49018052"    # mov w9,#0xa
        "e9430039"    # strb w9,[sp,#0x10]
        "400080d2"    # mov x0,#2
        "e1030091"    # mov x1,sp
        "220280d2"    # mov x2,#0x11
        "900080d2"    # mov x16,#4
        "011000d4"    # svc #0x80             write(2,sp,0x11)
        "e8a743a9"    # ldp x8,x9,[sp,#0x18]
        "e00745a9"    # ldp x0,x1,[sp,#0x28]
        "e20f47a9"    # ldp x2,x3,[sp,#0x38]
        "ff030191"    # add sp,sp,#0x40
        "1f093fd6"    # blraaz x8             (replayed: call app entry)
    ), "cave@0x970: write 'E'+x8 entry addr; blraaz x8"),
    # errno probe at the SETTLED site 0x356d8 (after free+close, before reuse):
    #   reads global _errno (0xa92c4) left by cerror, exits with its low byte.
    "errnoc":      (0x356d8, _le(
        "48d03bd5"    # mrs x8,TPIDRRO_EL0
        "080540f9"    # ldr x8,[x8,#8]      (TSD errno ptr)
        "480000b4"    # cbz x8,+8
        "000140b9"    # ldr w0,[x8]         (=*errno)
        "001c0012"    # and w0,w0,#0xff
        "200080d2"    # mov x16,#1
        "011000d4"    # svc #0x80
    ), "exit(TSD-errno&0xff) @0x356d8"),
    # check_np dump at prereuse site 0x34298 (inside loadDyldCache — guaranteed
    #   valid frame): replaces the BL reuseExistingCache, calls check_np first,
    #   dumps {ret,base,magic16}, then does the patched behaviour (w0=0 -> map path).
    "cknp2entry":  (0x34298, struct.pack('<I', 0x14000000 | (((0x47290 - 0x34298) // 4) & 0x3ffffff)),
                    "@0x34298 b 0x47290 (check_np dump @ prereuse)"),
    "cknp2cave":   (0x47290, _le(
        "d10403ff"    # sub sp,#0x100
        "f9007fe0"    # str x0,[sp,#0xf8]
        "f9007be1"    # str x1,[sp,#0xf0]   (save args)
        "910003e0"    # mov x0,sp
        "d28024d0"    # mov x16,#294        (check_np)
        "d4001001"    # svc #0x80
        "f9000be0"    # str x0,[sp,#0x10]   (ret)
        "f94003e9"    # ldr x9,[sp]         (base)
        "f9000fe9"    # str x9,[sp,#0x18]
        "d2800040"    # mov x0,#2
        "910043e1"    # add x1,sp,#0x10
        "d2800502"    # mov x2,#0x28
        "d2800090"    # mov x16,#4
        "d4001001"    # svc #0x80           write(2,{ret,base},0x28) FIRST
        "b4000049"    # cbz x9,+8
        "a9402d2a"    # ldp x10,x11,[x9]    (magic — may fault, after dump)
        "a9023dea"    # stp x10,x11,[sp,#0x20]
        "d2800040"    # mov x0,#2
        "910083e1"    # add x1,sp,#0x20
        "d2800202"    # mov x2,#0x10
        "d2800090"    # mov x16,#4
        "d4001001"    # svc #0x80           write(2,magic,0x10)
        "f9407fe0"    # ldr x0,[sp,#0xf8]
        "f9407be1"    # ldr x1,[sp,#0xf0]
        "910403ff"    # add sp,#0x100
        "d2800000"    # mov w0,#0           (replayed patch: w0=0)
        "17ffb3e9"    # b 0x3429c           (continue after BL)
    ), "cave@0x47290: check_np dump @ prereuse; w0=0; b 0x3429c"),
    # exit(sp&0xff): SP low byte as return code — 0 = 16-aligned.
    "errprobeSP":  (0x35698, _le(
        "e1030011"    # mov w1,wsp      (0x110003e1)
        "200c0012"    # and w0,w1,#0xf  (0x12000c20)
        "200080d2"    # mov x16,#1
        "011000d4"    # svc #0x80
    ), "exit(sp&0xf) after syscall 536"),
    # LATER errno probe (0x35698 exit gets killed as mid-attach; exit after free+close instead).
    "errprobe2":   (0x356d8, _le("aa1703e012001c00d2800030d4001001"), "exit(x23&0xff) after syscall+free @0x356d8"),
    # File-write probe (no exit -> never 137). 0x35698 -> b 0x3576c; cave (preflight fn, already
    # ret'd by now) opens/writes x0(=536 ret, 8 bytes) to /tmp/e536 then resumes @0x3569c.
    "fwentry":     (0x35698, bytes.fromhex("35000014"), "@0x35698 b 0x3576c (file-write probe)"),
    "fwcave":      (0x3576c, _le(
        "d10083ff" "f90003e0" "f90007fe" "10000200" "5280c021" "52803482" "d28000b0" "d4001001"
        "910003e1" "d2800102" "d2800090" "d4001001" "d28000d0" "d4001001" "f94003e0" "f94007fe"
        "910083ff" "aa0003f7" "17ffffbb" "706d742f" "3335652f" "00000036"),
        "cave: open/write x0 -> /tmp/e536, resume @0x3569c"),
    # REAL dead cave (0x38d08..0x38d3b, zero xrefs): write x0 (536 ret, 8 bytes) to stderr(fd2), resume.
    "fwentry2":    (0x35698, bytes.fromhex("9c0d0014"), "@0x35698 b 0x38d08"),
    "fwcave2":     (0x38d08, _le(
        "d10043ff" "f90003e0" "910003e1" "d2800102" "d2800040" "d2800090" "d4001001"
        "f94003e0" "910043ff" "aa0003f7" "17fff25b"),
        "cave: write(2,x0,8); resume @0x3569c"),
    # SANITY: does the cave run at all? write constant "ABCD" to stderr.
    "tcentry":     (0x35698, bytes.fromhex("9c0d0014"), "@0x35698 b 0x38d08"),
    "tccave":      (0x38d08, _le(
        "d10043ff" "52884828" "72a88868" "f90003e8" "910003e1" "d2800102" "d2800040"
        "d2800090" "d4001001" "910043ff" "aa0003f7" "17fff25a"),
        "cave: write(2,'ABCD',8); resume @0x3569c"),
    # errprobe3: enter at the COMMON EXIT 0x35714 (attach settled; dyld console writes here),
    # write w23 (536 ret) to stderr, then redo LDR X8,[X19,#0x18] and continue @0x35718.
    "ep3entry":    (0x35714, bytes.fromhex("7d0d0014"), "@0x35714 b 0x38d08"),
    "ep3cave":     (0x38d08, _le(
        "d10043ff" "b90003f7" "910003e1" "d2800102" "d2800040" "d2800090" "d4001001"
        "910043ff" "f9400e68" "17fff27b"),
        "cave: write(2,&w23,8); ldr x8,[x19,#0x18]; b 0x35718"),
    # Errno probe: hook the 536 stub's FAIL path at 0x76e04 (x0=raw kernel errno
    # before cerror). Cave writes x0 to stderr, returns -1 to caller @0x35698.
    "e5entry":     (0x76e04, bytes.fromhex("c107ff17"), "@0x76e04 b 0x38d08 (errno probe on 536 fail)"),
    "e5cave":      (0x38d08, _le(
        "d10083ff" "f90003e0" "910003e1" "d2800102" "d2800040" "d2800090" "d4001001"
        "12800020" "910083ff" "d65f03c0"),
        "cave: write(2,errno@sp,8); w0=-1; ret -> 0x35698"),
    # Errno probe + SPIN: write errno then b . (child stays alive for vmmap walk).
    "e5spin":      (0x38d08, _le(
        "d10083ff" "f90003e0" "910003e1" "d2800102" "d2800040" "d2800090" "d4001001"
        "910083ff" "14000000"),
        "cave: write(2,errno,8); b . (spin for vmmap)"),
    # Slide-mask cave (lifted verbatim from dyld_noslide.bin): entry @0x35690 ->
    #   cave iterates all mapping entries (0x30 stride) clearing bit0x20
    #   (VM_PROT_SLIDE) in initProt/maxProt @+0x28/+0x2c, then redoes MOV X3,X26
    #   and returns to 0x35694 (the BL __shared_region_map_and_slide_2_np).
    "slidentry":   (0x35690, bytes.fromhex("9e0d0014"), "@0x35690 b 0x38d08 (slide-mask cave)"),
    "slidecave":   (0x38d08, bytes.fromhex(
        "3f0800b9e9031aaaea0319aa2b2940b96b791a122b2900b92b2d40b96b791a122b2d00b9"
        "29c100914a0500f101ffff54e3031aaa56f2ff17"),
        "cave: for each mapping clear VM_PROT_SLIDE; resume @0x35694"),
    # Slide-mask v2 at __text padding 0x970: zeroes sf_slide (+0x8) on EVERY
    #   shared_file_np entry (12B stride, count=w28) — not just files[0] — then
    #   masks VM_PROT_SLIDE on all mappings, restores MOV X3,X26, back to 0x35694.
    #   Why: engine rejects non-16K-aligned slide on ANY file (vm_shared_region.c
    #   ~L1523) — .01 subcache slide was still being submitted before.
    # stack-free slide-mask (same loop logic, PRE! written from a static
    #   buffer at 0xa38+16 in the cave — reaching 0x35690 with a corrupted SP
    #   must not fault: zero stack use throughout).
    "slidentry2":  (0x35690, bytes.fromhex("b82cff17"), "@0x35690 b 0x970 (slide-mask v2)"),
    "slidecave2":  (0x970, _le(
        # stack-free PRE! FIRST: write(2, marker@cave-end, 4) — proves cave reached
        #   even when the mask loops later fault on corrupted pointers.
        "aa0103ef"    # mov x15,x1   (preserve files[] ptr across the write)
        "10000341"    # adr x1, +0x68  (marker buf at end)
        "d2800082"    # mov x2,#4
        "d2800040"    # mov x0,#2
        "d2800090"    # mov x16,#4
        "d4001001"    # svc #0x80
        # loopA: zero sf_slide(+8) on each 12B shared_file_np rec, count=w28
        "aa0f03e9" "aa1c03ea" "340000aa" "b900093f" "91003129" "7100054a" "543fffa1"
        # loopB: clr bit0x20 in init/maxProt(+0x28/+0x2c) of each 0x30 mapping, count=x25
        "aa1a03e9" "aa1903ea" "b940292b" "121a796b" "b900292b" "b9402d2b" "121a796b"
        "b9002d2b" "9100c129" "f100054a" "543fff01" "aa1a03e3"
        "1400d330"    # b 0x35694
        "21455250"    # 'PRE!' marker data (LE 'P','R','E','!')
    ), "cave@0x970 stack-free: write PRE!; zero sf_slide + clr SLIDE; b 0x35694"),
    # DIAGNOSTIC: dyld-entry probe at __dyld_start (0x47c0). Cave writes 'STRT' to
    # fd2, replays MOV X0,SP, resumes @0x47c4. Header dead zone 0xa00.
    "stentry":     (0x47c0, bytes.fromhex("90f0ff17"), "@__dyld_start b 0xa00 (entry probe)"),
    "stcave":      (0xa00, _le(
        "d10083ff" "528a8a69" "72a8a449" "f90003e9" "e1030091" "d2800082" "d2800040"
        "d2800090" "d4001001" "910083ff" "aa1f03e0" "14000f66"),
        "cave@0xa00: write(2,'STRT',4); mov x0,sp; b 0x47c4"),
    # empty variant: replay only, no write — isolates whether write() is the problem
    "stcave0":     (0xa00, _le("aa1f03e0" "14000f70"),
        "cave@0xa00: mov x0,sp; b 0x47c4 (no write)"),
    # 536-return probe at SECOND dead region 0x3b394 (60B): write raw x0 to fd2,
    #   restore x0/x23, resume @0x3569c. Use when 0x38d08 is occupied by slidecave.
    "retentry":    (0x35698, bytes.fromhex("3f170014"), "@0x35698 b 0x3b394 (ret probe)"),
    "retcave":     (0x3b394, _le(
        "d10043ff" "f90003e0" "910003e1" "d2800102" "d2800040" "d2800090" "d4001001"
        "f94003e0" "910043ff" "aa0003f7" "17ffe8b8"),
        "cave2: write(2,x0,8); resume @0x3569c"),
    # errno probe variant: stub-fail path @0x76e04 -> cave @0x3b394 (x0=raw errno).
    #   write(2,&x0,8); w0=-1; ret -> lands at caller 0x35698 continuing as -1.
    "e5bentry":    (0x76e04, bytes.fromhex("6409ff17"), "@0x76e04 b 0x3b394 (errno probe)"),
    "e5bcave":     (0x3b394, _le(
        "d10083ff" "f90003e0" "910003e1" "d2800102" "d2800040" "d2800090" "d4001001"
        "12800020" "910083ff" "d65f03c0"),
        "cave2: write(2,errno,8); w0=-1; ret"),
    # errno probe at THIRD dead region 0x47290 (56B): combinable with retcave.
    #   stub-fail @0x76e04 -> cave: write(2,&x0,8); w0=-1; ret (LR=0x35698).
    "e5centry":    (0x76e04, bytes.fromhex("2341ff17"), "@0x76e04 b 0x47290 (errno probe)"),
    "e5ccave":     (0x47290, _le(
        "d10083ff" "f90003e0" "910003e1" "d2800102" "d2800040" "d2800090" "d4001001"
        "12800020" "910083ff" "d65f03c0"),
        "cave3: write(2,errno,8); w0=-1; ret"),
    # Breadcrumb probes: each writes a 1-char marker+'\n' to fd2, replays the
    #   patched insn, branches back. All live in header dead space 0xa40..0xb00.
    #   'A' = start() entry 0x53dc (PACIBSP)            -> cave 0xa40
    #   'B' = mapSplitCacheSystemWide 0x35380 (MOV X8,X0)-> cave 0xa80
    #   'C' = files-loop join 0x355fc (MOV X8,#0)        -> cave 0xac0
    "mkAentry":    (0x53dc, bytes.fromhex("d9feff17"), "@0x53dc b 0xa40 (mark A=start)"),
    "mkAcave":     (0xa40, _le(
        "d10043ff" "52814828" "f90003e8" "910003e1" "d2800040" "d2800102" "d2800090"
        "d4001001" "910043ff" "d503237f" "1400125e"),
        "cave@0xa40: write 'A'; PACIBSP; b 0x53e0"),
    "mkBentry":    (0x35380, bytes.fromhex("c02dff17"), "@0x35380 b 0xa80 (mark B=mapfn)"),
    "mkBcave":     (0xa80, _le(
        "d10043ff" "52814848" "f90003e8" "910003e1" "d2800040" "d2800102" "d2800090"
        "d4001001" "910043ff" "aa0003e8" "1400d237"),
        "cave@0xa80: write 'B'; MOV X8,X0; b 0x35384"),
    "mkCentry":    (0x35434, bytes.fromhex("a32dff17"), "@0x35434 b 0xac0 (mark C=after preflight)"),
    "mkCcave":     (0xac0, _le(
        "d10043ff" "52814868" "f90003e8" "910003e1" "d2800040" "d2800102" "d2800090"
        "d4001001" "910043ff" "d2800000" "1400d254"),
        "cave@0xac0: write 'C'; MOV X0,#0; b 0x35438"),
    "mkDentry":    (0x355fc, bytes.fromhex("412dff17"), "@0x355fc b 0xb00 (mark D=join)"),
    # mkDlr: same site, but dumps x30 (LR) — reveals the re-entry caller when D repeats.
    "mkDlrcave":   (0xb00, _le(
        "d10043ff" "f90003fe" "910003e1" "d2800040" "d2800102" "d2800090"
        "d4001001" "f94003fe" "910043ff" "d2800008" "1400d2b6"),
        "cave@0xb00: write(2,&x30,8); MOV X8,#0; b 0x35600"),
    "mkDcave":     (0xb00, _le(
        "d10043ff" "52814888" "f90003e8" "910003e1" "d2800040" "d2800102" "d2800090"
        "d4001001" "910043ff" "d2800008" "1400d2b6"),
        "cave@0xb00: write 'D'; MOV X8,#0; b 0x35600"),
    # Glue-call probe (handover §9.3): the blraaz x8 app-entry call at 0x6b94 is
    #   redirected to a REAL dead code cave (__text NOP padding 0x38d08..0x38d3b,
    #   52B, ZERO xrefs; real function starts 0x38d40). The cave writes the two
    #   live registers {x8(glue ptr), x9([[sp+0x1d0]+8])} to stdout (16 raw bytes)
    #   then exit(0). Unlike the old contaminated probe, this cave is genuinely dead.
    "gluebranch":  (0x6b94, bytes.fromhex("5dc80014"), "blraaz x8 -> b 0x38d08 (glue probe)"),
    "glueprobe":   (0x38d08, _le("d10083ffa90027e8910003e1d2800020d2800202d2800090d4001001d2800000d2800030d4001001"), "cave: write(1,{x8,x9},16); exit(0)"),
}

# Generated register-safe breadcrumb markers (see _mkmark):
#   'E' @0x3561c outer mappings-loop head (once per cache file)
#   'F' @0x3562c inner copy-loop head (once per mapping — floods if runaway)
#   'G' @0x35668 after DynamicRegion::size() call
for _ch, _site, _cave, _rep, _tag in (
    ("E", 0x3561c, 0xb40, 0x9b0a250e, "mappings outer loop"),
    ("F", 0x3562c, 0xc00, 0x9bab69b0, "mapping copy inner loop"),
    ("G", 0x35668, 0xc80, 0x52800608, "after DynamicRegion::size()"),
):
    _e, _c = _mkmark(_ch, _site, _cave, _rep)
    P[f"mk{_ch}entry"] = (_site, _e, f"mark {_ch} @0x{_site:x} ({_tag})")
    P[f"mk{_ch}cave"] = (_cave, _c, f"cave@0x{_cave:x} mark {_ch}")

# m2* post-536 path markers: map the return/free/close/reuse/ret flow.
#   Output floods to a FILE are fine (never block); char stream = control flow.
#   'p' @0x35698 syscall-ret landing     'f' @0x356a8 close-loop entry
#   'l' @0x356b4 close-loop head         'c' @0x356d4 pre-BL reuse
#   'r' @0x356e4 reuse-fail w23!=0       'v' @0x356f8 reuse-fail w23==0
#   'S' @0x35710 success ret             'R' @0x35714 common exit (SP restore)
#   'E' @0x35754 error-string path       'u' @0x351a8 reuseExistingCache entry
#   'F' @0x51258 DynamicRegion::free entry
for _ch, _site, _cave, _rep, _tag in (
    ("p", 0x35698, 0xaf0, 0xaa0003f7, "post-536 landing (MOV X23,X0)"),
    ("f", 0x356a8, 0xb40, 0x91400668, "close-loop entry"),
    ("l", 0x356b4, 0xb90, 0xb94002c0, "close-loop head (LDR fd)"),
    ("c", 0x356d4, 0xbe0, 0xaa1403e1, "pre-BL reuse (MOV X1,X20)"),
    ("r", 0x356e4, 0xc30, 0xf9400a88, "reuse-fail w23!=0 (LDR errstr)"),
    ("v", 0x356f8, 0xc80, 0x39401aa8, "reuse-fail w23==0 (verbose chk)"),
    ("S", 0x35710, 0xcd0, 0x52800020, "success ret w0=1"),
    ("R", 0x35714, 0xd20, 0xf9400e68, "common exit (LDR saved SP)"),
    ("E", 0x35754, 0xd70, 0x52800000, "error-string path"),
    ("u", 0x351a8, 0xdc0, 0xd503237f, "reuseExistingCache entry (PACIBSP)"),
    ("F", 0x51258, 0xe10, 0xaa0003e1, "DynamicRegion::free entry"),
):
    _e, _c = _mkmark(_ch, _site, _cave, _rep)
    P[f"m2{_ch}entry"] = (_site, _e, f"m2 {_ch} @0x{_site:x} ({_tag})")
    P[f"m2{_ch}cave"] = (_cave, _c, f"m2 cave@0x{_cave:x} mark {_ch}")

# m3 'a' @0x35660 dynregion-append head (re-entry detector).
# m3 'q' @0x35694: cave performs the REAL BL 536 then writes 'q'+x0 — answers
#   "is 536 called at all" (q missing) vs "called but never returns" (q count
#   vs a/p count). Entry B->cave; cave: save, bl 0x76df8, write(2,'q'+x0), b 0x35698.
P["m3aentry"] = (0x35660, struct.pack('<I', 0x14000000 | (((0xe60 - 0x35660) // 4) & 0x3ffffff)),
                 "@0x35660 b 0xe60 (m3 a = append head)")
P["m3acave"] = (0xe60, b''.join(struct.pack('<I', w) for w in [
    0xd10143ff, 0xa90007e0, 0xa90123e2, 0xa9023bed, 0xa90343ef,
    0x52814c28,             # movz w8,#'a\n' (0xa61)
    0xb90403e8, 0x910103e1, 0xd2800040, 0xd2800102, 0xd2800090, 0xd4001001,
    0xa94007e0, 0xa94123e2, 0xa9423bed, 0xa94343ef, 0x910143ff,
    0xf948ea78,             # replay LDR X24,[X19,#0x11D0]
    0x14000000 | (((0x35664 - (0xe60 + 0x48)) // 4) & 0x3ffffff),
]), "m3 cave@0xe60: write 'a'; LDR X24; b 0x35664")
# m5 debug slidecave @0x970 (with m5dentry @0x35690): dumps {w28,x25,w23,x26}
#   (32B), then does the slide-mask with BOTH loop counts CLAMPED to <=64 — if
#   the infinite burn was a garbage count, the burn disappears and 'PRE!' fires.
_m5 = [
    0xd10143ff,                       # sub sp,#0x50
    0xa90063fc,                       # stp x28,x24,[sp]
    0xa9015bf9,                       # stp x25,x23,[sp,#0x10]
    0xa90203fa,                       # stp x26,x0,[sp,#0x20]
    0x910003e1,                       # mov x1,sp
    0xd2800040,                       # mov x0,#2
    0xd2800602,                       # mov x2,#48
    0xd2800090, 0xd4001001,           # write(2,sp,32)
    0x910143ff,                       # add sp,#0x50
    # loopA: sf_slide=0, count=min(x28,64)
    0xaa0103e9,                       # mov x9,x1
    0xaa1c03ea,                       # mov x10,x28
    0xf101015f,                       # cmp x10,#64
    0x54000049,                       # b.ls +8
    0xd280080a,                       # mov x10,#64
    0xb40000aa,                       # cbz x10,+0x14 (doneA)
    0xb900093f,                       # loopA: str wzr,[x9,#8]
    0x91003129,                       # add x9,#0xc
    0xf100054a,                       # subs x10,#1
    0x54ffffa1,                       # b.ne loopA (-0xc)
    # doneA -> loopB: mask mappings, count=min(x25,64)
    0xaa1a03e9,                       # mov x9,x26
    0xaa1903ea,                       # mov x10,x25
    0xf101015f,                       # cmp x10,#64
    0x54000049,                       # b.ls +8
    0xd280080a,                       # mov x10,#64
    0xb400014a,                       # cbz x10,+0x28 (doneB)
    0xb940292b,                       # loopB: ldr w11,[x9,#0x28]
    0x121a796b,                       # and w11,~0x20
    0xb900292b,                       # str w11
    0xb9402d2b,                       # ldr w11,[x9,#0x2c]
    0x121a796b, 0xb9002d2b,
    0x9100c129,                       # add x9,#0x30
    0xf100054a,                       # subs x10,#1
    0x54ffff01,                       # b.ne loopB (-0x20)
    # doneB
    0xaa1a03e3,                       # mov x3,x26
    0xd10083ff, 0x528a4a09, 0x72a428a9, 0xf90003e9, 0x910003e1,
    0xd2800082, 0xd2800040, 0xd2800090, 0xd4001001, 0x910083ff,  # PRE! write
    # tail: b 0x35694  (branch insn at cave+len-4 = 0x970+0xb0)
    0x14000000 | (((0x35694 - (0x970 + len([0]) * 4 + 0xac)) // 4) & 0x3ffffff),
]
_m5[-1] = 0x14000000 | (((0x35694 - (0x970 + (len(_m5) - 1) * 4)) // 4) & 0x3ffffff)
P["m5dentry"] = (0x35690, struct.pack('<I', 0x14000000 | (((0x970 - 0x35690) // 4) & 0x3ffffff)),
                 "@0x35690 b 0x970 (m5 debug cave)")
P["m5dcave"] = (0x970, b''.join(struct.pack('<I', w) for w in _m5),
                "m5 cave@0x970: dump regs + clamped slide-mask + PRE! + b 0x35694")
# m6: pack-loop count dumper @0x3561c (outer loop head). Replays MADD X14,X8,X10,X9
# in-cave then dumps {i=x8, count=[x14+0x180], src=x12, x26} = 32B per outer iter.
_m6 = [
    0x9b0a250e,                       # madd x14,x8,x10,x9   (replay)
    0xd10143ff,                       # sub sp,#0x50
    0xa9002fe8,                       # stp x8,x11,[sp]      i + scratch
    0xb94181cb,                       # ldr w11,[x14,#0x180]
    0xa9013beb,                       # stp x11,x14,[sp,#0x10] count + CacheInfo*
    0xa90227fa,                       # stp x26,x9,[sp,#0x20]  x26 + CacheInfo base
    0x910003e1, 0xd2800040, 0xd2800402, 0xd2800090, 0xd4001001,  # write(2,sp,32)
    0x910143ff,                       # add sp,#0x50
    0x14000000,                       # b 0x35620 (patched below)
]
_m6[-1] = 0x14000000 | (((0x35620 - (0xaf0 + (len(_m6) - 1) * 4)) // 4) & 0x3ffffff)
P["m6entry"] = (0x3561c, struct.pack('<I', 0x14000000 | (((0xaf0 - 0x3561c) // 4) & 0x3ffffff)),
                "@0x3561c b 0xaf0 (pack count dump)")
P["m6cave"] = (0xaf0, b''.join(struct.pack('<I', w) for w in _m6),
               "m6 cave@0xaf0: dump {i,count,ci,x26,base} per outer iter, b 0x35620")
# m7: who re-entered? @0x35690 dump {x23, x30(LR), x29(FP), [x29+8](saved LR),
#   x26, x24} = 48B then b 0x35694 (no slide cave — keep frame pristine).
_m7 = [
    0xf94007af,                       # ldr x15,[x29,#8]        saved LR
    0xd10143ff,                       # sub sp,#0x50
    0xa9007bf7,                       # stp x23,x30,[sp]
    0xa9013ffd,                       # stp x29,x15,[sp,#0x10]  (fp, saved-lr)
    0xa90263fa,                       # stp x26,x24,[sp,#0x20]  (x26,x24)
    0x910003e1, 0xd2800040, 0xd2800602, 0xd2800090, 0xd4001001,
    0x910143ff,                       # add sp,#0x50
    0x14000000,                       # b 0x35694 (patched)
]
_m7[-1] = 0x14000000 | (((0x35694 - (0xb40 + (len(_m7) - 1) * 4)) // 4) & 0x3ffffff)
P["m7entry"] = (0x35690, struct.pack('<I', 0x14000000 | (((0xb40 - 0x35690) // 4) & 0x3ffffff)),
                "@0x35690 b 0xb40 (m7 who-entered dump)")
P["m7cave"] = (0xb40, b''.join(struct.pack('<I', w) for w in _m7),
               "m7 cave@0xb40: dump {x23,lr,fp,saved-lr,x26,x24}, b 0x35694")
_qoff = (0x76df8 - (0xeb0 + 0x14)) // 4
#   64 raw bytes to fd2, then falls through to slidecave2's own logic via b 0x970.
#   Proves whether the mapping/file counts are sane before the slide cave runs.
P["m4dentry"] = (0x35690, struct.pack('<I', 0x14000000 | (((0xf30 - 0x35690) // 4) & 0x3ffffff)),
                 "@0x35690 b 0xf30 (m4 reg dump)")
P["m4dcave"] = (0xf30, b''.join(struct.pack('<I', w) for w in [
    0xd10143ff,                       # sub sp,#0x50
    0xa9004df9,                       # stp x25,x19,[sp]
    0xa9012bfd,                       # stp w28?,x23 -> stp x28,x23 (w28 in low)
    0xa9020bfa,                       # stp x26,x2,[sp,#0x20]
    0xa90303e1,                       # stp x1,x0,[sp,#0x30]
    0x910003e1,                       # mov x1,sp
    0xd2800040,                       # mov x0,#2
    0xd2800082,                       # mov x2,#64
    0xd2800090, 0xd4001001,           # svc write(2,sp,64)
    0x910143ff,                       # add sp,#0x50
    0xaa1a03e3,                       # replay MOV X3,X26
    0x14000000 | (((0x970 - (0xf30 + 0x30)) // 4) & 0x3ffffff),
]), "m4 cave@0xf30: dump regs {x25,x19,x28,x23,x26,x2,x1,x0}; b 0x970 (slidecave2)")
_qoff = (0x76df8 - (0xeb0 + 0x14)) // 4
P["m3qentry"] = (0x35694, struct.pack('<I', 0x14000000 | (((0xeb0 - 0x35694) // 4) & 0x3ffffff)),
                 "@0x35694 b 0xeb0 (m3 q = BL 536 probe)")
P["m3qcave"] = (0xeb0, b''.join(struct.pack('<I', w) for w in [
    0xd10143ff, 0xa90007e0, 0xa90123e2, 0xa9023bed, 0xa90343ef,
    0x94000000 | (_qoff & 0x3ffffff),   # bl __shared_region_map_and_slide_2_np (real)
    0xf90003e0,                          # str x0,[sp]  (save ret)
    0x52814d48,                          # movz w8,#'q\n' (0xa71)
    0xb90403e8, 0x910103e1, 0xd2800040, 0xd2800102, 0xd2800090, 0xd4001001,
    0xf94003e0,                          # ldr x0,[sp]  (restore ret)
    0xa94007e0, 0xa94123e2, 0xa9423bed, 0xa94343ef, 0x910143ff,
    0x14000000 | (((0x35698 - (0xeb0 + 0x50)) // 4) & 0x3ffffff),
]), "m3 cave@0xeb0: bl 536; write 'q'; b 0x35698")


# --- preflightCacheFile bisect markers (sp-independent replay sites) ---
# _mkmark(ch, site, cave, replay_word) -> (entry, cave)
for _ch,_site,_cave,_rep,_desc in [
    ("e", 0x35380, 0x9b580, 0xaa0003e8, "mapSplit callsite mov x8,x0"),      # preflight call seq
    ("f", 0x35da8, 0x9b600, 0x52800043, "preflight: before mmap (mov w3,#2)"),
    ("m", 0x35e00, 0x9b680, 0xb901828c, "preflight: rec.count store (str w12)"),
    ("s", 0x35f9c, 0x9b700, 0x3dc03b20, "preflight tail (ldr q0 hdr+E0)"),
]:
    _e,_c=_mkmark(_ch,_site,_cave,_rep)
    P["mk_"+_ch+"e"]=(_site,_e,f"mark {_ch} {_desc}")
    P["mk_"+_ch+"c"]=(_cave,_c,f"cave mark {_ch}")


P["xpD"]=(0x342b8, _le("52800aa0d2800030d4001001"), "exit(0x55) @epilogue head")
P["xpE"]=(0x342d8, _le("52800cc0d2800030d4001001"), "exit(0x66) @B mapSplit site")
P["xpF"]=(0x34298, _le("528008a0d2800030d4001001"), "exit(0x44) @reuse call site")
P["xpG"]=(0x352ec, _le("528009e0d2800030d4001001"), "exit(0x4f) @chkstk BLRAA")
P["cklite"]=(0x47290, _le("d280000017ffb3e9"), "cave lite: w0=0;b 0x3429c (no syscalls)")
P["ckjump"]=(0x47290, _le("d280000017ffb402"), "cave jump-only: w0=0;b 0x3429c")
P["xpK"]=(0x35380, _le("52800ae0d2800030d4001001"), "exit(0x57) @post-preflightMain land")
P["xpL"]=(0x355fc, _le("52800c00d2800030d4001001"), "exit(0x60) @join site")
P["xpM"]=(0x35690, _le("52800d20d2800030d4001001"), "exit(0x69) @pre-536 site")
# --- one-shot exit probes (12B inline, terminal) ---
P["xpA"]=(0x352bc, _le("52800aa0d2800030d4001001"), "exit(0x55) @mapSplit entry")
P["xpB"]=(0x35770, _le("52800cc0d2800030d4001001"), "exit(0x66) @preflightMain+4")
P["xpC"]=(0x35384, _le("52800ee0d2800030d4001001"), "exit(0x77) @post-preflight")

# DEFAULT: original-preflight clean build (no injected blob).
DEFAULT = ["crossarch", "hasexisting", "prereuse", "filescount1",
           "dynoff", "accessor", "fcntl_nop", "cover_b"]


def _dearm64e(data):
    # Convert ALL arm64e pointer-auth branch/call families to plain br/blr:
    #   brab*  = 0xd61f0800|Rn   braa*  = 0xd61f0c00|Rn   (branch)
    #   blrab* = 0xd63f0800|Rn   blraa* = 0xd63f0c00|Rn   (call)
    # also retab/eretab -> ret, braab -> b (rare).
    import struct
    n = 0
    for off in range(0, len(data) - 4, 4):
        w = struct.unpack_from('<I', data, off)[0]
        if (w & 0xfffffc00) == 0xd61f0800 or (w & 0xfffffc00) == 0xd61f0c00:
            struct.pack_into('<I', data, off, 0xd61f0000 | ((w >> 5) & 0x1f)); n += 1  # -> br Xn
        elif (w & 0xfffffc00) == 0xd63f0800 or (w & 0xfffffc00) == 0xd63f0c00:
            struct.pack_into('<I', data, off, 0xd63f0000 | ((w >> 5) & 0x1f)); n += 1  # -> blr Xn
        elif w == 0xd65f0bff:   # retab
            struct.pack_into('<I', data, off, 0xd65f03c0); n += 1  # -> ret
    return n


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "list":
        for k, (o, b, c) in P.items():
            print(f"{k:12s} 0x{o:x}  {b.hex()}  # {c}")
        return
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    out_name = sys.argv[1]
    keys = sys.argv[2:] or DEFAULT
    d = bytearray(open(PRISTINE, "rb").read())
    for k in keys:
        if k == "dearm64e":
            n = _dearm64e(d)
            print(f"applied {'dearm64e':12s} -> {n} brab/blrab sites converted to br/blr")
            continue
        if k not in P:
            print("unknown patch:", k); sys.exit(2)
        off, b, c = P[k]
        d[off:off + len(b)] = b
        print(f"applied {k:12s} @0x{off:<7x} <- {b.hex():20s}  # {c}")
    out = os.path.join(ROOT, out_name)
    open(out, "wb").write(d)
    print("wrote", out, len(d), "bytes")


if __name__ == "__main__":
    main()
