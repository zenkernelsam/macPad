# Kernel RE Handover — shared_region_map_and_slide_2_np return 40 (iPadOS 16.3 / T8112)

## Mission (终点 / definition of done)

The macOS 15.6.1 dyld, running chrooted on iPadOS 16.3 (Dopamine), calls
`shared_region_map_and_slide_2_np` (BSD syscall #536) to map the macOS dyld
shared cache. The kernel returns raw value **40** in W0 (observed stably via an
exit-code probe at dyld thin offset `0x35698`, 6/6 runs). We need the **exact
instruction + predicate in the real kernel binary that produces return value
40**, and a semantic conclusion:

- which check failed (MAC policy hook? vnode attr? cryptex/volume identity?
  cache UUID? lock/state?), and
- whether the check can be legitimately satisfied for a foreign (macOS,
  non-iOS-cryptex) shared cache file, i.e. is this path fixable or a hard
  blocker.

Do **not** declare it blocked until the producing branch is identified with an
IDA address and its inputs understood.

## IDA target

- File: `/private/tmp/kc_raw.bin` — raw arm64e Mach-O kernelcache, ~76 MB,
  decompressed from device IMG4 (`bvx2`/LZVN). iPad13,11, iPadOS 16.3 (20D47).
- IDB: `/private/tmp/kc_raw.bin.i64`, imagebase `0xfffffe0007004000`.
- MCP server: `ida-pro-mcp-Instance2`, port `13338`.
- State at handover: `hexrays_ready=true`, `auto_analysis_ready=false`,
  strings/xrefs caches NOT built. Do not rely on `XrefsTo` or function list.
  Work with direct queries:
  - `idc.get_wide_dword/get_qword/get_wide_byte(ea)` — **qwords are already
    fixup-decoded** (values like `0xfffffe0008...` are real VAs).
  - `ida_ua.create_insn(ea)` to force-disassemble unexplored code.
  - `ida_funcs.add_func(ea)` then `ida_hexrays.decompile(f)` works even while
    auto-analysis is running.
  - `idc.get_segm_name(ea)` / `idautils.Segments()` for segment map.

## Kernel address map (established)

```
com.apple.kernel:__cstring  0xfffffe0007ea0000..0x7f1c000  (approx; assert/func strings incl. "vm_shared_region.c")
com.apple.kernel:__text     0xfffffe0007f1c000..0x86b8000  (main kernel code)
com.apple.kernel:__const    0xfffffe00078a0000..0x79e1a50  (DATA_CONST; contains sysent)
com.apple.kernel:__data     0xfffffe000a9e4000..           (kernel __DATA)
AMFI  (com.apple.driver.AppleMobileFileIntegrity):
   __text    0xfffffe0009299b10..0x92b35b4
   __const   0xfffffe0007baa6f8..0x7bad148
Sandbox (com.apple.security.sandbox):
   __text    0xfffffe000a648930..0xa674288
   __const   0xfffffe0007e3fa48..0x7e418c8
```

## sysent discovered

`sysent` lives at **`0xfffffe0007999680`**, 24-byte entries, **556 entries**.
Entry layout (verified against syscall 1/294/536 semantics):

```
+0x00  qword  munger/helper fn ptr or 0
+0x08  qword  packed flags/narg metadata
+0x10  qword  sy_call → code ptr into kernel __text
```

- sysent[294] `shared_region_check_np` → `0xfffffe0008459024` (not yet analyzed)
- sysent[536] `shared_region_map_and_slide_2_np` → `0xfffffe0008459134` (fully
  disassembled — see below)

## Call graph recovered (all in com.apple.kernel:__text)

```
0x8459134  shared_region_map_and_slide_2_np wrapper
   ├─ kalloc files[] 12B/entry (max 256), mappings[] 48B/entry (max 2048), copyin
   ├─ slide = (read_random % files[0].+8) & ~0x3fff        @0x8459224..0x8459288
   ├─ loop: apply slide to each mapping (48B stride)      @0x8459390..0x8459413
   ├─ CASA/CASL lock on proc+0xD8, vnode_get(rootdir||rootvnode)
   ├─ BL sub_8459570   = shared_region_map_and_slide_setup   @0x8459488
   │     returns W0: 0 ok | nonzero → returned raw as kern_return_t
   ├─ on success: LDP X26,X27,[SP,#0x10] (setup outs: shared_region, files)
   ├─ BL sub_8061EF0   = map orchestrator                     @0x84594b0
   │     returns ENUM 0..3 → jumptable → W24 ∈ {0(flag check),0,EPERM,ENOMEM}
   │     (>3 → EINVAL 22)
   └─ BL sub_8459D90   = cleanup (return discarded)           @0x8459534
```

The wrapper's OWN exits can only emit {0,1,12,14,22}. **Return 40 can only
arrive via passthrough inside sub_8459570** (or the vnode helpers it calls).

## sub_8459570 = shared_region_map_and_slide_setup (fully decompiled)

Per-file validation loop over files[] (56B internal stride). Rejection values:

| check | ret |
|---|---|
| `fd==-1` entry with mapping_count>=2 | 22 |
| mapping addrs not page-aligned (mask 0x3FFF or pmap-derived) | 22 |
| `file_vnode`/`fp` lookup `sub_8377D54` fails | passthrough (0 or 9) |
| `fileglob+0x10 byte & 1` clear | **1** |
| `vnode_getwithref` fails | passthrough (errno) |
| `*(WORD*)(v44+0x70) != 1` (fileglob type?) | 22 |
| `mac_file_check_mmap` `sub_867A738(cred, fp, 7, 18, 0, &7)` fails | **passthrough — PRIME SUSPECT for 40** |
| `vnode_getattr` uid != 0 (file not root-owned) | 1 |
| file mount != rootdir mount AND file mount != `/private/preboot/Cryptexes` mount (string `0x7ee04cd`) | **1** |
| `vnode_getattr` size | passthrough |
| `*(WORD*)(vp+0x70)!=1` or `*(vp+0x78)==0` (ubc/vnode fields) | 22 |
| per-mapping range outside `ubc_cs_blob` signed range | 22 |

So errno-candidates that can emit arbitrary values: **`mac_file_check_mmap`
aggregation**, `vnode_getwithref`, `vnode_getattr`, `vnode_lookupat`,
`file_vnode`.

## sub_867A738 = mac_file_check_mmap (fully decompiled)

Iterates `mac_policy_list`: count @ `0xfffffe00079e175c`, entries array ptr @
`0xfffffe00079e1768` — **both are 0 in the static image** (runtime-populated;
dead end for static enumeration). Loop body:

```
policy = *(arr + i*8)
ops    = *(policy + 0x20)
hook   = *(ops + 0x120)          // mpo_file_check_mmap
v17    = hook(cred, fp, 0, prot, flags, offset, &maxprot)
merged v13 = worst-of({11,22,3,2,13,1} precedence, else any nonzero)
return v13
```

**mpo_file_check_mmap lives at ops+0x120.** To find each policy's hook
statically, locate `mac_policy_ops` tables in each MAC kext's `__const`
(pointer-dense struct whose slots point into that kext's `__text`), then read
`ops+0x120`.

One scan already tried: AMFI `__const` candidate table `0x7bacbe8..0x7bacd88`,
`+0x120` → `0x92aba20` — but that address decompiles as a C++ TLE destructor,
so either the table or the +0x120 assumption is off for AMFI's ops layout.
Verify ops layout by locating `mpo_*` hooks (functions with MAC-hook
signatures) or cross-check with xnu `security/mac_policy.h` field order.

## Return-40 literal sites (MOV Wn,#0x28; byte pattern [rd,0x05,0x80,0x52])

```
AMFI __text:    0x929ff20 0x92a9e48 0x92a9ee0 0x92a9f10 0x92a9f74
Sandbox __text: 0xa6551a4 0xa656ddc 0xa657b80 0xa66a828
                0xa66f980 0xa66f9b4 0xa66fa04 0xa670628
kernel __text:  NOT YET SCANNED (range 0xfffffe0007f1c000..0x86b8000)
```

Also scan kernel `__text` for the same pattern — setup's callees like
`vnode_getattr`, `vnode_getwithref`, `vnode_lookupat`, `ubc_cs_blob_get`,
`fp_*` live there and may return 40 directly.

## Remaining unknowns / next steps for the other AI

1. Scan com.apple.kernel `__text` for `MOV W*,#0x28` sites; decompile each
   containing function; check reachability from `sub_8459570`'s call list.
2. Properly locate every `mac_policy_ops` in AMFI + Sandbox `__const` (and any
   other kext registering MAC policy, e.g. `com.apple.kext.CoreTrust`,
   `AppleImage4`): find the ops table, read `+0x120`, decompile the hook,
   check whether it can return 40 for a file that is: on chroot-root volume
   (data volume, not preboot/cryptex), Apple-signed blob? — our test file was
   `/var/mnt/rootfs/...` macOS cache, opened O_RDONLY by dyld, fd passed in
   files[0].
3. Decompile `sub_8061EF0`→`sub_80623D4` completely (partial decompile shown
   above; it returns small codes, wrapper clamps to enum anyway — lower
   priority but confirm no path smuggles 40).
4. Decompile sysent[294] handler `0x8459024` (`check_np`) for completeness and
   to confirm table indexing is right.
5. Deliverable: one branch, one address, one predicate. e.g.
   "AMFI mpo_file_check_mmap at 0xXXXXXXXX returns 40 (EMSGSIZE) because
   `<condition>`" — plus whether a foreign cache can satisfy it.

## Runtime context (what the kernel saw)

- Caller: macOS 15.6.1 dyld under chroot `/var/mnt/rootfs`, files_count=1,
  files[0] = {fd=opened DSC fd, slide range at +8, mappings ptr at +4?}
  (file entry 12B; slide field +8; per-file mappings 48B stride;
  mapping fields: addr@+0?, size@+8, file_offset@+0x10, slide@+0x18,
  max/min prot@+0x28, init prot flags at +0x2c bit0x10 = "no-cs/readonly"?)
- Cache file: `/var/mnt/rootfs/System/Library/dyld/dyld_shared_cache_arm64e`
  inside chroot = on the **data volume**, Apple-distributed macOS file.
- errno measured **40** (EMSGSIZE / KERN_LOCK_OWNED=40 — disambiguate which
  semantics the producing site intends).
- Earlier same-family checks observed: file on wrong mount → EPERM(1);
  that matches the mount-vs-cryptex check in setup.

## Methodology notes (for whoever continues)

- `idc.get_qword` reads are already fixup-resolved — pointer tables are usable.
- `MOV Wn,#imm32` sites: word `(0x52800000 | imm<<5 | rd)`; for #40 use mask
  `w & 0xffffffe0 == 0x52800500`.
- Vector/adrp string refs: ADRP decode = `immhi:immlo << 12`, ADD imm at
  bits[21:10]; strings often also referenced from `__const` tables (scan for
  qword == string VA).
- Big scan loops over `__text` take a while — batch per segment.
