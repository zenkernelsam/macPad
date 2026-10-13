# HANDOVER: m3 write fault root-cause — dyld_pager + pointer_format 13 (2026-09-30)

## TL;DR

The m3 write fault (`STR X20,[X0]`, FAR `0x1ee188000`, `KERN_MEMORY_ERROR=10`) is on a
region backed by the **in-kernel `dyld_pager`** installed by syscall 550
(`__map_with_linking_np`). The live pager's link_info blob has
`version=7, page_size=0x4000, pointer_format=13` — i.e. macOS 15.6.1 dyld submitted
`DYLD_CHAINED_PTR_ARM64E_SHARED_CACHE` (=13), a format this iPadOS 16.3 kernel
(xnu-8792.82.2) does not handle (its `dyld_pager_data_request` fixup switch covers
formats 1–12 only; 13 falls to `default: printf("unknown pointer_format %d")`).

**Open inconsistency:** that printf never appears in the kernel msgbuf ring, so the
format dispatch was not reached — the failure happens earlier inside
`dyld_pager_data_request` (UPL request or inner `vm_fault_page` on the backing
object) or in the vm_fault copy machinery before the pager request. All of those
exits are silent (ktriage only; the triage ring is not enabled on this device).

## Evidence chain (all runtime-confirmed this session)

Child held frozen at exception via `RUN_DBG_HOLD=90` in `run_dbg_hold_v2`;
`vmwalk.py` (KRW, read-only) walked `proc→task(+0x538)→vm_map(task+0x28, PAC-stripped)
→entry`:

```
entry [0x1ee188000..0x1ee1ac000] is_sub=0 offset=0 prot=3/3 perm=0
[0] obj internal=1 shadow→[1]  pager=0      (flags 0x00390000)
[1] obj internal=1 shadow_offset=0x6c188000 shadow→[2]  (flags 0x01390000)
[2] obj internal=1 shadow→[3]  copy→[3]    (flags 0x01390000)
[3] obj internal=0 pager=mo@… ops==DYLD_PAGER_OPS, copy→[2] (flags 0x08387800)
    dyld_pager: is_mapped=1 is_ready=1
    dyld_backing_object=0xfffffe10f59e4000 (VNODE_PAGER, paging_offset=0,
        resident page@off=0 healthy)
    link_info=0xfffffe14dd9dc000 size=0x6940
    link_info: version=7 page_size=0x4000 ptr_format=13
```

- Exception registers (deterministic across runs): `type=1 code0=0xa`,
  `far=0x1ee188000 esr=0x92000046` (write fault), `x0=0x1ee188000`,
  `x20` = `__dyld_apis` contents pointer, csflags `0x26803b0d`,
  **`pagein_error=0`** (ARM_PAGEIN_STATE flavor 27 via thread_get_state →
  vnode pager never errored).
- Kernel msgbuf ring (16KB, dumped immediately at fault via `msgdump` C helper):
  **zero occurrences** of `pointer_format|dyld_pager|No segment|seg_info` —
  data_request format dispatch not reached.
- Prior experiments (same session family): native `--copy --write` on the same
  file range+csflags+`shadow=3` object shape succeeds; fixed-VA COPY+write
  succeeds — so csflags and COW chain shape alone do NOT explain the fault.

## Key offsets used (xnu-8792, verified against dumps)

```
proc: task = proc + 0x538 (embedded struct, not pointer)
task: vm_map = PAC-ptr @ +0x28 (strip: 0xffff800000000000 | (v & 0x7fffffffffff))
vm_map: links.prev@+0x10 next@+0x18 nentries@+0x30
vm_map_entry (stride 0x50): prev@0 next@8 start@0x10 end@0x18 store@0x20..0x30
    ctx u32 @0x38 (bit1=is_sub_map; if set, +0x38 u64 = submap<<2|flags)
    obj_packed u32 @0x3c → obj = (packed<<6) + 0xfffffe0000000000
    @0x40 u64: [alias:12][offset:52] → offset=(w>>12)<<12
    @0x48 flags u64: prot@7-9, maxprot@11-14, permanent@19, tpro@10, pmapcs@24
vm_object: memq@+0x00(packed next/prev) lock@+0x08..0x18 vou_size@+0x18
    ref@+0x28 resident@+0x2c wired@+0x30 reusable@+0x34
    copy@+0x38 shadow@+0x40 pager@+0x48 shadow_offset@+0x50 paging_offset@+0x58
    pager_control@+0x60 copy_strategy@+0x68 flags bitfield@+0x74
      (all_wanted:11, pager_created@11, init@12, ready@13, trusted@14,
       can_persist@15, internal@16, private@17, pageout@18, alive@19,
       purgable:2@20-21, shadowed@24, true_share@25, named@27,
       shadow_severed@28)
    2nd flags u32 @+0xa8: wimg@0-7, code_signed@8, blocked_access@15,
      object_is_shared_cache@17, ...
memory_object (pager kobject): mo_pager_ops @ +0x08
  known ops (IDB VA + kernel slide 0x15948000):
    dyld_pager_ops  IDB 0xfffffe00078e8820 → runtime 0xfffffe0007a41020
    vnode_pager_ops IDB 0xfffffe00078e7c30
dyld_pager struct (memory_object at +0x00, size 0x18):
    is_mapped@+0x18(B) is_ready@+0x19(B)
    dyld_backing_object@+0x20  dyld_link_info@+0x28  dyld_link_info_size@+0x30
mwl_info_hdr: version@0(u32) page_size@4(u16) pointer_format@6(u16)
    binds_offset@8 binds_count@0xc chains_offset@0x10 chains_size@0x14
    slide@0x18 image_address@0x20
vm_page: vmp_pageq@0 vmp_offset@0x18 vmp_object@0x20(packed) flags2@0x2c
      (busy@0 absent@10 error@11 unusual@17 cs_val@18-21 cs_taint@22-25 cs_nx@26-29)
```

## Fault-site facts

- Faulting instruction `dyld+0x79f8` = `STR X20,[X0]` in `prepare()` storing the
  `__dyld_apis` pointer into `__TPRO_CONST,__dyld_apis` (a TPRO CONST_DATA page,
  cache m3 `flags 0x44 = CONST_DATA|CONST_TPRO_DATA`).
- m3 slide-info: v5 blob, 16KB pages, 9 pages — mappingWithSlide table present
  (earlier "no slide info" claim in old docs is WRONG — corrected).
- `DYLD_PAGEIN_LINKING` env is NOT honored: `internalInstall()` gates it behind
  `__csrctl(0,&16,4)` which fails on production → default mode 2 (page-in
  linking) → dyld calls syscall 550 whenever cache conditions allow.

## What this means / candidate fix

The kernel dyld_pager path cannot fix up format-13 chains. dyld's alternative is
**in-process fixups**: for `!canUsePageInLinking`, dyld falls back to
`vm_protect(VM_PROT_READ|WRITE|COPY)` for CONST_DATA and `withWritableMemory` for
TPRO — pure userspace operations this kernel supports (native COPY+write control
proved the file range is writable).

→ The minimal real fix candidate is a **dyld-level switch to the in-process
fixup path** (e.g. make `canUsePageInLinking()`/the 550 precondition check return
false for this cache), NOT a kernel patch. Verify first that the write fault
truly dies before fixup (see "next decisive test"), and that m4/m5 don't hit a
different wall.

## Next decisive test (designed, not yet run)

`misc/mwl_repro.c` — iOS task calls `syscall(550, regions, 1, link_info, size)`
directly (svc 0x80, x16=550) to over-map an existing RW mmap of the cache file's
`0x6c188000` range, then READ/WRITE it:
- Build minimal `mwl_info_hdr` v7 + one `dyld_chained_starts_in_image`
  {seg_count=1, seg_info_offset[0]=8} + `dyld_chained_starts_in_segment`
  {size, page_size=0x4000, pointer_format=13, segment_offset=0,
   page_count=N, page_start[]={0xFFFF…}}; `mwli_image_address`=map VA.
- If read SIGBUSes AND ring shows `unknown pointer_format 13` → format dispatch
  is reachable → in macOS task it died earlier (COW layer). If it works →
  format 13 may be handled elsewhere; revisit.
- Note `p->p_disallow_map_with_linking` must be 0 (fresh proc OK); file needs
  `ubc_cs_is_range_codesigned` coverage (cache file has its blob registered by
  cachereg — keep it running).
- Regions: `mwlr_fd`, `mwlr_protections` (R|W; no ZF/EXEC), `mwlr_file_offset`,
  `mwlr_address` (existing VA), `mwlr_size`.

## Device-state regressions (fix before continuing)

1. **dyld cache files are gone**: `find /var /tmp` finds no `dyld_shared_cache*`
   >10M; rootfs symlinks `/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e{,.01}`
   point at `/tmp/dsc/` which is now empty → earlier experiments this session ran
   while files existed; they were lost (likely /tmp cleared). Redeploy the two
   cache files + recreate `/tmp/dsc` links before any dyld experiment.
2. **python3 `jbclient_process_checkin` now crashes** (worked earlier today);
   C-signed helpers (msgdump, triageread) still work — prefer C tools via
   `run_nocskill.entitlements.plist` signing + `jbctl trustcache add <cdhash>`.
3. kdebug triage ring NOT initialized — `ktriage_record` writes nowhere; the
   `INFO_G=0xfffffe000aa540a8+slide` bufinfo array reads as pointer garbage
   (buffer unallocated). Don't rely on ktriage evidence.

## Tool inventory (device)

- `/var/mobile/run_dbg_hold_v2` — run_dbg + ARM_PAGEIN_STATE dump +
  `RUN_DBG_HOLD=<sec>` freezes child at exception (no reply) for KRW autopsy.
- `/var/mobile/vmwalk.py` — task→map→entry→object-chain walker (needs working
  python checkin — currently broken; port to C if needed).
- `/var/mobile/msgdump` — kernel msgbuf ring dumper (16KB ring; pass slide).
- `/var/mobile/triageread` — kdebug triage ring dumper (dead end: ring
  unallocated when kdebug disabled).
- `/var/mobile/dyld_cli_autopsy_v1.sh` — orchestrator: backup dyld → swap
  diagnostic `dyld_emptysr_highreserve.bin` → spawn chrooted `/bin/echo HI`
  under run_dbg → vmwalk + msgdump at fault → trap-restore → SHA/inode verify.
  **Stale-marker hygiene**: before rerun, rm
  `/var/mobile/dyld_cli_autopsy_v1_{pre.bin,out,raw}` and
  `/var/mnt/rootfs/usr/lib/.dyld_cli_autopsy_v1_*`.
- Original dyld SHA-256: `9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`,
  inode `245791518`; diagnostic build `3f2048c9303464f673c8df2a661748a642175415c95ace31b3ff7f91a6937062`.

## Do NOT forget

- `/bin/echo HI` has NOT printed through the real macOS cache path — the CLI
  milestone is not met.
- The dyld_pager printf list (all unconditional): `No segment for user VA`,
  `unknown pointer_format %d`, `seg_info out of bounds`, `chain out of range`,
  `out of range bind ordinal`, `Invalid ptr auth key`, `Range not found for
  offset`. Absence in ring during a held fault is strong evidence data_request
  exited before fixupPage OR was never called.
- `dyld_pager_data_request` silent exits: `memory_object_upl_request` failure
  (ktriage DYLD_PAGER_NO_UPL), inner `vm_fault_page(backing)` →
  VM_FAULT_MEMORY_ERROR → retval=error_code (0→KERN_MEMORY_ERROR), shortage,
  interrupted.
- vm_fault COW write paths can return KERN_MEMORY_ERROR via
  VM_OBJECT_PURGEABLE_FAULT_ERROR / shadow_severed / no-pager / VMP_ERROR /
  guard-page branches — all ktriage-only, no printf.

---

## Startup prompt for the next session

Paste verbatim (edit paths if the machine changed):

```
You are continuing the macPad project at /Users/ciscohe/Desktop/macPad.

GOAL (unchanged): run a genuine macOS 15.6.1 CLI inside chroot on a jailbroken
iPad13,11 (M1/T8103, iPadOS 16.3, xnu-8792.82.2), using the ORIGINAL macOS dyld
and real macOS shared-cache libraries — no compatibility shims. WindowServer/GPU
is deferred until `/bin/echo HI` actually prints through the real macOS
dyld/cache path. It has not yet.

READ FIRST (in this order):
1. docs/porting/HANDOVER-M3-DYLD-PAGER-2026-09-30.md  (current root-cause state)
2. AGENTS.md — patch discipline + evidence discipline + IDA MCP workflow
3. docs/porting/dyld-15.6.1-state.md — history of prior experiments
4. docs/evidence/m1-dyld-pager-format13-20260930.md

CURRENT BLOCKER: macOS dyld maps the shared-cache m3 segment via syscall 550
(__map_with_linking_np) which installs a kernel dyld_pager whose link_info
requests pointer_format=13 (DYLD_CHAINED_PTR_ARM64E_SHARED_CACHE). The iPadOS
16.3 kernel dispatch table only supports formats 1–12; 13 → "unknown
pointer_format" + failure. The write fault at 0x1ee188000 (STR of __dyld_apis
in dyld prepare()) returns KERN_MEMORY_ERROR, but the format-printf never hit
the kernel ring, so the failure is inside the pager's pre-fixup path or the
COW layer — not yet proven which.

DEVICE STATE (fix first): the dyld cache files under /tmp/dsc are GONE
(find shows none); rootfs cache symlinks are dangling. python3 KRW checkin
crashes — use the C-signed helpers (msgdump, triageread, run_dbg_hold_v2) or
port vmwalk.py to C. Device SSH: root@192.168.64.1:2222, sshpass auth.
Sign new device binaries: ldid -S/var/mobile/run_nocskill.entitlements.plist -M
then jbctl trustcache add <CDHash from ldid -h>.

IDA MCP (3 live servers): Instance1=kernel IDB (imagebase 0xfffffe0007004000,
runtime slide was 0x15948000 last boot — rescan needle d503237f a9be4ff4
a9017bfd 910043fd at IDB 0xfffffe0007f18000, 4KB-step scan since slide is NOT
2MB-aligned), Instance2=dyld_15.6.1_arm64e_thin, Instance3=amfid.

NEXT STEPS (priority order):
1. Redeploy the two dyld_shared_cache files to the device and recreate /tmp/dsc
   symlinks; verify the rootfs cache path resolves again.
2. Build + run the controlled syscall-550 repro (mwl_repro design is specced in
   the handover §"Next decisive test"): determine whether dyld_pager_data_request
   reaches the format-13 dispatch (ring printf) or dies earlier silently.
3. Pick the real fix: force dyld onto the in-process fixup path (mwl/550 skipped)
   — the leaf alternatives are vm_protect COPY + withWritableMemory which are
   pure-userspace and already proven working — OR implement/patch format-13
   fixup in kernel (heavier; only if the in-process path hits a wall).
4. Then continue past m3 to m4/m5 and the rest of dyld init until
   `/bin/echo HI` prints via genuine macOS libSystem from cache.

DISCIPLINE: no NOP/branch-force/return-stub "fixes" (see AGENTS.md). Every claim
needs runtime evidence (registers/VM objects/msgbuf) or IDB disasm citation.
After ANY experiment that swaps the device dyld, restore original file and
verify SHA-256 9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1,
inode 245791518.
```

