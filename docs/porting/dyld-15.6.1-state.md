# macOS 15.6.1 dyld shared-cache bring-up — live state

**READ THIS FIRST after context loss.** Active task: get dyld to map the
macOS 15.6.1 shared cache on iPadOS 16.3 (xnu-8792.82.2) so macOS binaries
run in chroot. This file is the single source of truth — update it whenever
a fact/offset/result changes, BEFORE context is lost.

## 2026-09-26 17:50 — ★ POST-REBOOT RESTORE + BLOB-ARTIFACT CORRECTION + HANDOVER ★

**NEW comprehensive handover doc**: `docs/porting/HANDOVER-15.6.1-2026-09-26.md`
— self-contained reproduction guide for a fresh agent. State below assumes it.

**Persistent artifacts moved**: kernel now at
`analysis/kc_raw_16.3_T8112.bin` (80052224 B, imagebase `0xfffffe0007004000`) +
`analysis/kernelcache_16.3_T8112.img4`. `/tmp` wipes no longer lose them.

**Post-reboot restore procedure (verified working)**:
- trustcache tool = `/var/jb/basebin/jbctl` (`trustcache info|add <cdhash>`;
  the old hvfs shim + `.trustcache` file paths are gone).
- cachereg holder redeployed at `/var/mobile/cachereg`; one stale instance
  (PID 1355) was auto-restarted post-Dopamine holding the preboot paths.
- 5 hashes confirmed IN: proof2 `b2b3a8b8…`, dyld_pm `10bc320f…`, main cache
  `2b9cccd5…`, .01 `8c7ba7e5…`, cachereg `f176402d…`.

**dyld_pi full diff vs pristine** (byte-level, all sites):
`0x76270` movx0,0;ret (crossarch) | `0x30140` movz w0,#0;ret
(hasExistingDyldCache→0) | `0x34298` movz w0,#0 (reuse→0) |
`0x3538c` movz w28,#1 (NOT files_count — it's a mapSplit flag) |
`0x35fc8` movz x9,#0x7800,lsl#16 + preserved `add` @0x35fd8 (dynregion
submit → 0x1f8000000) | `0x50dfc` movz x8,#0x7800,lsl#16 (accessor) |
**`0x3576c-0x35afe` = injected blob replacing `preflightMainCacheFile`**
(open+fctl(F_ADDFILESIGS)+header-parse+CacheInfo fill, dumps
`/tmp/MTOUT5.txt`; called from `0x3537c`).

**⚠ CORRECTION — the "103" runs were a measurement artifact**: dyld_pm =
dyld_pi + glueprobe2 @ `0x3588c` + `b` @0x6b94, but `0x3588c` is INSIDE the
live injected blob (not dead code). So `true`/`ls` exit 103 = blob hit the
probe mid-flight; dumped regs (x8=1, x9=0x228, x10=0x50c000…) are blob
internal state, NOT the glue-call site. Whether normal flow reaches
`0x6b94 blraaz x8` is still UNVERIFIED. Next probe must use a real dead
region (e.g. __text tail padding) and only patch `blraaz→b`.

**NEW MYSTERY — proof2 137**: locally-built arm64e test exe (ctor→
`/tmp/ctor_ran`, main→`/tmp/main_ran`+stdout+ret7) gets SIGKILL even with
its cdhash live in trustcache, while system binaries (`true`,`ls`) run dyld
fine. Suspect AMFI launch constraint on non-Apple CodeDirectory shape /
platform. `true`→103, `ls`→103, `proof2`→137, all hashes trusted.

## 2026-09-26 17:00 — ★★ DYNREGION EPHEMERALITY FULLY DECODED (current blocker) ★★

**Observed pattern**: after any successful map run → first child exec works
(T1=0), every subsequent exec SEGVs at `0x1f8000000`
(`KERN_INVALID_ADDRESS … not in any region`) even though the file-backed
cache mappings are still present in the shared region (crash dumps show
`180000000-1e7f5c000 __TEXT SM=COW` alive). Conclusion (runtime-confirmed):
**the fd=-1 "dynamic" mapping is torn down when the mapping process exits;
file-backed entries persist.** On real macOS this doesn't matter because the
boot-time mapper stays alive / the region is populated once.

**Exact kill site on subsequent execs** (IDA-confirmed, Instance1 dyld IDB):
`start` → `SyscallDelegate::hasExistingDyldCache` @0x30140 (called at
start+0x5a8c) → `shared_region_check_np` returns base →
`DyldSharedCache::dynamicRegion(base)` (0x50dfc, patched → 0x1f8000000) →
`DynamicRegion::getDyldCacheFileID` derefs → SEGV. This fires **before**
`loadDyldCache`, so even a forced map path can't save a populated region —
the crash is in the earliest "is there a cache?" probe.

Call graph (all IDA-verified):
```
start 0x53dc → hasExistingDyldCache 0x30140 → dynamicRegion() → DEREF (boom #1)
loadDyldCache 0x34240 → reuseExistingCache 0x351a8 → same deref (boom #2)
                  → mapSplitCacheSystemWide 0x352bc → syscall 536
                  → reuseExistingCache again at 0x356d8 (post-syscall verify)
```

**Probe harness that finally works** (deploy + measure without files):
exit-code probes need `movz w0,#N; movz x16,#1; svc #0x80` — **x16 is the
syscall selector, `svc #0x80` is just a marker**; an `svc` without x16=1
invokes a random syscall (we measured 140=SIGSYS from garbage x16, and once
"exit(85)" because `movz w0,#0x55` encodes `a8 0a 80 52` not `#0x51`).
movz imm16 occupies insn bits [20:5]: exit(N) = `movz w0,#N` byte0 =
(N&7)<<5, byte1 = N>>3.

Deployed base for all probes = `/var/mobile/dyld_pb` (patches: P1 crossarch,
W28=1 @0x3538c, dynreloc `movz x9,#0x7800,lsl#16` @0x35fc8 + preserved add
@0x35fd8, `movz x8,#0x7800,lsl#16` @0x50dfc, **plus two leftover patches
found in it**: `movz w0,#0` @0x34298 = reuse-call→0 (forces map path
always) and `b 0x3576c` @0x35698 = skip post-syscall tail).

Probe results with correct encoding:
- exit(0x51)@0x351a8 + exit(0x52)@0x352bc + exit(0x53)@0x35698 → all runs 82
  (map path always taken; reuse never entered because of the 0x34298 patch).
- exit(syscall_ret&0xff)@0x35698 (`uxtb w0,w0`): T1=**0** = 536 succeeded on
  empty region; T2-T4=139 = die BEFORE the post-syscall site → inside the
  earlier `hasExistingDyldCache` deref on the still-populated region.

**Remaining question**: does re-submitting 536 on an already-populated
region re-create the dynregion entry? To measure: patch out the early
derefs (`hasExistingDyldCache` 0x30140 → return 0; `reuseExistingCache`
0x351a8 → return 0) so the map path always runs, then read errno at
0x35698. If 0 → "always-map" is the fix (each exec self-heals dynregion).
If EINVAL/EBUSY → need a persistent keeper OR kernel-behavior workaround.

Alternative theory (NOT yet verified): entries tagged dynamic may be
per-mapper and die with the mapper — if so, a permanently resident chroot
"cache keeper" that performs the one-time map would keep dynregion alive
for all later processes. Untestable via `sleep` because a fresh exec on a
populated region dies at `hasExistingDyldCache` first — the keeper must be
the *first* process after the region is created (chicken-and-egg unless
we wipe/reset the region or the keeper itself is the only mapper and
everything else reuses).

## 2026-09-26 (post-breakthrough) — ★★ STRUCTURAL WALL FOUND: 15.6.1 CACHE > iOS 4GB REGION ★★

**The macOS 15.6.1 cache cannot fully fit in the iOS 16.3 shared region.**
This is a kernel-constant limitation, NOT a validation failure.

- iOS 16.3 (xnu-8792.81.2 `osfmk/mach/shared_region.h`):
  `SHARED_REGION_BASE_ARM64=0x180000000`, `SHARED_REGION_SIZE_ARM64=0x100000000` (4 GB)
- macOS 15.x (xnu-11215.81.4 same header — fetched from apple-oss-distributions):
  `SHARED_REGION_SIZE_ARM64=0x180000000` (**6 GB**). Apple grew the region.
- 15.6.1 cache virtual span: `0x180000000 → 0x2ac75c000` + dynregion 0x4000
  ≈ **4.69 GB → overflows the iOS region by ~0.70 GB**.
- `sr_map` submap max_offset is baked to sr_size at creation
  (`vm_shared_region.c:775` `vm_map_create_options(pmap_nested, 0, size)`);
  pmap nesting region is also 4 GB. Out-of-bounds `vm_map_enter` →
  KERN_* → errno (EFAULT/ENOMEM/EINVAL), whole submission rolled back.
  Region size is a kernel-immediate constant — cannot change without
  kernel patch (none available under Dopamine).

### Exact overflow map (from cache headers, verified)

Main file — 8 mappings, ALL fit:
`0x180000000…0x22560c000` (end), fileoff 0…0x7ad4c000. CS cso=0xa160c000 css=0x50c000.

.01 — 7 mappings, **partially overflows**:
```
m0 0x22560c000+0x54808000 → 0x279e14000   ✓ fits
m1 0x279e14000+0x21c4000  → 0x27bfd8000   ✓ fits
m2 0x27dfd8000+0x38b4000  → 0x28188c000   ✗ starts in-bounds, ends OUT
m3 0x28188c000…0x28261c000                ✗ all out
m4 0x28261c000…0x286bf0000                ✗ out
m5 0x288bf0000…0x288dcc000                ✗ out
m6 0x288dcc000…0x2ac75c000                ✗ out
```
.01 CS cso=0x83150000 css=0x41c000.
dynregion (fd=-1 anon, size DynamicRegion::size(), dynMax=0x4000):
submitted VA = `regionBase+header[0x1F0]` = `0x2ac75c000` → out.

Free gap inside region after .01-m1: `[0x27bfd8000, 0x27dfd8000)` ≈ 32 MB.
Chosen dynregion relocation VA: **0x27c000000** (region offset
0xfc000000; single-insn `movz x8,#0xfc00,lsl#16` / VA `movz #0x27c00,lsl#16`).

### dyld patch sites for hybrid plan (thin-slice offsets, byte-verified)

| off | orig | new | why |
|-----|------|-----|-----|
| 0x76270 | `01 10 00 d4 c0 03 5f d6` | `e0 03 1f aa c0 03 5f d6` | P1 crossarch noop |
| 0x351f0 | strcmp-result test | force "not equal" | P5 reject iOS cache → real map path |
| 0x35fd8 | `add x9,x9,x11` (`09 01 0b 8b`) | `movz x9,#0x27c00,lsl#16` (`09 80 ef d2`) | dynregion VA → 0x27c000000 (CacheInfo+0x1B0; consumed at 0x35660 `ldr x24,[x19,#0x11d0]`) |
| 0x50dfc | `ldr x8,[x0,#0x1f0]` (`08 f8 40 f9`) | `movz x8,#0xfc00,lsl#16` (`08 80 bf d2`) | `dynamicRegion()` returns base+0xfc000000 = 0x27c000000 — consistent with relocated map |

Submission construction (0x35660-0x35694): `stp x24,x0,[x8]` = sms_address/sms_size;
`str x22,[x8,#0x10]` = sms_file_offset = dyld-side copyin source ptr;
prot word at +0x28 = 1.

### Trimmed-set plan (in progress)

Phase A (now): files_count=1 patch (0x3538c `ldr w28,[x19,#0x1a8]`→`mov w28,#1`
= `3c 00 80 52`) → submit main 8 + dynregion only → `true` should exec on
the REAL macOS cache (verify via `_dyld_get_shared_cache_uuid`).

Phase B: custom .01 — copy file, patch header `mappingCount` 7→2 (keeps
m0,m1), **re-sign whole file** (ldid/self CD), cdhash → jbctl trustcache,
F_ADDFILESIGS → vnode blob. dyld then submits 2+1 files naturally.

Phase C: .01 tail (m2..m6 = fileoff 0x569cc000..EOF → VAs
0x27dfd8000..0x2ac75c000) private-mmap'd PROT_* per initprot by a pre-main
hook (libmachook ctor / dyld patch) — file pages validated vs the SAME
custom CD + trustcache. VAs land OUTSIDE the shared region in normal user
space — image enumeration then works transparently.

Phase D: WindowServer deps — audit which images fall >0x280000000.

### Known risk from earlier session (retained)

`pmap_trim_internal` panic on `true` exit — likely triggered while an
invalid shared-region mapping existed. Now that only in-bounds, CS-valid
mappings are submitted, this specific panic path should not recur; still,
avoid fd=-1 mappings whose copyin source is invalid (EFAULT→ INVALID_ADDRESS,
not the panic path). If panic recurs, suspect map-engine undo path.

---

## 2026-09-26 16:00 — ★★★ BREAKTHROUGH: CACHE MAPPED SUCCESSFULLY ★★★

**syscall 536 returned 0 — the macOS 15.6.1 cache IS mappable on iPadOS 16.3.**

Runtime proof (chroot child, dyld-cave probe):
```
check_np before = 12 (region exists, empty)
F_ADDFILESIGS   = 0  (blob already on vnode — attached host-side earlier)
submit mapping0 = 0  ← SUCCESS
check_np after  = 0, base = 0x180000000 ← region POPULATED w/ macOS cache
```

### The complete gate chain of shared_region_map_and_slide_2_np
(xnu-8792.81.2 `bsd/vm/vm_unix.c:2189 shared_region_map_and_slide_setup`,
source downloaded to `/tmp/dyldwork/xnu-xnu-8792.81.2/` — RE-VERIFY vs
binary when kernel is back in IDA; tag is one patchlevel off 8792.82.2)

In order, errors:
1. `files_count==0` → EINVAL; `>MAX` → E2BIG
2. `shared_region==NULL` → EINVAL (ours exists — check_np=12)
3. `region->sr_root_dir != proc->fd_rdir` (chroot root) → EPERM
4. fd==-1 pseudo-entry: >1 mapping or unaligned addr/size → EINVAL
5. fd→vnode: not file/!FREAD/!VREG → EINVAL/EPERM
6. `mac_file_check_mmap` → passthrough errno (sandbox: 40 = EMSGSIZE
   deny for non-boot-cache vnodes; bypassed via `no-sandbox` entitlement)
7. `va_uid != 0` → EPERM (file must be root-owned — ours is)
8. `v_mount` must equal root-vol mount OR preboot-cryptex mount
   (`vnode_lookup("/private/preboot/Cryptexes")` — FAILS inside chroot!)
   → EPERM. *This is why cryptex-vol files get EPERM in chroot but data-vol
   files pass — the bindfs'd preboot dir is still a disk1s6 vnode.*
9. `scdir_enforce` (if on): vnode_parent must be expected scdir → EPERM
10. `ubc_getobject` NULL → EINVAL
11. **`ubc_cs_is_range_codesigned(vp, file_offset, size)` → EINVAL** ←
    our errno-22 wall. Needs a cs_blob on the vnode covering each
    non-ZF mapping range. Blob was ABSENT because our cache file (cp'd
    into rootfs) lost its APFS fs-signature.

### Errno observations (both confirmed)
```
CHROOT child (empty region):       iOS host (populated region):
  macOS-cache@datavol → EINVAL 22    macOS-cache@datavol → EPERM 1
  macOS-cache@preboot → EPERM  1     macOS-cache@preboot → EINVAL 22
  iOS-cache@preboot   → EPERM  1     iOS-cache@preboot   → EINVAL 22
  dyld@datavol(fake)  → EFAULT 14    dyld(fake)         → EINVAL 22
```
Pattern = gate-8 volume check vs later EINVAL ordering. Populated-region
submissions all EINVAL (occupied reject in map engine).

### ★ THE FIX: `fcntl(fd, F_ADDFILESIGS=61, &fs)`
Reads superblob at `fs.fs_file_start+fs.fs_blob_start` → `ubc_cs_blob_add`
→ attaches cs_blob to the vnode. For the 15.6.1 cache:
`fs_file_start=0, fs_blob_start=codeSignatureOffset (hdr+0x28=0xa160c000),
fs_blob_size=codeSignatureSize (hdr+0x30=0x50c000)`.

**KEY: must be called from an iOS-PLATFORM process (host side).**
Inside chroot it returns EPERM — `ubc_cs_blob_add` → `mac_vnode_check_
signature(vp,…,proc_platform)` → AMFI rejects (probably because caller is
PLATFORM_MACOS, or sandbox file-check of a macOS process). Host-side call
on the SAME file succeeded (probe7 on all files → 0). Blobs stick to the
VNODE → chroot children then pass gate 11.

### Tooling that now works (all verified this session)
- **dyld cave probe**: patch `B` at thin-dyld 0x35698 → 0x3576c overwrites
  `preflightMainCacheFile` (dead once probe exits). Asm via clang
  `-nostdlib -Wl,-e,__start -Wl,-static`, extract `__text`.
  GOTCHA: GNU as drops everything after `;` on a line — ONE INSTR PER LINE.
- **iOS-native probe binary**: build `arm64e` Mach-O exec, then
  `vtool -set-build-version 2 16.0 16.0 -replace` (platform=ios) —
  plain clang output gets "wrong platform" from iOS dyld; static Mach-O
  exec gets SIGKILL'd (137) even signed+trustcached — must be DYNAMIC.
- `mount_bindfs` exists: `/var/jb/usr/local/bin/mount_bindfs` (binary,
  mounts READ-ONLY per mountdevfs comment).
- `open=5 write=4 close=6 pread=153 fcntl=92 mmap=197 check_np=294
  map_and_slide_2=536` (svc #0x80, errno in x0; success→0/value).

### mmap PROT_EXEC + pagein on cache file (probe4)
mmap PROT_EXEC **succeeded** (0x10459c000), pagein returned byte 'd' —
kernel validated exec pages without kill, but that path does NOT attach a
cs_blob to the vnode (or its coverage doesn't satisfy). Still EINVAL after.
So F_ADDFILESIGS is the correct attach mechanism, NOT mmap.

### Next steps (NOT yet done)
1. Host-side helper/daemon: open cache files + F_ADDFILESIGS before launch
   (must run at each boot/vnode-recycle — blob lives on vnode only while
   vnode cached). Simplest: keep an fd open in a resident helper.
2. Retest REAL dyld submission (full files[] incl .01 + DynamicRegion
   fd=-1 pseudo-entry at 0x1fa000000 — relocated inside 4GB region).
   The probe only submitted mapping[0]; full submit may hit new gates
   (subcache count, slide, align checks per mapping, scdir check).
3. `scdir_enforce` sysctl — if on, file parent dir must be expected path
   (/System/Library/dyld in chroot may or may not satisfy it — worked in
   probe since errno was 22 not 1... or scdir off; verify).
4. Then dyld proceeds to actual lib loading — watch for next failures.
5. Investigate whether blob attach survives across `true` runs (vnode
   recycling) — if flaky, pin fd or add to postinst/launch helper.

## 2026-09-26 15:09 — KERNEL PANIC + RECOVERY (newest, read first)

**The iPad panicked during our experiments** — log
`/private/var/mobile/Library/Logs/CrashReporter/panic-full-2026-09-26-150854.000.ips`:

```
panic(cpu 5 caller 0xfffffe0025bc6938): pmap_trim_internal:
grand addr wraps around, grand=0xfffffdf12d917450,
subord=0xfffffdf1aee850e0, vstart=0xffffffffffffffff, size=0x1
@pmap.c:11173
Panicked task: pid 46438: true   (i.e. our launchdchrootexec /usr/bin/true run)
```

Interpretation: repeated malformed `shared_region_map_and_slide_2_np`
submissions (and possibly the earlier `check_np(NULL)` detach) leave the
task/shared-region pmap state inconsistent; the NEXT exec's pmap_trim on
teardown computes a wrap-around range and panics. **vstart=-1 is the
signature.** Consequences:

- AVOID `shared_region_check_np(NULL)` (detach) entirely — it both kills
  the caller (137) and probably corrupts state for later execs.
- AVOID large batches of malformed syscall-536 submissions; space
  experiments out, prefer clean-boot measurement.
- After ANY suspicious exit pattern, re-check kernel logs.

**Post-reboot device state** (panic also rolled back unflushed APFS writes):
- `/var/mnt/rootfs` content intact; probe binaries in `usr/local/bin` survive.
- **`/usr/lib/dyld` DELETED by the rollback** — backups survive:
  `dyld.orig` (2289328B fat, pristine), `dyld.func` (fat, 5-patch), etc.
  Deployed dyld must be RE-COPIED from dyld.orig + re-patched + re-signed.
- `/tmp` wiped on BOTH device and Mac: `kc_raw.bin`, probe asm, mt_dylib,
  dyld_* variants all gone. Pristine thin slice lives at
  `macPad/analysis/dyld_15.6.1_arm64e_thin` (1240752B). Kernel re-extract
  procedure documented below still valid (IMG4→bvx2→LZVN).
- Jailbreak re-applied by user (Dopamine); SSH ok.

**EINVAL=22 investigation — remaining candidate sites** (from
`sub_8459570` decompile; task->shared_region ruled out, it exists but is
EMPTY → check_np returns 12):
1. `vnode+0x70 != VREG` — unlikely (regular files).
2. `vnode+0x78` UBC info → cs_blobs chain empty → 22. **Top suspect.**
   Cache file has never been CS-validated by UBC (never mmap'd/exec'd as
   signed object on this kernel) → vnode may have NO cs_blob.
3. Per-mapping `ubc_cs_blob_get(vnode,-1,-1,file_offset)` must cover
   [file_off, file_off+size] → 22 if any non-slide mapping uncovered.
4. `fd=-1` DynamicRegion pseudo-entry malformed (count/align) → 22.
5. NEW: `/private/preboot/Cryptexes` literal in kernel — possible
   "vnode must be on cryptex volume" check. Our files sit on the data
   volume under a *fake* cryptex PATH. If so: copying cache onto the REAL
   preboot volume (or bind-mount) may be the bypass.

**Decisive next experiment (designed, not yet run)**: patch dyld's
failure-path dead code (~0x35720+) with a custom asm probe that submits
syscall 536 for (a) `/usr/lib/dyld` itself — guaranteed CS blob since it
is executing — and (b) the macOS cache file; write both errnos to a file.
Distinguishes "file has no UBC CS blob" vs "geometry/pseudo-entry" causes.
Probe asm source lost with /tmp; re-derive from this spec.

## 2026-09-26 — PARADIGM-SHIFTING FINDINGS (read before anything else)

1. **`re-using existing shared cache` observed at runtime.** With
   `DYLD_PRINT_SEGMENTS=1` (env DOES propagate through launchdchrootexec —
   it setenvs before POSIX_SPAWN_SETEXEC so the child inherits everything)
   a `true` run printed `dyld[pid]: re-using existing shared cache
   (/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld/
   dyld_shared_cache_arm64e)` + full segment dump, then RC=0.
   **CAUTION — attribution ambiguity**: launchdchrootexec is itself an iOS
   process, so iOS dyld prints the same "re-using" line for the LAUNCHER's
   own startup (Dopamine also injects forkfix/libinjector into it — visible
   in output). Everything BEFORE the `[launchdchrootexec] target=` banner is
   the launcher's iOS dyld; everything AFTER is the child (macOS dyld).
   Must capture output after the banner to attribute. Either way, the kernel
   pre-maps the iOS boot cache into every exec'd process — macOS dyld may be
   reusing IT rather than mapping ours.
2. **errno-40 source CONFIRMED by neighbor-AI kernel RE**
   (`docs/porting/kernel-syscall536-finding.md`): Sandbox
   `mpo_file_check_mmap` @ 0xfffffe000a659664 → `cred_sb_evaluate(op=16,
   file-map-executable)` → deny errno 40 for foreign cache vnodes lacking
   VSHARED_DYLD flag. AMFI hook can only return {0,1}. AppleImage4 no hook.
3. **Deployed dyld MUST carry project entitlements** (has
   `com.apple.private.security.no-sandbox` + 233 others): bare ldid-signed
   dyld gets platform sandbox at exec → file-map-executable denied → 40.
   Entitled dyld + `true` gave RC=0 ×5 (still ambiguous vs disk fallback).
4. **trustcache grep must be case-insensitive** — `jbctl trustcache info`
   prints UPPERCASE hex; `grep` without `-i` falsely reports missing.
   `jbctl trustcache add` silently no-ops sometimes — always verify.
5. **15.6.1 cache CodeDirectory cdhashes** (superblob 0xfade0cc0 @
   header+0x28 codeSignatureOffset, CD slot 0):
   main: sha256 `2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e` / sha1 `05afea7d…`;
   .01: sha256 `8c7ba7e588b0edd43f7334e2de11688cd4732192` / sha1 `f1d3342b…`.
   Both sha256 values added to device trustcache (verified).
   Cache files live at `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/
   System/Library/dyld/` (symlinked from `/System/Library/dyld/`).
6. **`_dyld_get_shared_cache_uuid` probe** — must declare manually:
   `extern const unsigned char *_dyld_get_shared_cache_uuid(void)
   __attribute__((weak_import));` compiled OK, deployed as dsctest, first
   run 137 (cdhash race), then RC=0 with ZERO output — main seemingly never
   ran (should print UUID + exit 42/43). Unexplained.
7. **Static arm64e test binary (`hw`, raw svc, no dyld) → 137** despite
   cdhash trusted. A no-dyld exec still gets killed — suggests an
   exec-time/launch-constraint gate on arm64e Mach-Os independent of dyld.
8. **137 causes catalog**: (a) cdhash not in trustcache; (b) stale inode
   (cp-overwrite keeps old CS vnode); (c) AMFI launch-constraint (see
   launchdchrootexec main.m comment — `Launch Constraint Violation` kills
   when spawn type mismatches); (d) dyld abort_with_payload = SIGKILL;
   (e) exiting while shared-region attach mid-flight (probe <0x35698).
9. **Host reboot wiped `/tmp`** — kernel `/tmp/kc_raw.bin` + IDB +
   `/tmp/xnu8792` gone. Kernel RE doc `kernel-syscall536-finding.md` has all
   addresses. If kernel needed again: re-extract → tell user → they load in
   IDA Instance2. Device kernelcache source: `/private/preboot/CFD92CED…/
   System/Library/Caches/com.apple.kernelcaches/kernelcache` (IMG4, 21.8MB,
   bvx2/LZVN payload, decompress to ~76MB arm64e Mach-O).
10. **Merge with upstream done** (`91ff1b6`), `control` conflict resolved to
    `Depends: python3, ldid`. Repo `~/Desktop/macPad`, upstream DCMMC/macPad,
    origin zenkernelsam/macPad, `main` ahead of origin by 5. Untracked
    `analysis/` dir exists.
11. **Device binaries of record**: `/var/mnt/rootfs/usr/lib/dyld` currently =
    entitled fat build (cdhashes 82d3f27a/46282ecc — trusted) but patch
    offsets 0x352bc/0x35698 show PRISTINE bytes → it's an entitled
    UNPATCHED dyld. `dyld.func.keep` = earlier functional build backup.
12. **`dsctest`/`hw`/`true` all currently 137 or silent-0** — device state
    unstable; when `true` gave RC=0 the cache-hash adds + entitled dyld were
    in place. Reproduce DYLD_PRINT run to see WHERE it dies now.

## Device access (also in AGENTS.md)

- SSH: `sshpass -p cisco ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no -p 2222 root@192.168.64.1`
  - Password: `cisco` (`alpine` does NOT work). The `-o` flags matter: without
    them ssh-agent keys get offered first → "Too many authentication
    failures" / "UNIX authentication refused" intermittently.
  - If unreachable, IP may have changed; scan for open :2222.
- Staging dir on device (upload everything here, overwritable):
  `/var/mobile/Containers/Shared/AppGroup/1B2AD29A-2C34-4770-86EC-E11CD02312FF/File Provider Storage/macPad_iOS`
- Rootfs mounted at `/var/mnt/rootfs`; installed tools at `/var/jb/usr/macOS`.
- Pristine dyld extract on device: `/tmp/usr/lib/dyld` (from rootfs tar).
- Repo paths `/var/jb/var/mobile/MacWSBootingGuide`, `/var/jb/var/mobile/theos`
  are GONE (device re-jailbroken).

## dyld binary layout — CRITICAL

- Source dyld = fat (arm64@0x4000 + arm64e@0x100000) from rootfs tar,
  2289328 bytes. Analysis file = `analysis/dyld_15.6.1_arm64e_thin`
  (1240752 B = arm64e slice).
- **`ldid -S` REPACKS the fat**: after signing, arm64e slice moves to
  `0xfc000`. NEVER hardcode the delta. ALWAYS read the fat header after
  signing:
  `d[8+i*20+8:8+i*20+12]` big-endian = slice offset; arm64e = cputype
  0x100000c subtype 0x80000002.
- Order of operations: restore pristine → patch at CURRENT delta →
  `ldid -S` → `jbctl trustcache add <sha256 cdhash>` (BOTH slices, UPPERCASE
  hex — lowercase add is accepted but verify with `jbctl trustcache info`) →
  **`rm` dest file THEN `cp`** (cp over same inode = stale CS → silent
  SIGKILL 137. New inode required).
- NOTE: `for h in $(ldid -h ...)` loops can silently produce empty vars →
  verify `jbctl trustcache info | grep <hash>` actually lists it.

## Syscall ABI (verified vs xnu-8792.81.2 + IDA)

- `shared_region_check_np` = **294** (NOT 464 — sprobe.c originally had it
  wrong; iOS master:445). `check_np(&base)` → 0+base if region mapped;
  errno otherwise. `check_np(NULL)` = **detach/unmap the task's shared
  region** (vm_shared_region_remove + set NULL).
- `shared_region_map_and_slide_2_np` = **536**
  `(u32 files_count, shared_file_np files[], u32 mappings_count,
   shared_file_mapping_slide_np mappings[])`:
  ```c
  struct shared_file_np { int sf_fd; u32 sf_mappings_count; u32 sf_slide; }; // 12B
  struct shared_file_mapping_slide_np {   // 48B
      u64 sms_address, sms_size, sms_file_offset;
      u64 sms_slide_size, sms_slide_start;
      int sms_max_prot, sms_init_prot;    // +VM_PROT_SLIDE(0x20) etc in max_prot
  };
  ```
- Kernel applies `slide_amount` (random % files[0].sf_slide) to EVERY
  sms_address → dyld submits UNSLID header addresses; slide=0 → as-is.
- Kernel-side checks (vm_unix.c shared_region_map_and_slide_setup): file on
  root/preboot volume, CS coverage, vnode owner/root-dir, alignment,
  KERN_NO_SPACE for out-of-region. Errnos: EPERM/EINVAL/EFAULT/ENOMEM.

## dyld flow (thin offsets; from full-analysis + fresh IDA reads)

`loadDyldCache` 0x34240 → `mapSplitCacheSystemWide` 0x352bc:
- 0x34268 `B.NE` guards private-vs-systemwide; 0x34298 `BL reuseExistingCache`
  = PRE-reuse (accepts iOS cache by magic alone!) — PATCH `MOV W0,#0` to force
  syscall path. Post-syscall reuse call at 0x356d8 is SEPARATE (keep it —
  it fills results->loadAddress via check_np+strcmp).
- 0x3538c `LDR W28,[X19,#0x1A8]` = numFiles (2). `MOV W28,#1` = main only.
- files[] entries = per-subcache {fd,count,slide} + trailing
  `{sf_fd=-1, count=1, slide=0}` = **DynamicRegion pseudo-mapping**:
  sms_address = header.sharedRegionStart(0xe0)+dynamicDataOffset(0x1f0)
  = 0x2ac75c000 (OUT OF 4GB iOS REGION — always KERN_NO_SPACE);
  sms_size = DynamicRegion::size(); **sms_file_offset = userspace ptr to
  DynamicRegion buffer** (kernel copies content from it); prots=0x100000001
  (R/R); slide fields 0.
  - Computed at 0x35fc8-0x35fd8 in preflightCacheFile tail:
    `LDR X9,[hdr+0x1f0]; LDR X10,[hdr+0x1f8](size?); LDR X11,[hdr+0xe0];
    ADD X9,X9,X11; STP X9,X10,[record+0x1B0]`. Read later at 0x35660.
  - `DyldSharedCache::dynamicRegion()` accessor at 0x50dfc:
    `LDR X8,[X0,#0x1F0]` then `this+X8` → must be relocated CONSISTENTLY
    with the submission patch (both to same offset). Returning NULL →
    fileId stays 0 → ctor `halt`. Don't NULL it.
- Error path: 0x356dc `CBZ W23,0x356f4` (syscall ok) / `TBNZ W0,#0→0x35710`
  (reuse ok) / else `LDR X8,[X20,#0x10]` errorMessage — **if non-NULL the
  native error path at 0x35754 is SKIPPED** (returns 0 at 0x356ec). My
  earlier tramp at 0x35754 only fires when errorMessage==NULL.
- Success print "mapped dyld cache file system wide" gate: `0x35700 B.NE`
  (options+6 != 1). "re-using existing shared cache (%s)" gate: `0x35270
  B.NE`. "mapped cache does not contain dynamic cache info" (0x35298) is
  UNGATED — fires when dynamicRegion()==NULL but still returns 1.
- `reuseExistingCache` 0x351a8: `check_np(&p)` → `strcmp(p,"dyld_v1  arm64e")`
  → slide → dynamicRegion() → getDyldCacheFileID → ret 1. Magic-only check =
  why iOS cryptex cache gets reused.
- errno global = `0xa9b10` (`_errno`, written by `cerror_nocancel` @0x2d64:
  `STR W0,[errno]; MRS TPIDRRO_EL0; STR W0,[[tls]+8]`). Read it via
  `adrp`+`ldr` (PC-relative, ASLR-safe).
- `console()` = dyld4::console @ **0xa2f4** — printf-family, callable from a
  tramp via adrp+add+blr (LR clobber OK if you save/restore it; re-do
  `pacibsp` in tramp if you overwrite one).
- NOP cave: thin `0x38d08`-`0x38d3c` (56B; 0x38d40 starts a real function).
  NOTE: `dyld_15.6.1_arm64e_thin.i64` IDB is **contaminated** — it shows my
  old `B 0x38d08` at 0x35754 as if native (clean file has `MOV W0,#0`).
  Trust the device pristine copy for raw bytes there.

## Patch recipe under test (thin offsets; verify bytes post-signing)

| thin | patch | purpose |
|---|---|---|
| 0x76270 | `e0031faa` (mov x0,xzr) | crossarch_trap svc→0 (iOS nosys) — REQUIRED |
| 0x34298 | `00008052` (mov w0,#0) | skip PRE-reuse → force syscall path |
| 0x3538c | `3c008052` (mov w28,#1) | files_count=1 → drop .01 subcache |
| 0x35fc8 | `0940afd2` (movz x9,#0x7a00,lsl#16) | dynregion submit addr → 0x1fa000000 |
| 0x50dfc | `0840afd2` (movz x8,#0x7a00,lsl#16) | dynamicRegion() offset → +0x7a000000 |
| 0x35700 | `1f2003d5` (nop) | diagnostic: ungate "mapped...system wide" |
| 0x35270 | `1f2003d5` (nop) | diagnostic: ungate "re-using (%s)" |

## 2025-XX session 2 — errno CONFIRMED + dead ends ruled out

**Syscall #536 errno = 40 = EMSGSIZE** (or kern_return_t 40=KERN_LOCK_OWNED —
the BSD wrapper returns `kr` raw, both readings possible). Source: iOS-16.3
CLOSED-source branch of `shared_region_map_and_slide_2_np` — not present in
xnu-8792 open source (which only produces EPERM/EINVAL/EFAULT/ENOMEM/E2BIG).
Verified via exit-probe: 6/6 stable rc=40 from `0x35698` (`CMP W0,#-1 → B.NE;
ADRP X8,#0xa9; LDR W0,[X8,#0xb10]=errno; exit(W0)`). The stub does run
`cerror_nocancel` → errno := kernel ret → W0=-1.

**Exit-probe methodology — hard rule discovered:** `exit(N)` placed BEFORE the
syscall returns (0x352bc entry / 0x3533c / 0x35680) reliably yields **137**
(SIGKILL), while exits AFTER the syscall (0x35698) work fine. i.e. the kernel
kills a task that exits while its shared-region attach is mid-flight —
early-exit probes are useless; only probe ≥ 0x35698 (or post-syscall sites).

**Intermittent 137s are environmental** (iOS-cache prebind timing / amfid),
not patch content — same file alternates 137/40 across runs, then stabilizes.
Retest before attributing.

**`deallocateExistingSharedCache` (check_np(0)) is a DEAD END:** calling it
from `mapSplitCacheSystemWide` (0x3533c tramp → cave → BL 0x3420c) kills the
task (137, consistent). Reason: `vm_shared_region_remove` rips the nested-pmap
region out of the task mid-exec. Also pointless: after detach,
`map_and_slide` gets `no shared region → EINVAL` — the region is created
once at exec by `vm_shared_region_enter`, cannot be recreated.

**Region geometry (xnu-8792):** `SHARED_REGION_BASE_ARM64=0x180000000`,
`SIZE=0x100000000` (fixed 4GB, not per-cache). macOS 15.6.1 cache: main
0x180000000–0x2255dc000 (fully in bounds), `.01` 0x22560c000–0x27dfd8000+
(TAIL CROSSES 0x280000000 — partial problem), dynamic region 0x2ac75c000
(fully out). With files_count=1+dynrelocate all submissions are in-bounds
yet EMSGSIZE persists → the rejection is NOT bounds; it's an iOS-specific
check (probably cache-identity/UUID-vs-boot-cache or file-set composition).

**iOS kernelcache extracted for RE:** device
`/private/preboot/CFD92CED…/System/Library/Caches/com.apple.kernelcaches/
kernelcache` (IMG4, 21.8MB) → decompressed via `pyimg4` (bvx2/LZVN payload)
→ `/tmp/kc_raw.bin` = 76MB arm64e Mach-O kernelcache (T8112 — device is M2,
iPad13,11). **Load this in IDA to find the EMSGSIZE return site** in
`shared_region_map_and_slide_2_np` (sysent[536]).

**Full 4.9GB 15.6.1 dyld cache staged on host:**
`/Users/ciscohe/Desktop/dyld-cache-15.6.1/` — main `dyld_shared_cache_arm64e`
(2712764416 B) + `.01` (2203500544 B), verified complete vs device sizes.
Deliberately outside the repo + outside /tmp. Use `dsc_extractor` or
`misc/extract_dyld_cache.py` against this pair if library bodies needed.

## Results so far (this session)

- 5-patch config (first five above, post-reuse intact): child prints
  `Mapping the shared cache system wide` → `dyld cache '(null)' not loaded:
  syscall to map cache into shared region failed` → dyld FALLS BACK to
  on-disk mmap (`Kernel mapped /usr/bin/true`, `Mapping
  /usr/local/lib/libmachook.dylib`, `Mapping /usr/lib/libSystem.B.dylib`) →
  `libdyld.dylib not found` → rc=0. **syscall still fails** even with
  files_count=1 + dynregion relocated.
- errno still unconfirmed — tramp-on-error-path approach was flaky because
  (a) errorMessage may already be set (skip path), (b) bss-write tramp
  crashed 139, (c) stub-level redirect + console gave 137 (was actually
  trustcache/inode staleness, not the tramp).
- Next step: **freestanding probe `misc/sprobe.c`** (already written &
  compiled — static arm64e Mach-O, raw SVC, no dyld needed). Staged tests:
  A check_np state; B main-cache-only map; C +relocated dynregion;
  D check_np(0) dealloc then retry; E dynregion at original out-of-bounds
  addr. Prints errno for each → pins down WHICH check fails.
  - sprobe fixes applied: check_np syscall 464→294; staged tests added.
  - Deploy: `ldid -S`, `jbctl trustcache add`, rm+cp into
    `/var/mnt/rootfs/usr/bin/sprobe`, run via launchdchrootexec.
  - First run gave 137 — most likely trustcache add raced/silent-fail OR
    stale inode. RETRY with verified `jbctl trustcache info | grep`.

## Exit-code legend

- 0   = ran to completion (may still be DEGRADED — disk fallback when no
        cache; distinguish via prints, not rc)
- 132 = SIGILL (real illegal instr / PAC failure — NOT brk)
- 133 = SIGTRAP = `brk #0` fired (bisect marker)
- 134 = SIGABRT (dyld graceful abort/halt)
- 137 = SIGKILL: CS/trustcache/exec-policy — silent, NO .ips. If it appears
        right after a rebuild: check trustcache add actually landed AND the
        dest inode was replaced (rm then cp, not cp-overwrite).
- 138 = SIGBUS
- 139 = SIGSEGV
- 140 = SIGSYS
- **WARNING: piping the launcher into `awk`/`tail` eats `$?`** — rc then
  reports the filter's exit. Always `echo $?` on the raw command.

## Confirmed root causes (unchanged)

1. iOS 16.3 `SHARED_REGION_SIZE_ARM64=0x100000000` = region
   0x180000000-0x280000000; macOS 15.6.1 cache extent to 0x2ac760000.
   .01 subcache tail + dynamicData region exceed it → KERN_NO_SPACE.
2. DSC file CS-enforced — editing header (subcache count) kills it for
   that inode permanently. Can't patch DSC.
3. Private path `mapSplitCachePrivate` 0x342dc = plain mmap of exec DSC
   pages → CS kill 137. Dead end.
4. iOS-cache prebind + magic-only reuse → false-success variance.

## Original-project tools — what they're for (see tools-and-porting doc)

- `misc/sprobe.c` — this task's probe (freestanding, raw SVC).
- `launchdchrootexec` — chroot launcher (sets MACWS_CHROOT_HOST_ROOT,
  DYLD_INSERT_LIBRARIES for libmachook, MACWS_SUSPEND_* for lldb).
- `misc/chroot_then_exec.c`, `misc/chroot_isolation_test.c` — chroot probes.
- `misc/loadtc`, `misc/vtool_and_sign.sh`, `autosignd/` — sign+trustcache.
- `misc/extract_dyld_cache.py` — DSC header/images parser.
- `misc/disasm_remote_dyld_range.sh` — device-side dyld byte dumps.
- `misc/lldb_*` — scripted lldb attach/breakpoints (incl. `MACWS_SUSPEND_AT_EXEC`).

## IDA MCP

- Server `ida-pro-mcp-Instance1`. IDB `dyld_15.6.1_arm64e_thin.i64` —
  CONTAMINATED at 0x35754/0x38d08 by old tramp; pristine bytes live at
  device `/tmp/usr/lib/dyld` (+0x100000 delta).
- `py_eval` arg = `code`. Use `idc.GetDisasm`/`ida_funcs`/`idautils.Strings`.
- Full analysis (mostly valid modulo contamination):
  `docs/porting/dyld-15.6.1-full-analysis.md`.

## Build/test commands

```bash
# verify arm64e slice delta on device file:
python3 - <<EOF
import struct;d=open('/var/mnt/rootfs/usr/lib/dyld','rb').read(64)
for i in range(struct.unpack('>I',d[4:8])[0]):
 o=8+i*20;ct,cs,off,sz,al=struct.unpack('>IIIII',d[o:o+20])
 if cs==0x80000002: print(hex(off))
EOF
# sign + trustcache + deploy (FRESH INODE):
ldid -S /tmp/dyld_x
for h in $(ldid -h /tmp/dyld_x|grep '^CDHash='|cut -d= -f2); do jbctl trustcache add $h; done
jbctl trustcache info | grep -i <hash>   # VERIFY it landed
rm /var/mnt/rootfs/usr/lib/dyld && cp /tmp/dyld_x /var/mnt/rootfs/usr/lib/dyld
# run:
/var/jb/usr/macOS/bin/launchdchrootexec 0 0 /var/mnt/rootfs /usr/bin/true
```
