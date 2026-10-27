# Frontend R027 result — serialized attribute wire-ID audit

Status: **completed; R026's wire-ID claim is corrected.** Local/offline/
public-source only: no device, source/package/build/Git edit, no attribute
removal or workaround.

## Exact finding

Upstream LLVM 16.0.0 (tag `llvmorg-16.0.0`, commit
`ca289b8aaad5e220a66c7afb25387b9d01509b1c`) defines the bitcode wire enum:

```text
ATTR_KIND_Z_EXT       = 34
ATTR_KIND_ARGMEMONLY  = 45
ATTR_KIND_NO_CALLBACK = 71
ATTR_KIND_MEMORY      = 86
```

Reader maps 71 -> `Attribute::NoCallback`; writer maps `NoCallback` -> 71.
**Wire ID 71 is `nocallback`, not `ZeroExt`.** The internal
`Attribute::getNameFromAttrKind` ordinal 71=zeroext is not a bitcode wire ID
and is not used.

Independent bitstream parsing of the preserved modules:

- converted F11H / type-15 request raw SHA-256
  `042303be…b849`, 7,372 bytes, byte-identical: exactly one function-level
  record `[0,45, 0,71, 0,62, 0,63, 0,18, 0,61]` =
  `argmemonly, nocallback, nofree, nosync, nounwind, willreturn`.
- zero wire kind-34 (ZExt) records; zero wire kind-86 (Memory) records.
- original preserved F11H raw SHA-256 `454ad619…6264`, 6,924 bytes: no
  kind-71 record; corresponding record is `[0,45, 0,18]`.
- The textual `memory(...)` seen by host LLVM17 is the upgrade of wire
  `argmemonly` 45, not wire 71 or 86.
- The runtime `Unknown attribute kind (71)` is LLVM `parseAttrKind`
  returning `None` for an unmapped code; it does not mean 71 decoded as
  zeroext.

## Corrections

1. R026's `wire kind 71 = ZeroExt` is rejected.
2. Any zeroext-lowering interpretation is unsupported: there are no ZExt
   wire records.
3. R026's original-module `6,916 B / ce018722…` is not reproduced by the
   preserved `orig-F11H.bc` (6,944 file / 6,924 raw / `454ad619…`); its
   provenance needs correction. The converted/request `042303be…` is exact.
4. The host LLVM17 textual decode is not wire evidence.

## Reader limit

Producer is LLVM16.0.0; the Apple reader (`libLLVM` UUID
`3C9D9D6C-CC92-326A-911C-141BC96DC8BE`, host `MTLCompiler`
`482EE528-…`) has an empty version string and rejects wire 71. Its full wire
table is not proven and the exact libLLVM binary is not preserved. Required
for a reader-side claim: exact libLLVM image/hash, xref of
`"Unknown attribute kind ("`, and the `parseAttrKind` switch/table. No
reader patch or attribute removal is proposed.

Files:
- `docs/evidence/r027-attribute-wire-audit-20261018.md`
  (SHA-256 `eae456883b37afb93cd5f3af8d6bbf4be13f92cff5680e305fac79a9c0007212`)
- raw evidence under `analysis/r027-attribute-wire/` (gitignored):
  `parse_paramattr.py`, `wire-summary.txt`, `llvm16-source-excerpts.txt`,
  `sources.json`, `wire-audit.json`, fetched `llvm16/` sources.

No background tasks remain running or queued.

(handover to codex now)
