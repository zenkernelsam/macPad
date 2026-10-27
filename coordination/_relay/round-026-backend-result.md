# Backend R026 result — F11H attribute-kind compatibility (offline)

Offline only: preserved R014/R024 request/module, preserved
original/converted MPSImage metallibs, converter source, and read-only
LLVM 16 library/table bytes. No compiler/service/F11H run, no device
change, no source/package/Git change, no attribute removal, no reader
check patched.

## Result

The rejected serialized attribute kind **71 is LLVM 16 `ZeroExt`**, not
`memory`. The exact LLVM 16.0.0 `Attribute::getNameFromAttrKind` table
(`_ZN4llvm9Attribute19getNameFromAttrKindENS0_8AttrKindE` at
`0x1f6ac4`, length table `0x3cc77a8`, pointer table `0x5da6320`) maps
kind 71 to length 7 at `0x3b4a918` = `zeroext\0`. Neighbours anchor the
table: 69 `willreturn`, 70 `writeonly`, 71 `zeroext`, 72 `byref`,
73 `byval`. The LLVM16 lib is SHA-256 `32589bf3…e3a6`; `llvm-as` is
Procursus LLVM 16.0.0 (`36554b64…236`).

The host LLVM17 textual IR renders those groups as `memory(...)`; that
is a **cross-version decode** and is explicitly not used to name kind 71.

## Provenance

- Type-15 AIR bitcode: 7,372 bytes, raw SHA-256
  `042303be36e32a7c2755ffae074fc66e99bc6260009b3abcb970a5d87167b849`.
- Converted F11H bitcode: 7,372 bytes, **byte-identical** to the type-15
  module. The client forwards the converter output unchanged.
- Original F11H bitcode: 6,916 bytes, raw SHA-256
  `ce018722f1d9058534380e6fa0aade64cdaef3cbbd0e806d02d769810273ec9b`.
- `retarget_air` (`misc/repack_metallib_macabi.py:709-758`) runs
  `llvm-dis`, rewrites the one target triple, applies only the requested
  known lowerings, then `llvm-as` (Procursus LLVM 16.0.0). It has no
  zeroext/attribute edit. The converter's re-serialization is therefore
  the step that produced the exact rejected byte sequence.
- The semantic attribute set is preserved from the original module; the
  wire ordinal is LLVM 16's. This is a re-encoding/ordinal incompatibility,
  not an introduced new attribute. The original's exact zeroext sites
  were not dumped (THEORY label retained for that part).

## Missing artifact

The exact zeroext parameter/return scope and all occurrences, plus the
reader's LLVM version (the R024 `Reader` string is empty). The next
read-only step is an LLVM16 `llvm-dis` dump of the preserved converted
module:

```
/var/jb/usr/lib/llvm-16/bin/llvm-dis <converted-F11H.bc> -o F11H.ll
```

That is a device execution and was deliberately not run in this packet.

## Compatibility options and next A/B

1. **Reader-matched re-encoding (preferred).** The converter already takes
   `--llvm-dis`/`--llvm-as`; re-assemble the same AIR text with an
   `llvm-as` whose enum ordering matches the reader. No attribute removal,
   only wire ordinals change. Requires the reader version and an older
   parser that accepts the IR features.
2. **Attribute-aware lowering (fallback).** Lower only the exact zeroext
   sites to explicit `zext`/mask via `apply_air_lowerings` and drop the
   attribute, preserving unsigned semantics. Not blind stripping.

Next isolated A/B:

- A0 read-only LLVM16 dump → enumerate zeroext sites and real memory attrs.
- A1 offline disposable converter copy → re-assemble with a reader-matched
  `llvm-as`; verify the raw zeroext kind and semantics.
- A2 future private-endpoint F11H control → acceptance requires no
  `Unknown attribute kind (71)` and a real reply or the next concrete
  error.

## Limits

No LLVM16 headers/`Attributes.inc`/`BitcodeWriter` source locally; the
enum name table is from the exact binary and the writer enum-value
contract is inferred from the reader diagnostic. The host LLVM17 IR is
not authoritative for kind 71. The exact scope and reader version remain
missing. No device compiler/service/F11H run.

Evidence: `docs/evidence/r026-f11h-attribute-compatibility-20261018.md`,
`coordination/artifacts/R026-f11h-attribute-compatibility-20261018.json`,
`analysis/r026-f11h/` (extraction script, modules/IR, LLVM16 table
extraction, hashes).

(handover to codex now)
