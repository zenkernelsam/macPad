# M1 private-cache pager investigation: format 13

Status: format-13 incompatibility confirmed AND the m3 fault attribution is now
RE-backed, not THEORY — see `docs/porting/STATIC-m3-dyld-pager-format13.md`
(2026-09-30): the device kernel's reject path is `ktriage_record(0x04000008
DYLD_PAGER_SLIDE_ERROR) + KERN_FAILURE`, and it has NO printf (release kernels
macro-out `printf` strings via `CONFIG_NO_PRINTF_STRINGS`), so "no printf in
the msgbuf ring" carries no information. syscall 550 itself never validates
`mwli_pointer_format`. Real macOS cached-libSystem CLI acceptance is still not met.

## Evidence Captures

- Device: iPad13,11, iPadOS 16.3, xnu-8792.82.2.
- Kernel IDB: `analysis/kc_raw_16.3_T8112.bin` (misnamed T8103 image).
- Dyld IDB: `analysis/dyld_15.6.1_arm64e_thin`.
- Current MCP execution targets: Instance1 is kernel, Instance2 is dyld,
  verified using `ida_nalt.get_input_file_path()`, not instance labels.
- IDA captures: `.ida-mcp/kernel-dyld-pager-format-proof.txt` and
  `.ida-mcp/dyld-cache-format13-proof.txt`; both contain instruction bytes,
  disassembly and full decompilation of the relevant routines.
- Device file metadata: `docs/evidence/m1-cache-slide-metadata-20260930.txt`.

All kernel addresses here are STATIC IDB addresses, not proven live addresses
for kernel reads/writes. This investigation performs no kernel writes.

## Confirmed Metadata Correction

Runtime-confirmed via `m1-cache-slide-metadata-20260930.txt`: the physical
cache file is `/var/mnt/rootfs/private/tmp/dsc/dyld_shared_cache_arm64e`,
size `0xa1b18000`. Its legacy slide offset/size are both zero, but its
`mappingWithSlideOffset=0x3e8`, count 8, contains five nonempty slide blobs.
All five have version 5 and page size `0x4000`.

The m3 entry is:

```text
va=0x1ee188000 size=0x24000 file_offset=0x6c188000
slide_offset=0x7ad4e146 slide_size=0x2a flags=0x44 max=0x3 init=0x1
slide_version=5 page_size=0x4000 page_count=9
header24=050000000040000009000000000000000000008001000000
```

Thus the old inference "zero legacy slide header means this cache has no
slide information" is false. This does NOT establish that slide version 5
caused the historical syscall 536 failure. Syscall 550 is a separate path.
Also, basic mapping entries are 32 bytes, mapping-and-slide entries 56 bytes;
earlier outputs using 40/64-byte strides are invalid, not alternative tables.

## Confirmed Dyld / Kernel Format Mismatch

RE-confirmed via the dyld capture:

- `mapSplitCachePrivate` checks all slid mappings for slide version 5 and
  requires at most five slid regions, in addition to the option gate.
- At `0x349ec..0x349f8` it stores `0x000d400000000007` at the blob start:
  version 7, page size `0x4000`, pointer format 13.
- `0x34b74..0x34b84` submits regions/count/blob/size to
  `___map_with_linking_np`.
- `0x769cc..0x769d0` implements syscall 550 with `X16=0x226; SVC 0x80`.

RE-confirmed via the kernel capture:

- The handler calls `sub_FFFFFE0008066F8C`, which creates the dyld pager,
  retains the file backing object and maps it over the submitted regions.
- The pager operations table at `0xfffffe00078e8820` identifies data_request
  as `sub_FFFFFE00080661A4`.
- At `0xfffffe00080667e8..0xfffffe00080667f4`, it reads the blob's format
  from offset 6, subtracts 1, compares to 11 and branches to the default
  failure path for values outside 1..12. Even inside that range, only
  selected formats are supported. Format 13 is unconditionally outside it.
- The default path records triage and sets the result to `KERN_FAILURE=5`
  at `0xfffffe000806668c..0xfffffe0008066690`.

Source-level supporting mechanism, NOT runtime attribution:
`vm_fault.c:1891..1901` turns non-interrupted pager data-request failures into
`VM_FAULT_MEMORY_ERROR`. Consequently an observed `KERN_MEMORY_ERROR=10`
does not uniquely imply that the dyld pager's SOURCE file-page fault failed.
Its fixup/format failure can also lead to a memory-error fault.

## Important Unproven Claims

- `external=1,shadow=3,offset=0` does not identify the pager implementation.
  Ordinary `VM_PROT_COPY` remapping is a competing explanation.
- The recorded PC `0x104f1b9f8` has no captured owning text-region base.
  The old `pc=dyld+0xb9f8 / CacheFinder` attribution is withdrawn pending
  actual mapping and instruction bytes. A static `B.NE` at offset `0xb9f8`
  does not prove asynchronous fault delivery or a sampled PC advancing.
- A file-page hash matching its embedded CodeDirectory does not establish
  which code-signing blob or validation state the live UBC object uses.
- `DYLD_PAGEIN_LINKING=0` is not automatically an effective comparison:
  the actual dyld Process constructor at the string xref `0xa69c` processes
  that variable only behind `SyscallDelegate::internalInstall()`.
- A native helper calling syscall 550 after main may encounter the
  handler's `p_disallow_map_with_linking` gate before pager setup. Such a
  rejection would not test pointer-format support.

## Prepared Diagnostics And Next Gate

`misc/run_dbg.c` now captures x0..x28/fp, optional hardware FAR/ESR, the VM
entry containing the exact PC, and up to 32 instruction bytes from that
readable executable entry. It does not read the failed cache page.
Host build: `/tmp/run_dbg_pcmap`; log: `/tmp/run_dbg_pcmap_build.log`.
Compilation succeeds; existing pointer-sign/unused-parameter warnings remain.
No deployment or runtime validation of these additions has occurred.

`misc/cache_page_copy.c` is a separate host-built diagnostic for ordinary mmap versus `vm_protect(READ|WRITE|COPY)`. It preserves the native shared region, maps the physical m3 file range at a non-fixed VA, records basic/extended info before and after COPY, and logs the first byte of all nine pages. `/tmp/cache_page_copy` builds without warnings; `/tmp/cache_page_copy_build.log` is retained. It has NOT been deployed or run on-device; it cannot establish equivalence to the macOS exec task.

The next decisive dyld trial must capture actual syscall-550 arguments and
return value, identify the owning PC mapping, and distinguish installed pager
from COPY fallback. Only then compare a supported in-process rebasing path;
do not change format 13 to 12, skip rebasing, fake syscall success or substitute
anonymous pages. Cache v5 rebasing must actually run, including authenticated
pointers, even at slide zero.

This pass only reads existing device files/logs. Independent read-only SHA:
`9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`.
No dyld replacement, global sysctl change, daemon stop or kernel patch occurs.

## Native COPY Runtime Control

Runtime-confirmed via `m1-cache-copy-ordinary-20260930.raw` and `m1-cache-copy-copy-20260930.raw`: both direct native trials (no run_dbg/debug marking) have `csflags=0x26803b0d`, exactly the flags recorded for the failed macOS task. Ordinary mmap reads the first byte of all nine m3 pages and exits 0. COPY returns `kr=0`, changes `shadow=1` to `shadow=3` and VM entry offset `0x6c188000` to `0`, then also reads all nine first bytes as `0x00` and exits 0. This confirms those object-chain fields do not identify a dyld pager. It disproves the universal claim that these csflags or COPY necessarily make this file range unreadable; it does not exclude task-specific signing/object behavior or prove all bytes/fixups valid. The COPY control ran after the ordinary read with all nine pages already resident; it is a warm, non-fixed-VA control, NOT a cold-page or macOS-exec equivalent.

The helper is `/var/mobile/cache_page_copy_v1`, signed CDHash `be4b4538bf18f320e7aa2252849e75b58d8b683f`, added to trustcache without changing other entries. Device dyld SHA before/after is `9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`; no dyld replacement or kernel-code modification occurred. The earlier host-only preparation paragraph is historical; COPY now has the runtime witness above, while the new PC-map runner has not yet been deployed.

## RE-Confirmed Root Cause And Kernel Patch Plan (IDB evidence)

The `KERN_MEMORY_ERROR` at `0x1ee188000` is now fully attributed:
dyld_pager `SLIDE_ERROR` triage events (x8) + `VM/NO_DATA` (x8) — the dyld
pager ran and failed inside the pointer-format fixup dispatch.

IDB (`kc_raw_16.3_T8112.bin`, actually T8103 xnu-8792.82.2):

- `dyld_pager_data_request` = `sub_FFFFFE00080661A4`; the fixup switch is
  **inlined** at `0x80667e8`: `LDRH W10,[X2,#6]` reads `mwli_pointer_format`,
  `SUB W16,W10,#1; CMP W16,#0xB; B.HI def` at `0x80667f4` → `0x8066670`
  (SLIDE_ERROR triage + `w25=5` → VM_FAULT_MEMORY_ERROR → KERN_MEMORY_ERROR).
- Jump table `jpt @0x8066c10` covers fmts 1..12; 4,5,7,8,10,11 → default.
  **No case 13** — matches the triage.
- Case helpers: `sub_FFFFFE0008066C40` = shared auth fixup for fmts 1,9,12
  (args X0=userVA X1=contents X2=end X3=pager X4=segInfo W5=pageIndex
  W6=offsetBased); `sub_FFFFFE0008066DB4` = fixupPage64 (fmts 2,6).
- PAC signing goes through **PPL dispatch op 0x3C** at
  `sub_FFFFFE0007F2BFF4` (`MOV X15,#0x3C; B dispatch`): args X0=target,
  X1=key, X2=diversifier, X3=pager+0xB0 (`dyld_a_key`); `target==0` or
  `a_key==0` → store raw. So a kernel-side format-13 handler can do real
  PAC signing by BL'ing this wrapper — no key material needed in EL1.
- Dispatch-time register context: X0=userVA (range loop at 0x80665b0-5b8),
  X2=link_info (pager+0x28), X4=segInfo, W5=pageIndex, X8=contents+0x4000,
  X9=link_info+link_info_size, X23=pager, [SP+0x38]=contents (dest page
  kernel VA). mwl hdr fields: +6 fmt, +0x18 slide, +0x20 image_address.
- Format-13 layout confirmed vs `dyld-1286.10/include/mach-o/fixup-chains.h`:
  rebase = runtimeOffset:34/high8:8/unused:10/next:11/auth:1;
  auth_rebase = runtimeOffset:34/diversity:16/addrDiv:1/keyIsData:1/next:11/
  auth:1. delta stride 8; target = image_address + runtimeOffset. Matches
  xnu-12377 `fixupCachePageAuth64()`.

Patch plan (implemented, pending device test):

- Code cave = dead case-2/3/6 inline bodies `0x8066848..0x80669f0` (424B);
  formats 2/3/6 are non-auth legacy pointers that cannot occur in an arm64e
  shared-cache pager; their jumptable slots get repointed to the default
  block so behavior is preserved (slide-error). External xref check
  confirmed nothing else references the window.
- `B.HI` at `0x80667f4` → cave start `0x8066848`. Cave checks `fmt==13`
  (others → original default), implements fixupCachePageAuth64 verbatim
  including bounds checks; failures branch to the same SLIDE_ERROR block;
  success sets `w25=0` and branches to `0x8066694` (paging_end/UPL commit).
- Files: `misc/fmt13_cave.s` (source of truth, 62 insns / 0xF8 bytes),
  `misc/fmt13_patch.py` (device deployer: verify originals → kwrite32 cave
  → patch B.HI → repoint jpt[2,3,6] → verify; `--undo` restores dispatch).
  All patch sites verified byte-for-byte against the IDB before writing;
  PC-relative encodings are slide-independent.
- Not yet runtime-tested; iPad reboot pending. Post-reboot runner:
  `misc/post_reboot_fmt13.sh` (env restore + cache cdhashes + patch +
  `/bin/echo HI` witness).
