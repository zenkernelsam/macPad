# macOS 15.6.1 dyld shared-cache bring-up — live state

**READ THIS FIRST after context loss.** Active task: get dyld to map the
macOS 15.6.1 shared cache on iPadOS 16.3 (xnu-8792.82.2) so macOS binaries
run in chroot. This file is the single source of truth — update it whenever
a fact/offset/result changes, BEFORE context is lost.

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
