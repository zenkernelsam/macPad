# Round 013 — frontend result: exact 22F82 Quarantine stub provider not locally resolvable

Date: 2026-10-18. Read-only offline. No SSH/device, no extraction from the
device, no source/test/build-input change, no dependency install, no Git write.
Added only this result and
`docs/evidence/r013-quarantine-stub-cache-search-20261018.md`.

Candidate HEAD: `c97e219`.

## Verdict

The exact 22F82 shared-cache/fixup input needed to resolve
`libquarantine` stub `0x18b321bc0` / GOT `0x1dfef8f88` is **not present
locally**. The available sparse exact image keeps `__TEXT` only; its GOT,
LINKEDIT, symtab/dysymtab and fixup/bind data are absent or zero. The exact
provider therefore remains unresolved.

- Do not use 15.6 `___sandbox_ms` as a 22F82 fact.
- Do not infer the actual 103 branch from any provider symbol name; R009's
  three possible `__mac_syscall` ENOPOLICY sources remain unassigned.

## Identity facts

| Item | Value |
| --- | --- |
| exact image | `analysis/r005-responsibility-inputs/libquarantine.dylib` |
| SHA-256 | `f5989c8f4afc9985e95795bf683679eed629af8b5ae288a0fb87317ba0e844f2` |
| UUID / arch | `A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3` / arm64e `MH_DYLIB`, macOS 13.4 |
| `__TEXT` | `0x18b31f000` + `0x3000`; stub `0x18b321bc0` at file `0x2bc0` |
| GOT slot | `0x1dfef8f88` |
| 22F82 cache | UUID `BE59BB71-7B69-3EFC-86A8-5625A077AE2E`; WS spindump slide `0x1ba40000`, base `0x19ba40000` |
| unslid cache base | `0x180000000` (`0x19ba40000 - 0x1ba40000`) |
| arm64e live slide | `0x123ec000` (`0x19d70b000 - 0x18b31f000`; libsystem_kernel `0x1927ab000 - 0x1803bf000` gives the same slide) |
| sparse file size | `0x5046e080` (1,346,822,272 B) |
| slot cache offset | `0x5fef8f88`; beyond the sparse file by `0xfa8af08` |

The arm64e live image-list check is a positive identity check: both
libquarantine and the R005 arm64e libsystem_kernel (`34A49B54-…`) agree on
slide `0x123ec000`. The WS spindump is arm64 and lists a different
`libsystem_kernel` UUID `C76E6BED-…`; it must not be substituted for the
arm64e stub target.

## Search result

Searched only existing local analysis/cache/fixup material:

- `analysis/r005-responsibility-inputs/`: sparse exact image, 8 × 32 KB
  dependency slices, metadata. No cache/subcache, no fixup/bind table, no full
  dependency images.
- `analysis/**/dyld_shared_cache*`: only iOS 16.3 cache
  `136ECC70-…` (376,832 B) and `3C5E5C44-…` (115,867,648 B); other
  filename hits are dyld source files, not cache data. Not 22F82.
- `analysis/share-review-20261015`, `analysis/share-review-20261018`,
  `analysis/macPad-15.6…`: 15.6/24G90 share material; not used as 22F82.
- `analysis/device-leftovers…/libsystem_kernel.dylib`: UUID `6E4A96AD-…`;
  not the exact Ventura dependency.
- `analysis/r007-ws-sample/ws-spindump-20261018.txt` and
  `loaded-images.json`: cache UUID/slide/image bases only, no cache bytes or
  fixup record.
- `coordination/artifacts/r006-lldb-pane.txt`, `r007-lldb-pane-full.txt`:
  image lists/thread stacks; no slot memory or register value.
- No local 22F82 `.a2s`/`.map`; the historical 22D68 host cache is macOS
  13.2.1, not 22F82, and was not used.

## Structural checks on the sparse image

1. **Address/file coverage:** no segment or section covers `0x1dfef8f88`
   (`__TEXT` `0x18b31f000+0x3000`; `__DATA_CONST` `0x1d41ac958+0x50`;
   `__AUTH_CONST` `0x1dd78c558+0x110`; `__DATA` `0x1d8ca4fe0+0x10`;
   `__LINKEDIT` `0x22193c000+0x80`). Its cache offset `0x5fef8f88` is beyond
   the sparse file end `0x5046e080`. The image-local `__auth_got`
   (`0x1dd78c578+0xf0`) and `__got` (`0x1d41ac958+0x10`) are zero and are not
   the stub target page.
2. **Relocation/naming:** `LC_SYMTAB` all zero; `LC_DYSYMTAB` all 18 fields
   zero; no `LC_DYLD_INFO`/`LC_DYLD_CHAINED_FIXUPS`; the exports trie range
   is zero-filled and `dyld_info` reports a LINKEDIT overlap; `otool -Iv`
   reports 30 indirect entries extending past the table; `nm -a` says
   `no symbols`; no `sandbox_ms`/`mac_syscall` strings (only `Quarantine`).
3. **Stub shape:** `adrp x17,0x1dfef8000; add #0xf88; ldr x16,[x17];
   braa x16,x17` confirms the slot address but contains no name.

Both intended checks (coverage and relocation) fail independently, so no local
resolution is possible.

## Minimum exact artifact/range still needed

Prefer a read-only cache-side extraction:

1. 22F82 arm64e `dyld_cache_header` + mapping table for UUID
   `BE59BB71-…`, identifying the exact subcache/file offset for unslid
   `0x1dfef8f88` (main vs `.01` must come from the mapping, not a guess);
   record the subcache SHA-256.
2. The cache page containing the slot (minimum 8 bytes at `0x1dfef8f88`;
   page size per the 22F82 cache header, 16 KB in the observed Ventura
   mapping) plus the covering chained-fixup/bind record (target VA/import
   ordinal/name). A raw 8-byte value alone is insufficient if it is an
   unresolved fixup placeholder.
3. If the fixup yields only a target VA, the target image UUID and symbol
   table/export trie/`.a2s`/`.map` range for that address. The arm64e
   candidate is libsystem_kernel `34A49B54-…`, but it is not assumed until the
   fixup record says so.
4. Cache identity/build: 22F82, UUID, subcache name/size/SHA-256.

Alternatively, on an owned arm64e generation, a read-only debugger read of
runtime slot `0x1f22e4f88` (slide `0x123ec000`) with PAC strip and
`image lookup -a`/`dladdr` would directly identify the provider; this is
device work and was not performed.

## Commands / exits

`find` for local cache files, targeted `rg`/`grep` for the UUID/slot/stub,
`shasum`, `file`/`lipo -info`, `otool -l`, `otool -Iv`, `nm -a`,
`xcrun llvm-objdump -d`, Python load-command/offset arithmetic and read-only
`git log`: all exit 0. No tests, builds, package, device commands or Git
writes were run.

## Limits

- No 22F82 cache/subcache/fixup record exists locally; the sparse image cannot
  stand in for it.
- arm64 vs arm64e libsystem_kernel identity is resolved only for the arm64e
  image list; the exact stub target still needs the fixup record.
- No device cache mapping, policy, or 103 branch was observed. No provider
  name or 103 cause is claimed.

Full detail:
`docs/evidence/r013-quarantine-stub-cache-search-20261018.md`.

(handover to codex now)
