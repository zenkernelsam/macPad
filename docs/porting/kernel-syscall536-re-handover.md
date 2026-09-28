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

---

# MILESTONE 2026-09-27 — errno matrix nailed + all setup EFAULT sites disproven

## Measured errno per input shape (dyld 15.6.1, chroot /var/mnt/rootfs)

| Test (dyld patch variant) | files[] sent to syscall | Result |
|---|---|---|
| `fc0` | 1 anonymous entry {fd=-1, cnt=1} + dynamic region mapping | **success** (536 returns 0; dyld then fails later with "cache not loaded" — expected, no real file mapped) |
| `nodyn` | 1 real file {cryptex cache fd} + 8 macOS mappings | **EINVAL(22)** |
| `map1` | 1 real file + only mapping[0] {VA 0x180000000, sz 0x67f5c000, foff 0, prot 5/5} | **EFAULT(14)** |
| `map1` + slide-bit cleared (`clrslide` @ patch clears 0x20 in sms max/init prot) | same | **EFAULT(14)** — slide path eliminated |
| `map1` + `init_prot|=VM_PROT_ZF(0x10)` → kernel takes ANON path for same address | same | **EFAULT(14)** — file object eliminated; address/entry path still fails |
| `file@+0x40000000` (cache VA[0] edited in file) | real file, 1 mapping at submap offset 0x40000000 | **SIGKILL** (536 succeeded → first touch of modified-cache page → cs_invalid_page kill; NOT a mapping failure) |
| `anon@offset 0` (dynoff=0 variant) | fd=-1 mapping at offset 0 | **SIGKILL** under modified binary (CS kill of dyld itself — inconclusive for syscall) |

errno decode chain (RE-confirmed): worker returns kern codes; wrapper
`sub_FFFFFE0008459134` maps 0→0, 1→EFAULT(14), 2→EPERM(1), 3→ENOMEM(12),
≥4→EINVAL(22). `setup` (`sub_FFFFFE0008459570`) instead returns **raw errno**
directly (22/1/12/…).

## Setup `sub_FFFFFE0008459570` — complete error-site map

8 args: a1=proc-ish, a2=files_count, a3=user files, a4=mappings_count,
a5=user mappings, a6=&internal_files_out, a7=&sr_out, a8=task root dir vnode.

Returns **errno** (not kern codes):
- `!sr` (task has no shared region) → 22
- `sr->+0x18 (sr_root_dir) != a8 && rootvnode != a8` → **1 (EFAULT)**
- anon file (fd=-1): `mappings_count>=2` → 22; sms_address/sms_size not
  page-aligned (shift=*(cpu+0x34), fallback 14) → 22
- `sub_FFFFFE0008377D54` (fileproc lookup, returns raw errno) → raw
- `*(fileproc+0x10)=fileglob; (fg+0x10)&1 == 0` → **1** (gated by
  `dword_FFFFFE000A9FCA58` trace-level, prints if ≥1)
- `vnode_getattr`: `va_uid != 0` → **1**
- `*(vnode+0xD8) != *(a8+0xD8)` → falls back to
  `vnode_lookupat("/private/preboot/Cryptexes")` then compares `+0xD8` —
  mismatch → **1**
- `ubc_cs_blob_get(vp,-1,-1,sms_file_offset)` → `blob==NULL` or
  `blob[6]+blob[5] > map_end || blob[7]+blob[5] < map_start` → **22**
  (per-mapping CS coverage check)
- various: `v_type != VREG`, no pager/control, zero-fill bookkeeping → 22

### All four setup EFAULT sites DISPROVEN by runtime measurement (KRW reads)

| Site | Measured |
|---|---|
| sr_root_dir vs a8 | fc0 (same region+task) passed → equal |
| `(fileglob+0x10)&1` | cryptex cache fg+0x10 = `0x100000001` → bit set ✓ |
| `va_uid != 0` | files are root:wheel uid=0 → pass |
| `vnode+0xD8` mismatch | mac file / rootdir / macdsc ALL `0xfffffe22a96d4340` (same mount) → first check passes, cryptex fallback NOT reached |

⇒ **EFAULT(14) originates inside the mapping WORKER**
(`sub_FFFFFE00080623D4` → `sub_FFFFFE0008017E5C`/`sub_FFFFFE0008019768`),
not in setup. EINVAL(22) for the 8-mapping set is a *separate* failure —
most likely the per-mapping `ubc_cs_blob_get` coverage check (last untested
setup predicate) or the worker loop.

## Enter layer — `sub_FFFFFE0008019768` (vm_map_enter)

- Only DIRECT `v16=1` (INVALID_ADDRESS) = bounds check:
  `start < *(map+0x20) || end > *(map+0x28) || start>=end`.
  Measured live submap: **min=0x0, max=0x100000000** → offset-0 entry passes.
- All other nonzero returns come via subcalls:
  `sub_FFFFFE000801DF20` (when a5&0x4000),
  `sub_FFFFFE000801CE34` (on `v49→LABEL_291` path; called with
  (map,start,end,prot,24,1,0,0,0) when `map+181h&2` — the nested-map flag),
  `sub_FFFFFE00080231D0`, `sub_FFFFFE0008034B2C` (alloc entry),
  `sub_FFFFFE000801CBF4`, `sub_FFFFFE000808F3E8`, `sub_FFFFFE0008090038`.
- `sub_FFFFFE0008017E5C` (vm_map_enter_mem_object wrapper): returns
  4/17/29 only — never 1. ⇒ the "1" is inside 8019768 or its callees.
- `v16=3` (ENOMEM) sites exist but we get 1.

⇒ NEXT: find which callee inside 8019768 returns 1 for a file-backed
entry in an EMPTY nested submap. Candidate: `sub_FFFFFE000801CE34`
(the permanent/protect sync on nested-map path) or the object-branch
checks on `v154`/`v89` (vo_size vs end, object internal bit etc.).

## AMFI mmap hook — measured & RE'd

- `mac_file_check_mmap` = `sub_FFFFFE000867A738` — MACF dispatch,
  collects prioritized module errors (11>22>3>2>13>1>other).
- AMFI impl `hook_file_check_mmap` @ `0xfffffe000a659664`:
  `if (prot&4) { vp=fg_get_vnode(); if (!vnode_isdyldsharedcache(vp))
      return cred_sb_evaluate(cred,16,{type=1,vp}); } return 0;`
- `vnode_isdyldsharedcache` @ `0xfffffe000811468c` = `(vp+0x54) >> 9 & 1`.
- Measured v_flag(+0x54):
  - cryptex macOS cache: **0x184a00 — bit9 SET** → AMFI check skipped
  - iosdsc copy: 0x84800 (bit9 clear) — yet iOS cache passed 536 before
    → sb_evaluate tolerated it
  - macdsc copy: was 0x84800 → KRW-set to 0x84a00 via set_vshared.py

## Confirmed struct offsets (this session, runtime-verified)

```
proc +0xF8            fd_ofiles;  fd_ofiles + fd*8 → fileproc
fileproc +0x10        fileglob
fileglob +0x10        flags — bit0 must be SET (else setup EFAULT)
fileglob +0x38        vnode
vnode   +0x54         v_flag — bit9 (0x200) = VSHARED_DYLD
vnode   +0x78         ubc_info
vnode   +0xB8         v_name
vnode   +0xD8         mount/subtree owner — files sharing fs share value
shared_region +0x18   sr_root_dir vnode
shared_region +0x38   base  (+0x40 size, +0x48/+0x50 nesting, +0x76 stale)
vm_map  +0x20         min_offset   (measured 0)
vm_map  +0x28         max_offset   (measured 0x100000000)
vm_map  +0x18         first entry ptr ; +0x30 nentries
cs blob               v66[5]=coverage base, v66[6]+v66[7]=covered range
                      (vs mapping file-offset range; else setup EINVAL)
dyld cache mapping    56-byte records @ header+0x138 (off) / +0x13C (cnt):
  +0x00 VA  +0x08 size  +0x10 file_off  +0x18 slideInfoFileOff
  +0x20 slideInfoFileSize  +0x28 flags  +0x30 maxProt|initProt<<32
macOS cache rec0 = VA 0x180000000 sz 0x67f5c000 foff 0 slInfo 0/0 flg 0 prot5/5
```

## Device tooling notes (reproducible traps)

- `python3 -` (stdin) and `python3 -c` segfault (rc=139, no output) under
  Dopamine checkin — **only file-based scripts work**.
- `sysctl` is at `/var/jb/usr/sbin/sysctl`; PATH must include /var/jb/*.
- `sysctl vm.shared_region_pivot=1` → "Operation not permitted" from this
  shell (write-once/denied) — region staleness was instead cleared via
  `vm.shared_region_destroy_delay=0` + KRW marking node stale.
- Working scripts: `/var/mobile/{set_vshared.py,fgdump.py,vnd8.py,blob_read.py,subwalk.py}`.
- KRW write of vnode v_flag (non-pointer field) is safe — pointer fields
  are PAC'd, writing them panics.

## Open questions (ordered)

1. Inside `sub_FFFFFE0008019768`: which callee returns 1 for a file-backed
   entry? (decompile 801CE34 / 801DF20 / 80231D0 — check each return-1 site)
2. Why EINVAL(22) with all 8 mappings — is the `ubc_cs_blob_get` coverage
   check failing on a specific mapping? (rec3 flags=0x44 / rec5 0x5 /
   rec6 0x20 — AUTH/CRYPTO/DIRTY flag bits may route differently)
3. Does `vnode+0xD8` cryptex-subtree comparison actually allow the REAL
   `/System/Volumes/Preboot/Cryptexes/...` file, or does setup expect the
   file under a cryptex mount (`/private/preboot/Cryptexes` +0xD8 was
   DIFFERENT = 0xfffffe22a96d4d60 — that lookup branch would EFAULT if
   reached; it's only avoided because rootfs mount == rootdir mount).

## MILESTONE 2026-09-27 (cont.) — corrected evidence: file-backed path fails at ALL offsets

### Error corrections vs earlier session notes

1. **ZF experiment was invalid**: the `init_prot` patch encoded `0x407`
   (bit10=0x400), NOT `VM_PROT_ZF=0x10`. The kernel ZF check is
   `*(sms+0x2C) & 0x10`. Anon path for file records was never actually
   tested via that route.
2. **"file@nonzero offset succeeded" interpretation was wrong**: retesting
   VA[0]=0x1c0000000 (submap offset 0x40000000) gives the same 134-abort
   as offset 0. The earlier 139-SIGSEGV was a dead-cave artifact.
3. **dynoff restore**: original `dynamicDataOffset` (header+0x1F0) =
   `0x12c75c000`, NOT 0. A `0` write was a modification, not a restore.

### Cleanest current error matrix (all with real cryptex-path file)

| files[] | mappings | result |
|---|---|---|
| {fd=-1} anon only | dynamic region | **success** (536=0) |
| {fd=cache} | mapping[0] only | **EFAULT(14)** — e5 probe byte `0e` confirmed twice |
| {fd=cache} | all 8 | **EINVAL(22)** |
| {fd=cache} | m[0] at VA+0x4000, +0x40000000, initProt+0x10 | **EFAULT/134** (offset-independent) |

⇒ **file-backed `vm_map_enter_mem_object` (`sub_FFFFFE0008017E5C`)
fails for the macOS cache regardless of target address.**
Setup phase fully cleared (all 4 EFAULT sites measured pass).
EFAULT = worker code 1 = `KERN_INVALID_ADDRESS`.

### Inside `sub_FFFFFE0008017E5C` / `vm_map_enter_mem_object_helper`

Source path (xnu-8792.81.2 `vm_map.c:3977+`): for `IKOT_MEMORY_OBJECT`
port → `memory_object_to_vm_object` → `pager_ready` wait →
`memory_object_map(pager, prot)` → **`vm_object_copy_strategically`
(because copy=TRUE)** → `vm_map_enter`.  Errors from
copy_strategically propagate directly (`vm_map.c:4742`).

The ANON path skips all of this: `vm_object_allocate` (`sub_8034B2C`)
→ `sub_8019768` (vm_map_enter) directly. This is the ONLY structural
difference between working and failing paths.

`vm_object_copy_strategically` (`vm_object.c:3742`):
- `COPY_DELAY` → `vm_object_copy_delayed` (cheap shadow, may fallthrough)
- `COPY_NONE` → `vm_object_copy_slowly` (physically copies EVERY page —
  for 1.7 GB object this is enormous; failure modes: INVALID_ARGUMENT,
  MACH_SEND_INTERRUPTED)
- `COPY_CALL` → `vm_object_copy_call` → pager `memory_object_copy()`
- `COPY_SYMMETRIC` → RESTART_COPY → `vm_object_copy_quickly`

### New suspect (ordered)

1. **The file's vm_object `copy_strategy`**: if the vnode pager handed
   the kernel a COPY_NONE object for this file, `copy_slowly` attempts a
   physical 1.7 GB page-for-page copy inside the syscall — likely to
   fail. iOS stub object may use a different strategy. UNVERIFIED —
   need `obj+0x70` (copy_strategy) + `obj+0x7C` flags read; my
   obj-via-`ctl+0x20` chain needs re-verification (reads were noisy).
2. `object->pager_ready == FALSE` for this file → wait loop → could
   propagate an error on interrupt.
3. `sub_FFFFFE000801CE34` (the map+0xB4&2 nested-flag protect sync on
   the v49 tail) — returns v22/v91 from callees; may return 1.

### Helper arg semantics (from worker decompile)

`sub_FFFFFE0008017E5C(v23=submap, v95=&target_addr)` — the "port" arg
(a8) comes from `*(file_record+0x48)` = the file's **ui_pager**
(vnode_pager). `pager+0x10` = ui_control (back-ptr). The helper reads
`*(a8+16)` and treats it as the vm_object — so effectively
`object = ui_control` in the IKOT_MEMORY_OBJECT path. Still needs a
clean read of `moc` layout; `control+0x20` looks like the real
vm_object ptr and `control+0x18` like a map/ref count (0x146 for the
cryptex file — it had been mapped before by other processes/tests).

### Environment notes (new traps)

- **Interpreter is `/usr/lib/dyld`** (NOT `/usr/bin/dyld`) — deploy
  probes to BOTH; usr/lib/dyld was clobbered once today by an unknown
  writer (timestamp 12:51).
- **Exec vetoes cascade**: after several CS-invalid execs, ALL chroot
  execs die SIGKILL (even `true`, even TC-whitelisted dyld). zsh itself
  starts failing to dlopen signed modules. It recovered spontaneously
  before; `jbctl trustcache add <cdhash>` for the deployed interpreter
  is necessary but NOT sufficient once the cascade starts.
- `objc`/`dyld` output bytes: e5 errno probe writes 8 raw bytes to
  stderr at the syscall stub — grepable as `^\x0e\x00{7}` or
  `^\x16\x00{7}` at file start.
- Device dirs: real tests use cryptex path
  `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e`;
  `/var/mnt/rootfs/macdsc/` is a scratch copy (has been edited; its
  embedded signature no longer matches contents — do NOT use for CS
  experiments anymore; restore by re-copying from Mac
  `/Users/ciscohe/Desktop/dyld-cache-15.6.1/`).
- Pristine cache on Mac: `/Users/ciscohe/Desktop/dyld-cache-15.6.1/dyld_shared_cache_arm64e`
  (size 0xa1b18000, dynoff 0x12c75c000, rec0 prot 0x500000005).

---

## 2026-09-28 (post-panic) — 536 full syscall path mapped; region lifecycle solved

### Panic attribution
`panic: Invalid/destroyed mutex 0xfffffe20e0397e38 @lock_mtx.c:203` was caused
by a KRW write copying `ubc+0x38` between vnodes — **ubc+0x38 is NOT cs_blobs;
it points at an object the kernel takes as a mutex**. Do not write ubc fields.
(cs_blobs is `ubc+0x50`.)

### Definitive syscall plumbing (T8103 16.3, IDA base 0xfffffe0007004000)

- sysent #536 → `sub_FFFFFE0008459134` ("wrapper", narg=2, munger
  `sub_80B5A04`). Earlier confusion: `sub_83F1DB8` is syscall #544, NOT 536 —
  sysent table base 0xfffffe0007999688.
- Wrapper: copyin files[] (12B/rec: fd,count,slide) and mappings[] (48B/rec);
  slide randomize `slide = rand32() % files[0].sf_slide & ~0x3FFF` added to
  every sms `address` (+0) and `slide` (+24 if nonzero);
  `rootdir = *(proc+0x288) ?: rootvnode`;
  calls `sub_8459570` (validate/setup) then `sub_8061EF0(region, nfiles, recs)`
  (populate); **populate return >3 → EINVAL; 1→?, 2→EPERM, 3→ENOMEM**.
- `sub_8459024` = `shared_region_check_np` core: `*(task+0x3E8)` get/ref;
  `*a2==0` → detach (`sub_806391C` unmaps window + `sub_8060A68(task,0)`
  unbind); `*a2==-1` sets task flag +1194.

### Shared-region object model (all RE-confirmed)

- `task+0x3E8` = bound `vm_shared_region` (get: `sub_80608E8`, set:
  `sub_8060A68`). Region bound at vm_map init via `sub_802D40C → sub_8063720`.
- Global queue head `off_FFFFFE000A9F2300`. Dedup (`sub_8060FD0`) matches on:
  `+0x18` (a1 = **rootdir vnode** — confirmed: chroot's a1 = vnode "rootfs"),
  `+0x20/+0x24` kinds, bytes +112/+115/+118(stale)/+119/+120, +0x90.
  `+118=1` → skipped by dedup (can be KRW-set to retire a poisoned region).
- Region fields: `+0x28` = mem_entry port (→port+0x48 kobj→+0x10 submap),
  `+0x38/+0x40` = base/size (chroot & iOS both 0x180000000/0x100000000),
  `+0x18` rootdir vnode key, `+0x30` = -1.
- Chroot (rootdir=vnode) regions are destroyed when last task unbinds;
  a1=0 system regions persist.
- Queue snapshot this boot: node0 = iOS system region (60 entries,
  cache data at off 0x28094000+ → VA 0x1A8094000 — matches dyld's
  "re-using existing shared cache" listing). node1 = poisoned leftovers
  [0x70cdc000..0x7552c000] + sentinel (f119=1) — stale-marked it.

### EINVAL (22) site map in `sub_8459570`

| IDB addr | Condition |
|---|---|
| 0x84596c4 | sum(files[i].count) > mappings_count |
| 0x845976c | **`task+0x3E8 == 0` — no bound shared region** |
| 0x8459774 | (same site, trace twin) |
| 0x8459814 | fd→fileglob lookup failed (errno arg=22) |
| 0x8459d04 | anonymous/dyn mapping VA or size not page-aligned |
| 0x8459d74 | dyn record (fd=-1) with count≥2 |
| 0x8459d50 | `*(u16*)(vnode+0x70) != 1` (v_type != VREG) |
| 0x8459ce0 | `vp+0x78`(ubc)==0 or `*(ubc+8)`==0 (UBC not instantiated) |
| 0x8459cb0/b4 | per-mapping: blob coverage/range check |
| 0x8459cbc | per-mapping: `(sms+44 & 0x10)==0` && (`sms_size==0` \|\| `foff+size` overflow \|\| `ubc_cs_blob_get`==NULL \|\| blob doesn't cover [foff,foff+size)) |

EPERM(1) sites: fglob flag check, `va_uid!=0`, file's mount != rootdir's mount
(unless on /private/preboot/Cryptexes mount), region rootdir mismatch.

### sms (mappings[]) layout — confirmed by live dump at 0x35690

48B stride: `+0 va, +8 size, +16 file_offset, +24 slide_size, +32 ?, +40
{max_prot,init_prot}`. Kernel per-mapping CS check: skipped if
`sms+44 & 0x10` (byte inside init_prot half — VM_PROT_ZF-class flag).

Main cache sms[0] = `{va 0x180000000, size 0x67f5c000, foff 0, slide 0,
prot 5/5}` — foff=0 is legal (covered by blob range check).

### Blob state (verified post-cachereg, this boot)

`cachereg` (fcntl F_ADDSIGS {0,cso,css}) attaches cs_blob at `ubc+0x50`;
blob+0x38=cso (covered-range end), +0x40=css. Both main & .01 attached OK
(main superblob first-page repair still holding after reboot — file
content persists on the data volume).

### Currently-open EINVAL (main still fails with blobs+VSHARED+VREG)

Remaining un-excluded candidates:
- `sub_8061EF0` populate returning >3 → wrapper EINVAL (NOT yet audited).
- `sub_867A738` MAC file_check_mmap(prot=7,flags=0x12) propagating 22 from
  some policy — AMFI skips VSHARED files, but Sandbox/other policies could
  still veto. Needs per-policy branch trace.
- `*(vp+216)` mount == `*(rootdir+216)` — verified same fs (both under
  /var/mnt/rootfs on data vol) → passes; cryptex fallback exists anyway.
- fd lookup: dyld's fds resolve (files opened successfully before syscall).

### dyld_plat.bin patch inventory (measured, vs stock dyld-15.6.1)

Only: crossarch + misc small sites; **NOT** hasexisting (real
hasExistingDyldCache incl. its `__shared_region_check_np` call), NOT
prereuse, NOT filescount1 (production files=[main,.01,dyn]), NOT dynoff
(real dyn VA), NOT accessor/fcntl_nop/cover_b. `hasexisting`/`prereuse`
are diagnostic-only: they skip the check_np inside hasExistingDyldCache
which is harmless for binding (map-init binds anyway) but changes reuse.
