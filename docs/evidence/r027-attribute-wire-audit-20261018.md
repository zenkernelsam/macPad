# R027 — independent serialized attribute wire-ID audit

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-027-to-frontend.md`. Local/offline/public-source
work only: no iPad/device probe, no source/package/build/Git edit, no
attribute removal or workaround. Raw evidence is under
`analysis/r027-attribute-wire/` (gitignored).

## 1. Exact upstream LLVM 16.0.0 wire mapping

Fetched read-only from the upstream release tag `llvmorg-16.0.0`, commit
`ca289b8aaad5e220a66c7afb25387b9d01509b1c`:

```text
https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-16.0.0/llvm/include/llvm/Bitcode/LLVMBitCodes.h
  SHA-256 41c9d3803cc77a23bca2040142d45db3fd1357ac9b13303645c0abe58c460912
https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-16.0.0/llvm/lib/Bitcode/Reader/BitcodeReader.cpp
  SHA-256 171ba64ae6c7b4455c5318b7127c219c6b97f9525dcc78784b29ac34e61550df
https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-16.0.0/llvm/lib/Bitcode/Writer/BitcodeWriter.cpp
  SHA-256 6da61902bc43d561cfd3821c8488fd908dc3d49afff93cde7178e93e646351e5
```

Exact wire enum (`LLVMBitCodes.h:627-715`):

```text
ATTR_KIND_Z_EXT        = 34
ATTR_KIND_ARGMEMONLY   = 45
ATTR_KIND_NO_CALLBACK  = 71
ATTR_KIND_MEMORY       = 86
```

Exact reader mapping (`BitcodeReader.cpp:1899-2075`):

```text
case bitc::ATTR_KIND_NO_CALLBACK: return Attribute::NoCallback;
case bitc::ATTR_KIND_Z_EXT:       return Attribute::ZExt;
case bitc::ATTR_KIND_MEMORY:      return Attribute::Memory;
```

Exact writer mapping (`BitcodeWriter.cpp:642-810`):

```text
case Attribute::NoCallback: return bitc::ATTR_KIND_NO_CALLBACK;   // 71
case Attribute::ZExt:       return bitc::ATTR_KIND_Z_EXT;         // 34
case Attribute::Memory:     return bitc::ATTR_KIND_MEMORY;        // 86
```

The diagnostic path is exact (`BitcodeReader.cpp:2082-2087`):

```cpp
Error BitcodeReader::parseAttrKind(uint64_t Code, Attribute::AttrKind *Kind) {
  *Kind = getAttrFromCode(Code);
  if (*Kind == Attribute::None)
    return error("Unknown attribute kind (" + Twine(Code) + ")");
  return Error::success();
}
```

**FACT: serialized wire ID 71 in standard LLVM 16.0.0 is
`ATTR_KIND_NO_CALLBACK` (`nocallback`). It is not `ZeroExt`; `ZeroExt` is
wire ID 34.** The internal `Attribute::getNameFromAttrKind` table seen in
R026 maps internal AttrKind 71 to `zeroext`, but that is an internal enum,
not the bitstream wire enum. It is not used for any wire-ID conclusion here.

## 2. What the preserved F11H modules actually serialize

A minimal independent bitstream parser
(`analysis/r027-attribute-wire/parse_paramattr.py`,
SHA-256 `969f6d8e…35c4`) was used to decode the PARAMATTR_GROUP block from
the preserved modules. It does not build an LLVM module and does not use
`Attributes.td` or any name-table ordinal. Raw summary:
`analysis/r027-attribute-wire/wire-summary.txt` (SHA-256 `a348d6cd…9057`).

| Module | File bytes | Raw bitcode bytes | Raw SHA-256 |
| --- | --- | --- | --- |
| original F11H | 6,944 | 6,924 | `454ad619475cc081e7d57de78d6d4e2f0abdc818c5c7d38e230f72739c806264` |
| converted F11H | 7,392 | 7,372 | `042303be36e32a7c2755ffae074fc66e99bc6260009b3abcb970a5d87167b849` |
| type-15 request | 7,372 | 7,372 | `042303be36e32a7c2755ffae074fc66e99bc6260009b3abcb970a5d87167b849` |

The converted F11H and type-15 request are byte-identical, confirming R026's
provenance. Exact PARAMATTR_GROUP findings:

```text
converted / request:
  one function-level record (bitcode grp=4, idx=0xFFFFFFFF):
    raw tokens = [0,45, 0,71, 0,62, 0,63, 0,18, 0,61]
    wire kinds = argmemonly(45), nocallback(71), nofree(62),
                 nosync(63), nounwind(18), willreturn(61)
  wire kind 34 (ZeroExt): 0 records
  wire kind 86 (Memory):  0 records

original:
  corresponding record = [0,45, 0,18]  (argmemonly, nounwind)
  wire kind 71: 0 records
  wire kind 34: 0 records
  wire kind 86: 0 records
```

The converted record's names match the textual group that contains
`nocallback`; the `argmemonly` wire 45 is upgraded by the reader to
`memory(argmem: readwrite)` at `BitcodeReader.cpp:2162-2164`. That textual
`memory(...)` is a cross-version upgrade, **not** wire kind 86.

## 3. Corrections to prior unsupported claims

1. **Rejected:** R026's `serialized kind 71 = ZeroExt`. Exact LLVM16 wire
   71 is `ATTR_KIND_NO_CALLBACK`/`nocallback`. The internal name-table
   ordinal 71 = `zeroext` is not wire evidence.
2. **Rejected:** any zeroext-lowering interpretation. The converted/request
   module contains **zero** wire kind-34 (ZExt) records; the only 71 record
   is `nocallback`.
3. **Corrected:** the runtime diagnostic `Unknown attribute kind (71)` is
   `parseAttrKind` returning `Attribute::None` for a code with no reader
   mapping. It does **not** mean the reader decoded 71 as zeroext.
4. **Corrected:** the host LLVM17 textual `memory(...)` is the upgrade of
   wire `argmemonly` 45; wire `memory` 86 count is zero. It is not a
   cross-version decoding of wire 71.
5. **Corrected provenance:** R026's original F11H `6,916 B /
   ce018722…` is not reproduced by the preserved
   `analysis/r026-f11h/orig-F11H.bc` (6,944 B file / 6,924 B raw /
   `454ad619…`). The source of the 6,916/`ce018722…` value is not preserved;
   the converted/request raw `042303be…` is exact and decisive. This does
   not affect the wire-71 finding.

No attribute was removed, rewritten, lowered, or worked around.

## 4. Reader-side limit and exact disassembly needed

The producer is LLVM 16.0.0 (Procursus) and its exact wire mapping is the
upstream one above. The reader is the Apple-side LLVM in
`MTLCompiler.framework`/`libLLVM`; its version string is empty
(`Reader: 'LLVM '`). The runtime images are recorded from the R014 service
report as:

```text
MTLCompiler  482EE528-9D70-3ED7-A746-D19BB741245B
libLLVM      3C9D9D6C-CC92-326A-911C-141BC96DC8BE
```

The exact libLLVM reader binary/hash is **not** preserved locally (the
66,816-byte `MTLCompilerService.real.orig` is the service shim UUID
`6D2CFE56-…`, not the LLVM library). Therefore this audit cannot claim the
Apple fork's full wire table. Exact disassembly needed for a reader-side
claim:

1. Preserve the 22F82 `libLLVM` image UUID
   `3C9D9D6C-CC92-326A-911C-141BC96DC8BE` (and its MTLCompiler host
   `482EE528-…`) with SHA-256.
2. Locate the xref to the literal `"Unknown attribute kind ("` in that exact
   image and disassemble the `parseAttrKind`-equivalent function and its
   attribute-code switch/table.
3. Record which wire codes around 34/45/62/63/71/86 the reader accepts; the
   runtime already proves code 71 is absent. This is required before any
   reader-matched re-encoding or removal proposal; no such proposal is made
   here.

## 5. Limits

- Public upstream LLVM16 release sources are exact; the Apple reader fork is
  only identified by UUID and the runtime rejection.
- The parser is independent but minimal: it decodes the PARAMATTR_GROUP
  bitstream and token markers, not the whole LLVM module. It was
  cross-checked against the textual group contents and the exact reader
  token semantics.
- No device, source, package, build, Git or attribute workaround was used.
- The audit does not authorize removing `nocallback`, lowering `zeroext`, or
  patching the reader; those are separate decisions requiring reader-side
  evidence.

Raw evidence: `analysis/r027-attribute-wire/llvm16/` (sources),
`llvm16-source-excerpts.txt` (SHA-256 `05c76b52…b8a3`),
`sources.json` (SHA-256 `84323b17…c217`),
`wire-summary.txt` (SHA-256 `a348d6cd…9057`),
`wire-audit.json` (SHA-256 `b68c03a1…d18d`).

(handover to codex now)
