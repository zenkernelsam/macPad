# R014 — minimal read-only 22F82 responsibility capture plan

Date: 2026-10-18. Scope: inspect existing repository tools/source and prepare a
bounded, read-only evidence-capture plan for the next backend-owned device
window to resolve the `Quarantine` selector `0xb6` stub at `0x18b321bc0` /
GOT `0x1dfef8f88`. **No device/SSH command, no helper/code/test change, no
dependency install, no Git write.** Only this note and the R014 result were
added.

R013 established: the exact sparse `libquarantine.dylib` is local, but the
22F82 cache/subcache/fixup input is not. This plan therefore starts from the
device cache, not from the existing sparse file.

## 1. Tool audit (existing repository only)

| Tool | Suitable for this task? | Exact reason |
| --- | --- | --- |
| `misc/dsc_extract_image.py` | Partial / not sufficient alone | Loads main + `.01`…`.15` and 32-byte mappings (`:39-50`, `:62-64`); finds the image table via literal `0x88/0x90` (`:66-76`); copies non-LINKEDIT segments and linkedit_data blobs (`:100-111`, `:140-150`); zeroes symtab/dysymtab (`:161-164`). It does **not** preserve the cache header/mapping table, does not decode chained fixups or the cache patch table, and does not copy a cache-global GOT/page. Its `0x88/0x90` image-table use is a Ventura-era empirical assumption, not a dyld-1286 fact. |
| `misc/dsc_cache_subset.py` | No | Dependency-closure only (`:15-19`); no cache bytes, mapping, fixup or patch table. |
| `misc/dsc_extract.c`, `misc/extract_dyld_cache.py` | No for this question | Apple `dsc_extractor.bundle` image extraction; extracts dylibs, not the shared GOT or patch/fixup record. Filter in `dsc_extract.c` is unused (`:20-24`). Heavy and not targeted. |
| `misc/cache_page_probe.c` | Partial page probe only | Takes `<cache-file> <offset> <pread|map>`, requires 16KB alignment (`:13-19`), and reads only the first byte of the page (`:25-37`). Cannot read the 8-byte slot or arbitrary offset. |
| `misc/cache_page_copy.c`, `misc/cache_page_fixed.c` | No | Hardcoded 15.6-era cache path/offset `/var/mnt/rootfs/private/tmp/dsc/...` and `0x6c188000` (`cache_page_copy.c:44-46,105-106`; `cache_page_fixed.c:38,44-57`); not 22F82. |
| `misc/cache_cdhash.py` | Yes for cache identity | Uses `codeSignatureOffset/Size` at header `0x28/0x30` (`:16-30`), verified against the stock 22F82 cryptex in the source comment (`:1-14`). Use to record main/subcache CDHashes; it does not resolve imports. |
| `misc/disasm_remote_dyld_range.sh` | Yes for bounded raw read/disasm **after** mapping validation | Computes `FILE_OFFSET = VM_ADDRESS - CACHE_MAPPING_VM` (`:20-26`) and reads via on-device `xxd`, disassembling 4-byte words with `llvm-mc-16` (`:31-38`). Assumes the caller already validated the cache path/mapping; no header/fixup validation, no timeout/record logic of its own. |
| `DevinWatchdog_mac/macpad_ssh.py` (the packet’s `misc/macpad_ssh.py` does not exist) | Yes for bounded remote execution/record | Supports `--command`, `--script`, `--upload`, timeout, real remote exit status (`MACPAD_SSH_EXIT=… timeout=…`), and sanitized JSON records (`:15-43`, `:68-100`). It does not parse caches. |
| `misc/lldb_remote.sh` | Yes for read-only runtime cross-check | Host Apple lldb + on-device `debugserver`, explicit PID/process attach, state check, SSH tunnel, cleanup trap (`:1-17`, `:86-93`, `:99-138`). No breakpoints required for `image list` / `memory read` / `image lookup`; attaching briefly stops the process. |
| `misc/ios_lldb_tmux.sh` | Yes for on-device lldb commands | Persistent tmux lldb; `cmd`/`cmd-multi`/`capture`/`status`/`stop` (`:1-39`, `:146-173`, `:197-215`); no Python scripting; warns on prompt timeout. Default `PASSWORD=alpine` (`:45`) must not be used; use the authorized session environment. |
| `misc/lldb_attach_chroot_ws.sh` | No for this stub | Attaches to `WindowServer`, which is arm64 in the sampled generation (`:16-23`); the exact libquarantine image is arm64e. Wrong image/arch for this provider read. |
| `misc/csprobe2.py`, `misc/fmt13_patch.py`, `misc/fmt13_cave.s` | No | Kernel-memory probes/patch (KRW / dyld_pager patch). `fmt13_patch.py` writes kernel memory; R014 is read-only and must not use these. They are not offline cache parsers. |
| Local dyld 1286.10 headers/source | Structural reference only | See §2. Not the 22F82 header definition. |

Existing `lldb_*.lldb` files show the exact read-only command shapes
`image list -o -f`, `image lookup --address`, `memory read --force -fx -s8`;
no new breakpoint is required or proposed.

## 2. Ventura 22F82 vs local dyld-1286.10 (format facts)

**FACT:** the repository documents the Ventura 22F82 dyld as **1066.8**
(`docs/porting/ventura-22F82-bringup-state.md:433`). The only local dyld source
is `analysis/dyld-dyld-1286.10` (plus the 15.6 binary/source material); no
1066.8 header/source or 22F82 cache file exists locally.

**FACT (local 1286 header, not 22F82):**
`analysis/dyld-dyld-1286.10/include/mach-o/dyld_cache_format.h:32-116`
defines the current `dyld_cache_header`; `:122-128` is the classic 32-byte
`dyld_cache_mapping_info`; `:141-150` is the 48-byte
`dyld_cache_mapping_and_slide_info`; `:157-164` is `dyld_cache_image_info`;
`:577-586` is the subcache entry. `DyldSharedCache.cpp:160-190` chooses classic
vs slide mappings from `mappingOffset` vs `mappingWithSlideOffset`; `:311-321`
chooses `imagesOffset` vs `imagesOffsetOld`; `:2041-2074` reads the slide
mapping array. These are **version-sensitive** rules from 1286, not a license
to apply 1286 offsets to 1066.

**Concrete mismatch:** `misc/dsc_extract_image.py:66-76` reads the cache image
table as two `u64` at `0x88/0x90`. In the local 1286 header, `0x88/0x90` are
`imagesTextOffset/imagesTextCount`; `imagesOffset` is a later field and
`imagesOffsetOld` is at 0x18. The tool’s successful extraction of exact
libquarantine is **empirical evidence** that `0x88/0x90` matched the 22F82
header at extraction time, but it is **not** a documented 1066 layout and must
not be generalized to `patchInfo`, subcache arrays, or fixups.

**FACT (local 1286 fixup/patch structures, applicability to 1066 unproven):**
- `analysis/dyld-dyld-1286.10/include/mach-o/fixup-chains.h:37-72`
  (`dyld_chained_fixups_header`, starts-in-image/segment), `:90-105`
  (pointer formats, including `ARM64E_USERLAND24=12` and
  `ARM64E_SHARED_CACHE=13`), `:109-153` (arm64e bind/auth entries),
  `:270-289` (shared-cache rebase/auth entries).
- `analysis/dyld-dyld-1286.10/common/CachePatching.h:45-77` (v1 patch table:
  image patches/export/locations/names), `:106-176` (v2 large/split cache
  tables), `:177-218` (v3 GOT-combining tables with `cacheOffsetOfUse`),
  `:221-312` (v4 location layout).
- `DyldSharedCache.cpp:564,614` reads `patchInfoAddr` and selects v1 vs v2+
  structures.

**THEORY (must be validated):** the exact libquarantine image has no
`LC_DYLD_INFO` and no `LC_DYLD_CHAINED_FIXUPS`, and its image-local
`__auth_got`/`__got` are zero. Its `__auth_stubs` load from the shared
`0x1dfef8xxx` page. This is consistent with the **cache patch table**
(v1/v2/v3 GOT locations) supplying the provider, rather than an image-local
chained-fixup chain. Do not assert the patch-table version or that this theory
is true until the exact cache header/patch data is captured.

## 3. Capture plan for the backend window

The plan is deliberately two small transactions because the exact 1066 header
must be validated before any targeted offset read.

### Transaction A — identity + header/mapping metadata (small, read-only)

All remote commands through `DevinWatchdog_mac/macpad_ssh.py --command`, with
the recorded `MACPAD_SSH_EXIT=0 timeout=0` required. Do not upload a parser or
write a helper.

1. Identify the exact rootfs cache files:
   `/var/mnt/rootfs/System/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e`
   and any actual `.01`/`.02`/… files. Record `ls -l`, `stat` size/inode,
   `shasum -a 256`, and (if applicable) `misc/cache_cdhash.py` output. Stop if
   the expected path is absent.
2. Capture the first **64KB** of each real cache/subcache file as hex
   (`xxd -p -l 65536 …`) plus `shasum -a 256` of the source file. 64KB is
   enough for the header+mapping table in the observed format; if
   `mappingOffset/mappingCount` later imply a larger table, stop and request a
   second targeted range. Do not copy the full 1.6GB/1.7GB cache.
3. Record `kern.osversion`, macOS build, and the exact chroot dyld UUID/version
   (1066.8 expected) as identity context.
4. Offline validation before Transaction B:
   - magic must be `dyld_v1  arm64e`; UUID of the main cache must be
     `BE59BB71-7B69-3EFC-86A8-5625A077AE2E`.
   - parse only fields supported by the exact header version; record
     `mappingOffset`, `mappingCount`, header size/version and the mapping
     entry size. Use the 1286 parser’s `mappingOffset` discriminator only as a
     hypothesis; validate 32-byte vs 48-byte entries by sane addresses/sizes
     and file bounds.
   - if the header layout/version cannot be matched to a 22F82/1066-compatible
     definition, **stop**; do not parse `patchInfo`/images/subcache offsets by
     guess.

### Transaction B — targeted page/patch/fixup capture (small, read-only)

Only after A validates. Use the validated mapping table to find the one
mapping containing unslid `0x1dfef8f88`; do not assume main vs `.01`.

1. Record the mapping index/address/size/fileOffset/flags and the exact
   subcache file that owns it. Compute
   `file_offset = mapping.fileOffset + (0x1dfef8f88 - mapping.address)` and
   verify it is in file bounds and on a cache page.
2. Capture the containing page (16KB in the observed Ventura mapping; exact
   page size from the header/fixup metadata) and at least the 8-byte slot,
   plus SHA-256 and byte count. If the read is short or the page is
   unreadable, stop.
3. Capture the cache header’s `patchInfoAddr`/`patchInfoSize` record and a
   bounded prefix (≤256 bytes) of the patch-info structure. If
   `patchInfoSize` is ≤1MB, capture the whole patch region; if larger, capture
   only the version/counts/header and stop for a targeted follow-up. Do not
   copy the cache.
4. Depending on the exact patch-table version validated offline:
   - v1: image-patch/export/location/names arrays; locate `cacheOffset` =
     `0x5fef8f88`, then resolve `exportNameOffset` in the names blob and
     `cacheOffsetOfImpl`.
   - v2+: `patchTableVersion`/`patchLocationVersion`, image patches/exports/
     clients/locations/names; for v3 also the GOT client/location arrays.
     Locate the GOT location whose `cacheOffsetOfUse` equals `0x5fef8f88`
     (v3) or the relevant image-relative location (v2); resolve the export
     name and implementation image.
   - If the version is v4 or unknown, or the expected GOT table is absent,
     stop; do not reinterpret a v2/v3 structure as v4 or vice versa.
5. Capture the image table and only the load commands/segment ranges needed
   to identify the export’s owning image and the decode target. Record image
   index/path/UUID and export name/offset. Do not scan/extract all images if
   the patch table already names the provider.
6. If no patch-table location covers the slot, check the owning image’s
   `LC_DYLD_CHAINED_FIXUPS`/`LC_DYLD_INFO` (if the exact image has one) and
   capture only the header/starts/imports/symbols ranges needed for the slot
   page. If neither covers it, stop and report the exact missing metadata.

### Optional Transaction C — runtime cross-check (same window, bounded)

Only if A/B are ambiguous. Use `misc/lldb_remote.sh` (host lldb + debugserver)
or `misc/ios_lldb_tmux.sh` (on-device) against an **arm64e** chroot process
that actually maps libquarantine; verify with `image list -o -f` that
`A2D17FF6-…` is arm64e and record the measured slide. `misc/lldb_attach_chroot_ws.sh`
is not used: WindowServer is arm64.

- Read the slot at `measured_slide + 0x1dfef8f88` (the stub is
  `0x18b321bc0`; the GOT address is not a guessed runtime address). For the
  observed arm64e slide `0x123ec000` this would be `0x1f22e4f88`, but measure
  the live slide first and require the exact image UUID.
- Record raw 8 bytes; verify non-zero and that the address equals the measured
  slot. If zero/placeholder, stop.
- Do **not** set a breakpoint or invent a PAC mask. If the exact fixup/pointer
  format and auth key/diversity are known from B, an existing `ptrauth_strip`
  helper/expression may be used only with that key; otherwise record the raw
  value and treat the provider as unresolved. `image lookup -a` is used only
  on a decoded target address. Detach/quit and require the helper’s cleanup.

## 4. Minimum data to preserve (exact)

| Category | Minimum data | Bound |
| --- | --- | --- |
| Identity | cache/subcache path, size, inode, SHA-256, header UUID, magic, platform/cacheType if validated; dyld version/UUID; device/macOS build | ≤64KB per header page |
| Mapping | `mappingOffset`/Count, validated entry size, the one slot mapping, file offset formula | ≤64KB |
| Subcache | actual filenames, each header UUID/suffix/VM offset if fields validated | ≤64KB |
| Page/slot | 16KB page containing `0x1dfef8f88` (at least 8 bytes), SHA-256 | 16KB |
| Patch/fixup | patch-info version/counts; matching location/export/name/impl; or image chained-fixup header/starts/imports/symbols | ≤1MB; stop if larger |
| Target image | image index/path/UUID, export name/offset, impl VA; segment range only if needed | ≤64KB |
| Runtime (optional) | process/PID/arch, image UUID/slide, raw slot 8 bytes, decoded target/lookup output | minimal |

All captured bytes must carry SHA-256 and exact byte counts. The cache itself
is never copied wholesale.

## 5. True exit/status checks and stop conditions

- Every remote command: `DevinWatchdog_mac/macpad_ssh.py` must print
  `MACPAD_SSH_EXIT=0 timeout=0`; any nonzero/timeout stops the plan. The
  `--record` JSON is the evidence record.
- `xxd`/`dd` output length must match the requested bytes (hex length / 2);
  `shasum` of captured bytes and source files recorded; a short read or
  checksum mismatch stops.
- `cache_cdhash.py` exit 0 and the expected cache UUID/subcache identity; a
  mismatch stops.
- Offline parser: exit 0 with expected fields; any unknown version/format,
  out-of-bounds field, or ambiguous 32/48-byte mapping stops.
- `disasm_remote_dyld_range.sh`: `set -euo pipefail`; require both
  `__MACWS_RAW__` and `__MACWS_DISASSEMBLY__` sections and exact byte count.
- LLDB: target state `T`, `image list` shows the exact UUID/arch, expected
  memory-read value/non-zero, clean `process detach`; `ios_lldb_tmux.sh`
  prompt timeout is a failure. No debugserver/tunnel may remain.
- Stop immediately if: cache path missing; header magic/UUID/mapping format
  cannot be validated; slot mapping/page is absent; slot is zero/placeholder;
  no patch-table or chained-fixup record covers the page; patch version is
  v4/unknown; target image/export is ambiguous; any attempt would exceed the
  byte caps or require copying the full cache; any command exit is nonzero.
- Never guess sparse bytes, offsets, pointer formats, PAC keys, or the
  candidate target image. Never treat dyld-1286 offsets as 22F82 facts.

## 6. Stub naming vs errno 103 (separate milestones)

**Milestone A (this plan):** resolve the provider/export and target image for
the `Quarantine` selector `0xb6` stub. A name such as `___sandbox_ms` or
`__mac_syscall` (only a version-adjacent candidate today) is **not** proof of
the kernel branch that returned 103.

**Milestone B (separate evidence):** distinguish:
1. `mac_proc_check_mac_syscall` precheck returning ENOPOLICY;
2. no matching `mpc_name`/`mpo_policy_syscall` default ENOPOLICY;
3. a matched `mpo_policy_syscall` returning ENOPOLICY.

Required evidence for B:
- exact device kernel build (8792.82.2) and exact
  `__mac_syscall`/`mac_proc_check_mac_syscall` code/symbols from the device
  kernelcache, not adjacent XNU source;
- a read-only kernel trace/observation for the exact caller/audit token,
  policy string `Quarantine`, selector `0xb6`: precheck return; policy match
  (`mpc_name`) or no-match; matched handler return; final `w0`/errno;
- userland correlation from the same thread/time: exact tccd/WS caller,
  selector/struct, raw return/errno, output-token write;
- if tracing is unavailable, an ABI-preserving read-only query that separates
  the three branches; no setter, kernel write, fake token/session or grant.

Until B is observed, say only that 103 was returned by the MAC syscall path;
do not attribute it to precheck, no-match, or a handler, and do not infer it
from the provider name.

## 7. Route comparison

**Cache-side (recommended primary):** static, repeatable, and can name the
provider directly from the patch-table export/name or chained-fixup import
without PAC arithmetic. Requires exact 1066 header/patch-version validation
and careful subcache mapping; no full-cache copy. The patch-table route is
stronger than raw slot bytes because image-local GOT slots are zero in the
sparse image and may be runtime-patched.

**Runtime (secondary/cross-check):** minimal bytes and directly reads the
resolved slot, using existing `lldb_remote.sh`/`ios_lldb_tmux.sh`; but it
requires an arm64e process with the cache mapped, briefly stops it, does not
give the import name directly, and a PAC-signed slot cannot be named without
the exact auth key/diversity. Do not use it to bypass format validation.

Recommended order: A metadata, offline header validation, B patch/page
capture, then optional runtime read. If A cannot validate the exact header,
capture and stop rather than parse by guess.

## 8. Local inspection commands / exits

`rg`/`find`/file reads over `misc/`, `analysis/dyld-dyld-1286.10`, and
`docs/` exited 0; `git log --oneline -1` (read-only) reported `c97e219`.
No tests, builds, package, device commands, or Git writes were run.

## 9. Limits

- No 22F82 cache/subcache/patch table or 1066 header source is local; this is
  a plan, not an observation. The existing sparse image cannot substitute for
  the cache.
- The cache patch-table version and the shared-GOT ownership are THEORY until
  the exact header/patch data is captured and validated.
- The runtime route’s PAC handling is unresolved without the exact fixup
  metadata; no key/diversity is assumed.
- Stub-name resolution alone does not assign 103; kernel policy evidence is a
  separate milestone.

(handover to codex now)
