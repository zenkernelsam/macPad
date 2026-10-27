# Frontend R014 result — bounded 22F82 responsibility capture plan

Date: 2026-10-18. Offline audit only. No device/SSH command, no helper/source/
test/build-input change, no dependency install, no Git write. Added only this
result and
`docs/evidence/r014-responsibility-capture-plan-20261018.md`
(SHA-256 `3d6817627b870ce0396e7cf35009f75afa0ff8173fa0171915936f3b47577c42`).

Candidate HEAD: `c97e219`.

## Tool audit outcome

- `misc/dsc_extract_image.py`: partial only. It can extract an image and copy
  linkedit_data blobs, but it does not preserve the cache header/mapping table,
  decode fixups/patch tables, or copy the shared GOT page. Its literal
  `0x88/0x90` image-table read is a Ventura-era empirical assumption, not a
  dyld-1286 fact.
- `misc/dsc_cache_subset.py`: dependency closure only; unsuitable.
- `misc/dsc_extract.c` / `misc/extract_dyld_cache.py`: Apple `dsc_extractor`
  image extraction; unsuitable for the shared GOT/patch record.
- `misc/cache_page_probe.c`: reads only the first byte of an aligned 16KB page;
  not enough for the 8-byte slot. `cache_page_copy.c`/`cache_page_fixed.c` are
  hardcoded 15.6-era paths/offsets; unusable.
- `misc/cache_cdhash.py`: suitable for cache/subcache identity (0x28/0x30
  offsets verified against the stock 22F82 cryptex).
- `misc/disasm_remote_dyld_range.sh`: useful for a bounded raw page/stub read
  **after** the mapping is validated; it does not validate header/fixup.
- `DevinWatchdog_mac/macpad_ssh.py` (no `misc/macpad_ssh.py` exists): suitable
  for bounded commands/scripts/uploads and true remote exit records
  (`MACPAD_SSH_EXIT=… timeout=…`).
- `misc/lldb_remote.sh`: suitable for a read-only runtime cross-check
  (`image list`, `memory read`, `image lookup`), with cleanup.
- `misc/ios_lldb_tmux.sh`: suitable for on-device lldb commands and timeout
  checking. `misc/lldb_attach_chroot_ws.sh` is not: WindowServer is arm64,
  while the exact libquarantine is arm64e.
- `misc/csprobe2.py`, `fmt13_patch.py`, `fmt13_cave.s`: kernel probes/writes;
  not read-only and not offline cache parsers; do not use.

## Ventura 22F82 vs local dyld-1286.10

Repo docs identify 22F82 dyld as **1066.8**; only dyld-1286.10 source is local.
The 1286 header/source is structural reference only, not a 22F82 header fact.
In particular, `dsc_extract_image.py` reads the image table at `0x88/0x90`
while the 1286 header has `imagesTextOffset/Count` there; the tool’s successful
22F82 extraction is empirical, not a documented 1066 layout. The exact
`patchInfo`, subcache-array and fixup layouts must be validated from the
captured 22F82 header before any offset is used.

The exact libquarantine image has no `LC_DYLD_INFO`/`LC_DYLD_CHAINED_FIXUPS`
and zero image-local GOT/auth-GOT values. The primary hypothesis is therefore
the **cache patch table** (v1/v2/v3 export/GOT locations), but its version and
layout are THEORY until captured. Do not parse a v2/v3 structure as v4 or use
1286 offsets as 1066 facts.

## Bounded capture plan (backend-owned window)

**Transaction A — identity + headers/mappings (small):**
- Record actual rootfs cache/subcache paths, sizes/inodes, SHA-256 and
  `cache_cdhash.py` identity; require the main cache UUID
  `BE59BB71-7B69-3EFC-86A8-5625A077AE2E`.
- Capture only the first 64KB of each cache/subcache as hex plus source
  SHA-256, and record `kern.osversion`/macOS build and dyld UUID/version.
- Offline, validate magic/UUID/header version and mapping entry format. If the
  exact 1066 header layout cannot be validated, stop; do not guess
  `patchInfo`/images/subcache offsets.

**Transaction B — targeted page/patch/fixup (small):**
- Use the validated mapping table to find the one mapping containing unslid
  `0x1dfef8f88`; identify the actual subcache/file offset (main vs `.01` is
  never assumed).
- Capture the containing page and 8-byte slot with SHA-256; stop on a short
  read/unmapped slot.
- Capture `patchInfo` prefix (≤256B) and, if ≤1MB, the whole patch region;
  otherwise stop for a targeted follow-up. Parse only the validated version
  (v1 image/export/location/names; v2+ image/client/location; v3 GOT
  `cacheOffsetOfUse` = `0x5fef8f88`), resolve export name and implementation
  image. If no patch location covers the slot, check the owning image’s
  chained-fixup/classic-bind metadata; if neither covers it, stop.
- Capture the minimal image table/load-command data needed to identify the
  export’s image/UUID and target.

**Transaction C — optional runtime cross-check:**
- Use `lldb_remote.sh`/`ios_lldb_tmux.sh` on an arm64e process that maps
  libquarantine; verify `A2D17FF6-…`/arm64e and measure the live slide. Read
  `measured_slide + 0x1dfef8f88` (observed slide `0x123ec000` would give
  `0x1f22e4f88`), record raw 8 bytes. No breakpoints and no invented PAC mask;
  only use an existing `ptrauth_strip` with the exact fixup key/diversity.
  If the pointer cannot be decoded, stop; do not call it a provider.
- WindowServer (`lldb_attach_chroot_ws.sh`) is arm64 and is not used.

## Minimum preserved data

Cache/subcache identity + SHA; validated header/mapping; actual subcache;
16KB page and 8-byte slot with hash; patch/fixup version, counts and matching
export/name/impl; target image UUID/path/export; optional runtime slide/raw
slot/lookup. Never copy the full 1.6GB/1.7GB cache; caps: 64KB per header/
mapping, 16KB page, ≤1MB patch/fixup, stop above that.

## True exit/status checks and stop conditions

Every remote command must report `MACPAD_SSH_EXIT=0 timeout=0`; `xxd`/`dd`
lengths must equal the requested byte count and SHA-256 must match; parser
must exit 0 with only validated fields; `disasm_remote_dyld_range.sh` must
produce both raw and disassembly sections; lldb must show target state T and
the exact UUID/arch, then detach cleanly. Any nonzero/timeout, missing header/
mapping/page, zero/placeholder slot, unknown patch version, ambiguous target
image, or request to exceed the caps stops the plan. No guessed bytes,
offsets, pointer formats or PAC keys.

## Stub naming vs 103

Resolving the provider/export (Milestone A) does not assign errno 103.
Distinguishing the kernel branch (Milestone B) requires exact device-kernel
build (8792.82.2) and `__mac_syscall`/`mac_proc_check_mac_syscall` code/
symbols plus a read-only observation of: precheck return; policy `Quarantine`
match or no match; matched `mpo_policy_syscall` return; final w0/errno and
userland caller/token/struct/output-token write. Without that, only “103 was
returned by the MAC syscall path” is supportable; precheck, no-match and
matched-handler remain unresolved. A provider name alone is not evidence.

## Limits

No 22F82 cache/subcache/patch table or 1066 header source is local; this is a
plan, not an observation. The patch-table route and shared-GOT ownership are
THEORY until the exact header/patch data is captured and validated. Runtime
PAC decoding remains unresolved without exact fixup metadata.

Full detail:
`docs/evidence/r014-responsibility-capture-plan-20261018.md`.

(handover to codex now)
