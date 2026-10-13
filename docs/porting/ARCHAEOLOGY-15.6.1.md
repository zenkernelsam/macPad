# Archaeology: the macOS 15.6.1 / 24G90 roots (2026-09-24 → 10-04)

The 15.6.1 branch is FROZEN, but it is the deepest RE work in the repo and
its lessons drive today's rules (fresh-inode deploys, rc≠milestone,
kernel-vs-userspace triage). This doc consolidates the era's ~30 handover
files into one narrative. Every section cites its source doc; the era's
own files now live under `docs/porting/archive/macos15-roots/` and their
evidence labels outrank any paraphrase here.

Sources (canonical, in archive): `dyld-15.6.1-state.md` (live log),
`HANDOVER-15.6.1-COMPLETE-BRIEF-2026-10-01.md` (most complete),
`HANDOVER-MACOS15-FROZEN-2026-10-04.md` (final state), `CLI-MILESTONE-
2026-09-28.md`, `CONSOLIDATED-2026-09-29.md`, `KB-E-shared-region-limit.md`,
`T8103-PMAP-NESTED-OWNER-FIX-PROPOSAL.md`, plus the HANDOVER/REPLY chain.

## 0. Goal and environment

```text
milestone:  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI
            printing HI via the REAL macOS 15.6.1 dyld + shared cache
device:     iPad13,11 (M1/T8103), iPadOS 16.3 (20D47), Dopamine rootless
kernel:     xnu-8792.82.2 RELEASE_ARM64_T8103
rootfs:     rsync'd from a live macOS 15.6.1 host (NOT IPSW-extracted);
            dyld cache at System/Volumes/Preboot/Cryptexes/OS/.../dyld/
cache:      dyld_shared_cache_arm64e (2.71 GB) + .01 sub-cache (2.20 GB)
            total span 0x180000000..0x2ac75c000 = 4.77 GB
            vs iOS SHARED_REGION_SIZE_ARM64 = 4 GB
```

## 1. The adoption discovery (09-26/27)

The first fact that shaped everything: a chrooted process gets the **iOS
shared region attached at exec** (`__shared_region_check_np`, syscall 294,
returns base `0x1A4AE8000`). macOS 15.6.1 dyld's `reuseExistingCache` then
*adopts* the iOS cache (matching `dyld_v1 arm64e` magic) and successfully
loads ~90 iOS dylibs. Upstream's design never remapped the shared cache —
the README admits only a GradedArchs arm64e→arm64 dyld patch, i.e. the
author intended adopt-iOS-cache + disk-load macOS-only libs.

## 2. The exec-admission wall (09-26..27)

Bare macOS binaries died `rc=137` with zero dyld output; BRK-at-entry probes
proved **neither dyld's nor echo's first instruction ever ran** — the kill
was kernel exec admission, not userspace. The recipe that opened it (and
still opens every binary today):

```text
ldid -Hsha256 -S<project-entitlements.plist> <file>     (×2)
jbctl trustcache add <new CDHash>                        (each slice)
```

Plain `-S` or Apple-entitlement-merged signatures get killed at exec.
Side findings that survived: static arm64e Mach-O can never exec (kernel
veto, SIGKILL 137); LC_MAIN args arrive in x0-x3 registers; Darwin arm64
`fork` marks the child via **x1=1**, not x0==0; `| head` masks real rcs —
measure the command's own rc.

## 3. Syscall 536 saga — two distinct bugs (09-27..28)

`__shared_region_map_and_slide_2_np` (syscall 536) populates a shared
region from a dyld cache. It returned EINVAL for two different reasons,
solved separately:

1. **slide-info version 5 > kernel max 4** — kernel IDA
   `sub_8062CA8 @ 0xfffffe0008063024`: `if ((version-1)>3) → KERN_FAILURE`,
   mapped to EINVAL(22) at `bsd/vm/vm_unix.c:2725`. Physical proof:
   `xxd -s 0x7ad4c000 dyld_shared_cache_arm64e` = `05 00 00 00`.
   Triggered only when `sms_max_prot & VM_PROT_SLIDE(0x20)`; masking that
   bit on all 9 mappings → **536 returns 0** and the cache really maps.
   Secondary blocker: cache `page_size=16K` vs kernel 4K.
2. **`files[].sf_slide` not 16K-aligned** — the second EINVAL after the
   first was fixed; realignment made the iOS cache populate a chroot's
   shared region (the "536 打通" run).

A mapped region persists until reboot: the first successful mapping fills
it and later processes reuse it (the "keeper" problem).

## 4. The shim-era CLI milestone (09-28) — and why it didn't count

`CLI-MILESTONE-2026-09-28.md`: `echo HELLO`, `cat`, `sh -c` chains all
passed — but through a **disk libSystem shim** (34 KB dsc-extracted stub
re-exporting enough symbols), never the real cache. The milestone rule
came from this: a shim `HI`, a surviving process, or a clean rc are not
the milestone — only real-cache libSystem resolving the binary's deps.
This is why three different "echo" witnesses exist in the history (see
PROJECT-ARCHAEOLOGY.md).

## 5. The 4.77 GB > 4 GB layout saga (09-28..10-01)

The cache spans `0x180000000..0x2ac75c000`; iOS grants 4 GB. The long
middle of the era fought this:

- **Header rewrites** (6 variants: `sharedRegionSize`, `subCacheArrayCount`,
  `dynamicDataOffset`, …) — all ineffective. `STATIC-cache-layout…` doc.
- **Rebuild a ≤4 GB cache** — dead end: `SharedCacheLinker` source depends
  on ld64-internal `ld/` headers absent from the open-source drop;
  `update_dyld_shared_cache` on device is a 16 KB stub.
- **`DYLD_SHARED_REGION=private`** — same EXC_GUARD; **`=avoid`** — ignored
  except on simulator (`DyldProcessConfig.cpp:1394` comment).
- **`DYLD_SHARED_CACHE_DIR`** — supported in source, but the probe was
  unreliable: `DYLD_PRINT_*` output is suppressed in this environment, so
  env-based A/B could not prove the cache was or wasn't skipped.
- **The EXC_GUARD chain** (RE-complete, file:line cited in the brief):
  `vm_map_delete` hits a gap → `VMDS_FOUND_GAP` →
  `vm_map_guard_exception(kGUARD_EXC_DEALLOC_GAP)` →
  `task_exc_guard & VM_FATAL` → `task_bsdtask_kill`. The
  `VM_MAP_REMOVE_GAPS_FAIL` escape flag is set only by `vm_reclaim.c:600`
  — never reachable from userspace mmap.
- **`task_exc_guard` offset = `task+0x5C4`** (KRW cross-validated:
  kernel_task=0x00, launchd=0x53, our proc=0x99); the per-task value is
  seeded from `task_exc_guard_default` global @ IDB `0xFFFFFE000A9FABE0`,
  third-party byte = `(default>>8)&0xFF`.
- **E2 experiment**: `kwrite32(task_exc_guard_default, 0x90)` then exec →
  `EXIT=137` became `EXIT=90` — the guard stopped killing. First read as
  "progress", then correctly retracted: **90 is the generic chroot-exec
  failure code of that device state**, not a new failure point. The
  stderr blob (`DF/AN/FL/TD/F2/HG/HA` tags) was likewise first misread as
  a failure marker, then identified as a jailbreak-injection diagnostic
  present on *every* exec, including working ones.

## 6. The October 1 twist — the patch was the bug

`misc/post_reboot_cli_test.sh` reported `PATCH_STATE=BAD` — i.e. the
device was running the **original, unpatched** 4.77 GB cache. On that
state:

```text
DYLD_PRINT_LIBRARIES=1 run_dbg_hold_v2 chroot rootfs /bin/echo HI
  -> 131 images loaded (all from the real shared cache)
  -> GUARD_2ac75c000_HITS = 0   (zero EXC_GUARD)
  -> VERDICT = FIXED-please-verify-HI-printed
```

Meanwhile a logged run with the **patched** cache showed
`GUARD_2ac75c000_HITS=1` — the 4 GB layout patch itself produced the
guard kill the whole "cache too big" narrative was built on. The
remaining failure under original cache was **not** a crash: initializers
ran in order and **hung inside the jailbreak-injected ElleKit dylib**
(`/private/preboot/…/dopamine-…`), the last initializer logged before
silence — the injection enters every chrooted process, and `HI` never
printed because the process never got past it.

## 7. The final wall — PMAP nested-owner (10-01..04)

The last series of runs (different trust/debug context than the Oct-1
hang) converged on the canonical final fault:

```text
fault VA 0x18047dc9c   code0=0x32 (KERN_CODESIGN_ERROR)   pagein_error=0
```

Page bytes, CodeDirectory slot[287], SHA-256, page validation, outer
PMAP-CS association, trust=8, and the 24G90 CDHash all verified **correct**
— the failure is below every checkable layer. T8103 kernel IDA pins the
selection:

```text
sub_FFFFFE00086A0924 picks a PMAP owner for a shared-region VA using
nested_region_addr/size only — it never consults
nested_region_asid_bitmap. The fault routes to an empty nested pmap
and the PPL path returns 50 (0x32).
```

`vm_shared_region_remove()` had already unnested the twig (association-
tree root = 0), yet the fault still routes nested — a kernel/PPL design
gap on T8103, not something a userspace patch can fix.

Attempts that all confirmed the boundary: `emptysr+highreserve` (crossed
the high-address guard, hit the main-cache data fault), TPRO removal,
MAP_SHARED, CodeSignature retention, child-scoped
`pmap+0xc9=3`/`nested_pmap=NULL`/`nested_region_size=0` writes (all
accepted, all still code0=50). Direct kernel-text write at
`0xfffffe002de1c934`: **blocked by PPL/KTRR** — byte unchanged.

**Offline escape also failed**: the candidate
`0xf9402c08 (LDR X8,[X0,#0x58]) → 0xd2800008 (MOV X8,#0)` was repacked
into an IMG4 of original size, but the IM4P SHA-384 differs while the
IM4M `krnl` DGST is unchanged — cryptographically unbootable. No
PongoOS/kexec/signing path exists in the workspace. Commits `c51a7d5`,
`6e782f7`, `94f74c0`, `3a43504`, `96c5a50` record the boundary.

## 8. Why the branch froze — and what would unfreeze it

Frozen on 10-04 (`HANDOVER-MACOS15-FROZEN-2026-10-04.md`) because the
remaining wall is a **signed-kernel defect**: fixing it needs either a
custom boot chain (none available on Dopamine rootless) or an Apple fix.
The honest state of knowledge:

- Everything userspace-checkable was proven correct; the fault is PPL-side.
- The "cache too big" root cause is real (4.77 > 4 GB) but the original
  cache demonstrably loads all 131 images — layout is not the immediate
  killer; the codesign fault at first text page is.
- The ElleKit-injection initializer hang is a separate, cheaper blocker
  worth removing if the branch ever resumes (§12.7 of the brief:
  `DYLD_INSERT_LIBRARIES` hygiene / `exec_hooks.c` normalization).

## 9. Lessons the era left (all still load-bearing)

| lesson | where it came from |
|---|---|
| fresh-inode install for signed mapped files | "rm then cp" ops note, era 1; re-confirmed catastrophically 10-13 |
| rc=0/survival ≠ milestone; only the literal witness counts | shim-era echo vs real-cache echo |
| `| head` masks the real exit code | static-exec probes |
| LC_MAIN args in x0-x3; fork child flag in x1 | msh bring-up |
| static arm64e Mach-O never execs | kernel veto, SIGKILL 137 |
| an EXIT code can be the *generic failure* code — compare against a known-good baseline before reading meaning into it | E2's `137→90` misread, retracted in brief §10.3 |
| a probe's own binary can poison the experiment (jailbreak injection loads everywhere) | stderr-blob attribution, 3 retractions |
| patched-state evidence can misattribute the cause to the original | PATCH_STATE=BAD → zero guards |
| `DYLD_*` env debugging is unreliable inside the chroot | setUpLogging SIGBUS + suppressed prints |
| kernel writes are PPL/KTRR-blocked on T8103; IMG4 repack fails DGST | final wall |
| negative results go in the failure table so they're never retried | B1/B2'/T4/T5/D③/E1/E2 table |

## 10. Artifacts (all under `docs/porting/archive/macos15-roots/`)

`dyld-15.6.1-state.md` — the era's live log (append-only, ~2.9k lines)
`dyld-15.6.1-full-analysis.md`, `dyld-15.6.1-deep-re-handover.md`,
`re-analysis-15.6.1.md` — RE writeups
`kernel-syscall536-finding.md`, `kernel-syscall536-re-handover.md`,
`syscall536-errno-and-probe-sigill.md` — the 536 saga
`STATIC-*.md`, `KB-E-shared-region-limit.md`,
`T8103-PMAP-NESTED-OWNER-FIX-PROPOSAL.md` — layout + guard + pmap wall
`CLI-MILESTONE-2026-09-28.md` — shim-era CLI results
`patch-ledger.tsv`, `hit-rate-table.md` — port-diff ledger
`rootfs-15.6.1-install.md` — rootfs build procedure
`CONSOLIDATED-2026-09-29.md` — mid-era unified state (with its own
corrections; read its 勘误 header first)
the full `HANDOVER-*.md` / `HANDOVER-REPLY-*.md` chain — the agent-to-agent
dialogue in chronological order
