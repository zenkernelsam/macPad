# R015 amendment — no exact 22F82/1066.8 header witness; capture stops after raw header

Date: 2026-10-18. Scope: resolve the R014 review condition using only existing
offline sources/artifacts. No SSH/device, no source/test/build-input edit,
no dependency install, no Git write. This note **amends** R014; it does not
replace or rewrite `r014-responsibility-capture-plan-20261018.md`.

## 1. Review condition and disposition

R014 proposed deciding 32-byte vs 48-byte mapping records by checking whether
parsed addresses and file bounds look plausible. Review R014 correctly rejects
that: plausibility is not a format witness. Since 22F82 dyld is 1066.8 and only
1286.10 headers (plus 15.6.1 and iOS 16.3-derived tooling) are local, R014's
mapping/page/patch parsing path is **withdrawn** until a positive,
version-matched witness exists.

## 2. Offline search result (FACT: no exact witness found)

Searched only repository/analysis sources and referenced evidence:

| Source/artifact | Version/identity | Exact 1066.8 witness? |
| --- | --- | --- |
| `analysis/dyld-dyld-1286.10/include/mach-o/dyld_cache_format.h`, `fixup-chains.h`, `common/CachePatching.h`, `common/DyldSharedCache.cpp` | dyld **1286.10** | **No** — newer/other version; its `mappingOffset`/`mappingWithSlide`, `imagesOffset`, subcache and patch-table layouts are not 1066.8 facts. |
| `docs/porting/archive/macos15-roots/dyld-15.6.1-full-analysis.md` (complete header table) | dyld **15.6.1** | **No** — its table has 48-byte mapping entries, `mappingOffset`/`mappingCount` gates, `subCacheArrayOffset` at 0x138/0x188, `patchInfoAddr` at 0x98, `imagesOffset` at 0x1C0; do not transplant to 1066.8. |
| `analysis/dyldwork/dyld_*.bin` (sampled `dyld_t`, `dyld_macwin`, `dyld_deployed_backup`, `dyld_cleanB.signed`) | UUID `3247E185-CED2-36FF-9E29-47A77C23E004`, minos **15.6** | **No** — 15.6, not 1066.8. |
| `analysis/MacWSBootingGuide/misc/macws_dyld_image_for_address.py:17`, `macws_dyld_range_query.py:93` | image table at **0x1c0**, 32-byte mappings | **No exact Ventura witness** — evidence docs show `macws_dyld_range_query.py` used on the iPadOS 16.3 SpringBoard cache (`docs/evidence/switcher-selection-safe-mode-20260912.md:37`), not the 22F82 cache. Its 0x1c0/32-byte assumptions are not a 1066.8 header source. |
| `misc/dsc_extract_image.py` | 0x88/0x90 image table, 32-byte mappings | **Empirical only** — it successfully extracted exact libquarantine from the 22F82 cache, but did not preserve the header, and its literal 0x88/0x90 is `imagesTextOffset/Count` in the 1286 header. Success is not a positive field-offset/mapping/patch witness. |
| `coordination/artifacts/r006-cache-find2.json` | alias listing only | **No** — lists 1.5G main, 1.8G `.01`, 854K `.map` through the rootfs alias; no header bytes/fields. |
| `analysis/MacWSBootingGuide/docs/evidence/...sample-*.txt` | dyld 1066.8 UUID `BD993F39-C902-37D6-8BD8-6D827C7F121E` | **No** — text samples identify the version/UUID, not a local dyld binary or header dump. A bounded search of small `analysis/` files for this UUID found no local binary. |
| Local 22F82 cache/subcache/header | — | **Absent** (R013; no `.01`/`.map` header data local). |

No local 1066.8 dyld binary/source/header table, and no exact 22F82 cache
header dump, was found. The two independent checks are: (a) no 1066.8
binary/source artifact exists locally (only 15.6/1286/iOS 16.3 material), and
(b) no 22F82 header field/dump exists (only the 15.6.1 table, which differs).

## 3. Revised capture procedure (identity + raw header only)

Replace R014's Transaction A/B/C with the following **single bounded
transaction**. It preserves the confirmed rootfs alias and performs no format
interpretation.

### Phase 0 — identity and raw header sample

1. Use the confirmed rootfs alias:
   `/var/mnt/rootfs/System/Cryptexes/OS/System/Library/dyld` (R006 command
   `coordination/artifacts/r006-cache-find2.json`, exit 0, listed the 22F82
   cache pair). Also record the canonical package-storage path
   `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld`
   (R006 `r006-cache-find.json`, exit 0). Do **not** replace the alias with
   canonical storage; do not use `/var/mnt/rootfs/System/Library/dyld/`
   (`r006-cache-dir.json`, exit 1, absent).
2. For every actual cache/subcache file in the alias (and canonical path if
   accessible), record:
   - path, `stat` size/inode/device, `shasum -a 256`;
   - optional `misc/cache_cdhash.py` identity (its 0x28/0x30 offsets are
     source-verified against the stock 22F82 cryptex; this is identity, not
     mapping/patch validation);
   - the first **64KB** of each file as hex plus SHA-256 and exact byte count.
3. Record `kern.osversion`, macOS build, and the exact 22F82 dyld UUID/version
   (`BD993F39-C902-37D6-8BD8-6D827C7F121E`, 1066.8) as identity context.
4. **Stop.** Do not parse `mappingOffset`/`mappingCount`, choose 32/48-byte
   mapping entries, read `SubCacheArray`, `patchInfo`, the image table, the
   target slot/page, or derive any file offset from the sample. The raw header
   sample is preserved verbatim for a later exact-version parser.

No lldb/runtime slot read is part of this amended transaction; it also depends
on the deferred mapping/page/patch interpretation.

### Rootfs alias vs canonical (retained)

- Confirmed alias: `/var/mnt/rootfs/System/Cryptexes/OS/System/Library/dyld`.
- Canonical: `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld`.
- Verify relationship read-only (`stat` device/inode/size; SHA for both) if
  both paths are accessible. Do not assume hardlink/symlink equivalence and do
  not write either path.

## 4. Precise artifact needed to resume

To resume mapping/page/patch work, one of:

1. **Exact dyld 1066.8 header definition/source** (matching 22F82), e.g. the
   Apple dyld-1066.8 `include/mach-o/dyld_cache_format.h`,
   `common/CachePatching.h`, and `common/DyldSharedCache.cpp` (or an
   equivalent exact field-offset table). It must define:
   - `mappingOffset`/`mappingCount` and the mapping entry size/discriminator;
   - `imagesOffset`/`imagesOffsetOld` and image-info layout;
   - `subCacheArrayOffset/Count` and entry schema;
   - `patchInfoAddr`/`patchInfoSize` and the patch-table version/schema.
2. **Exact 22F82 `/usr/lib/dyld` 1066.8 binary** (UUID
   `BD993F39-C902-37D6-8BD8-6D827C7F121E`, with SHA-256) or an exact
   1066.8 source checkout, so the field offsets can be derived offline from
   the matching code rather than guessed.
3. **A 1066.8-versioned parser/tool** positively validated on the exact
   22F82 cache header sample (not a 1286/15.6 parser, not an address-
   plausibility heuristic). The tool must report the exact header fields,
   mapping format, subcache schema and patch-table version it used.
4. The Phase 0 raw header samples and cache/subcache identities, including
   main, `.01`, and any actual subcache suffix, with SHA-256.

The existing 15.6.1 table, 1286.10 source, author iOS-cache tools and
`dsc_extract_image.py`'s empirical 22F82 success are **not** substitutes for
items 1–3.

## 5. Exit/status checks and stop conditions

- Every remote command through `DevinWatchdog_mac/macpad_ssh.py` must report
  `MACPAD_SSH_EXIT=0 timeout=0`; any nonzero/timeout stops.
- `stat`/`shasum`/`xxd` output must produce the exact requested byte count
  (hex length/2) and matching SHA-256; short reads or checksum mismatch stop.
- `cache_cdhash.py`, if used, must exit 0; an error or unexpected identity
  stops.
- Stop after the 64KB sample: do not read beyond it, do not derive a mapping/
  page/patch offset, do not interpret header fields as 22F82 facts, and do not
  run a device lldb slot read. If any procedure requires such a step, stop and
  request the exact 1066.8 witness above.
- Never treat plausible addresses, bounds, a 1286.10 parser, a 15.6.1 table,
  or the iOS 16.3 author tool as validation.

## 6. Unchanged items

The R014 tool audit, the arm64/arm64e LLDB distinction, the requirement to
avoid a full-cache copy, and the stub-name vs errno-103 separation remain
valid. Resolving the provider later still does not identify the kernel path
that returned 103; that needs exact device-kernel policy-boundary evidence.

## 7. Commands/exits and limits

`find`/`rg`/`grep`/`otool -l` over `analysis/`, `docs/`, and the author repo
exited 0; `git log --oneline -1` (read-only) reported `c97e219`. No device,
build, test, dependency or Git operation was performed.

Limits: no exact 1066.8 header source/table or 22F82 cache header exists
locally; the Phase 0 sample is not interpreted here. The alias/canonical
relationship is not yet proven inode-identical; that is a read-only capture
check for the backend window.

(handover to codex now)
