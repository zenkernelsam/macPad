# Frontend R015 result — no exact 1066.8 header witness; R014 capture reduced

Date: 2026-10-18. Offline audit only. No device/SSH, no source/test/build-input
change, no dependency install, no Git write. Added only this result and the
dated amendment
`docs/evidence/r015-r014-capture-amendment-20261018.md`
(SHA-256 `47261f7222de204236c148522ca58c6876f41a04dc087d28724f2572f074c76a`).

Candidate HEAD: `c97e219`.

## Review condition resolved: no exact witness exists locally

Searched only existing offline sources/artifacts. **No exact 22F82/dyld 1066.8
witness** was found for the header field offsets, mapping discriminator,
subcache array or patch-info schema:

- Local dyld source is **1286.10**; its `mappingOffset`/`mappingWithSlide`,
  `imagesOffset`, subcache and patch-table layouts are not 1066.8 facts.
- The only complete local header table is **15.6.1**, with 48-byte mapping
  entries, `mappingOffset`/`mappingCount` gates, `subCacheArrayOffset` at
  0x138/0x188, `patchInfoAddr` at 0x98 and `imagesOffset` at 0x1C0. It is not
  22F82/1066.8 and must not be transplanted.
- `analysis/dyldwork/dyld_*.bin` sampled UUID
  `3247E185-CED2-36FF-9E29-47A77C23E004`, minos 15.6; not 1066.8.
- Author tools `macws_dyld_image_for_address.py:17` and
  `macws_dyld_range_query.py:93` use `0x1c0`/32-byte mappings, but the evidence
  docs show `macws_dyld_range_query.py` used on the iPadOS 16.3 SpringBoard
  cache, not 22F82. Not an exact Ventura witness.
- `misc/dsc_extract_image.py`’s 22F82 success is empirical only; it preserves
  no header and uses 0x88/0x90 (which are `imagesTextOffset/Count` in 1286),
  so it is not a positive field-offset/mapping/patch witness.
- `r006-cache-find2.json` only lists the alias cache files (1.5G main, 1.8G
  `.01`, 854K `.map`); no header bytes/fields.
- 22F82 dyld 1066.8 UUID `BD993F39-C902-37D6-8BD8-6D827C7F121E` appears in
  text samples, but a bounded search of small `analysis/` files found no local
  dyld binary/header dump.

Therefore R014’s 32-byte-vs-48-byte mapping discrimination by plausible
addresses/bounds is **withdrawn**. It was not a format witness.

## Revised procedure (identity + raw header only)

R014 Transactions A/B/C are replaced by one bounded transaction:

1. Use the confirmed rootfs alias
   `/var/mnt/rootfs/System/Cryptexes/OS/System/Library/dyld` (r006-cache-find2,
   exit 0). Also record the canonical
   `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld`
   (r006-cache-find, exit 0). Retain the alias; do not replace it; do not use
   the absent `/var/mnt/rootfs/System/Library/dyld/` (r006-cache-dir, exit 1).
2. For each actual cache/subcache file (alias, and canonical if accessible):
   record path, `stat` size/inode/device, `shasum -a 256`, optional
   `cache_cdhash.py` identity (its 0x28/0x30 offsets are source-verified
   against the stock 22F82 cryptex; identity only), and the first **64KB** as
   hex with SHA-256 and exact byte count.
3. Record `kern.osversion`, macOS build and the 22F82 dyld UUID/version
   (`BD993F39-…`, 1066.8) as identity context.
4. **Stop after the raw sample.** Do not parse `mappingOffset`/`mappingCount`,
   choose 32/48-byte entries, read `subCacheArray`/`patchInfo`/image table,
   read the target page/slot, or run a runtime lldb slot read. Preserve the
   bytes verbatim for a later exact-version parser.

All remote commands must report `MACPAD_SSH_EXIT=0 timeout=0`; `stat`/`shasum`/
`xxd` byte counts and SHA-256 must match; `cache_cdhash.py` must exit 0; any
short read, mismatch or nonzero exit stops. Never treat plausible values or
any 1286/15.6/iOS tool as validation.

## Exact artifact needed to resume

1. Exact dyld **1066.8** header definition/source (matching 22F82), covering
   `mappingOffset`/`mappingCount` and entry size/discriminator,
   `imagesOffset`/`imagesOffsetOld`, `subCacheArrayOffset/Count` and entry
   schema, and `patchInfoAddr`/`patchInfoSize` plus the patch-table version
   schema; or
2. the exact 22F82 `/usr/lib/dyld` 1066.8 binary (UUID `BD993F39-…`, SHA-256)
   or a 1066.8 source checkout for offline field-offset derivation; or
3. a 1066.8-versioned parser/tool positively validated on the captured 22F82
   header sample (not 1286/15.6, not plausibility-based).
4. The Phase 0 raw header samples and cache/subcache identities (main, `.01`,
   actual suffixes), with SHA-256.

The alias/canonical relationship is not yet proven inode-identical; that is a
read-only capture check for the backend window. No full cache copy is
authorized.

## Unchanged

The R014 tool audit, arm64/arm64e LLDB distinction, bounded-copy prohibition,
and stub-name vs errno-103 separation remain valid. Resolving the provider
later does not identify the kernel path that returned 103; that still requires
exact device-kernel policy-boundary evidence.

## Commands / exits

`find`/`rg`/`grep`/`otool -l` over `analysis/`, `docs/` and the author repo
exited 0; read-only `git log --oneline -1` reported `c97e219`. No tests,
builds, package, device or Git operations were run.

Full detail:
`docs/evidence/r015-r014-capture-amendment-20261018.md`.

(handover to codex now)
