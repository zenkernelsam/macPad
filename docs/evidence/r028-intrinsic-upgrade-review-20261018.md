# R028 — intrinsic auto-upgrade and original-attribute preservation review

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-028-to-frontend.md`. Offline/public-source review
only: no device/source/build/package/Git edit, no attribute removal or
converter implementation. Raw evidence is under
`analysis/r028-attribute-review/` (gitignored).

## 1. Exact LLVM16 mechanism: the reader materializes intrinsic attributes

Upstream LLVM 16.0.0, tag `llvmorg-16.0.0`, commit
`ca289b8aaad5e220a66c7afb25387b9d01509b1c`:

- `Intrinsics.td:127-139`: `IntrNoCallback`, `IntrNoSync`, `IntrNoFree` and
  `IntrWillReturn` are marked "Applied by default" via
  `IntrinsicProperty<1>`.
- `Intrinsics.td:1189-1200`: `llvm.lifetime.start/end` are
  `DefaultAttrsIntrinsic` with `IntrArgMemOnly, IntrWillReturn,
  NoCapture<ArgIndex<1>>, ImmArg<ArgIndex<0>>`. Because they are
  `DefaultAttrsIntrinsic`, the four default properties also apply.
- `Function.cpp:413-422`: a function whose name is a known intrinsic gets
  `setAttributes(Intrinsic::getAttributes(...))` at construction.
- `BitcodeReader.cpp:3964-3976`: the reader creates the function and then
  sets the attributes from the bitcode group.
- `BitcodeReader.cpp:3678-3687`: `globalCleanup()` then calls
  `UpgradeIntrinsicFunction` for **every** function.
- `AutoUpgrade.cpp:1171-1182`: even when
  `UpgradeIntrinsicFunction1` returns false, the function does
  `F->setAttributes(Intrinsic::getAttributes(...))` if
  `F->getIntrinsicID() != 0`. The comment is explicit: "Upgrade intrinsic
  attributes. This does not change the function."
- `BitcodeWriter.cpp:822-866`: `writeAttributeGroupTable()` serializes the
  in-memory attribute set; `getAttrKindEncoding` maps `NoCallback` to wire
  71 (R027).

**FACT:** the materialization happens in the bitcode reader
(`globalCleanup` → `UpgradeIntrinsicFunction` → `Intrinsic::getAttributes`),
not in `llvm-dis` formatting. `llvm-dis` prints the already-materialized
attributes; `llvm-as` then writes them into the PARAMATTR_GROUP record.

The serialized scope is the **function-level attribute group of the
`llvm.lifetime.start.p0` and `llvm.lifetime.end` declarations**. The call
instructions use a different group (#6, `nounwind`) and are not the record
that changed.

## 2. What the preserved modules show

R027's independent bitstream parser gives:

```text
original F11H:  raw 6,924 B / 454ad619475cc081e7d57de78d6d4e2f0abdc818c5c7d38e230f72739c806264
  lifetime-level corresponding record: [0,45, 0,18]  (argmemonly, nounwind)
  kind 71/62/63/61: absent

converted F11H and type-15 request: raw 7,372 B / 042303be36e32a7c2755ffae074fc66e99bc6260009b3abcb970a5d87167b849
  record: [0,45, 0,71, 0,62, 0,63, 0,18, 0,61]
  = argmemonly(45), nocallback(71), nofree(62), nosync(63),
    nounwind(18), willreturn(61)
```

The original and converted textual IR differ only in the `ModuleID` line
(`orig.ll` SHA `246d06b7…`, `conv.ll` SHA `dab33f38…`); both show
`attributes #5 = { nocallback nofree nosync nounwind willreturn
memory(argmem: readwrite) }` and both lifetime declarations use #5. This is
the exact textual/raw boundary:

- **Textual equality is expected** because the reader materialized the
  intrinsic defaults before `llvm-dis` printed either module.
- **Raw equality is not present**: the original wire record lacks
  71/62/63/61; the converted/request wire record contains them.
- The textual `memory(...)` is the LLVM16/17 upgrade of wire `argmemonly`
  (45), not wire `memory` (86); wire 86 count is zero in all three modules.

The runtime diagnostic `Unknown attribute kind (71)` stops at 71. The same
materialized record also contains 62 (`nofree`), 63 (`nosync`) and 61
(`willreturn`), whose acceptance by the older reader is **not proven**.
Removing or handling only 71 would not settle the compatibility question.

## 3. Is preserving original intrinsic attributes semantically conservative?

Yes, with precise boundaries:

- In LLVM16 the four defaults are **redundant with the intrinsic
  definition**; the reader re-applies them in memory. Restoring the
  original wire group for the intrinsic declaration preserves the same
  in-memory semantics in LLVM16 and avoids sending newer wire codes to an
  older reader.
- It is conservative only if the converter also preserves parameter
  contracts and all non-intrinsic attributes. A blanket "strip attributes"
  transform is not conservative.
- If the source/target triple or reader profile changes, preservation must
  be gated by an exact supported-profile check; the original encoding is
  not automatically valid for a different reader/ABI.

Attribute classification (do not remove all):

| Class | Examples here | Rule |
| --- | --- | --- |
| Parameter/ABI or contract | `immarg`, `nocapture`, `nonnull`, `align`, `dereferenceable`, `byval`, `sret`, `inalloca` | Must remain exact; removal/change can break verifier, lowering or ABI |
| Function semantic/optimization | `nocallback`, `nofree`, `nosync`, `willreturn`, `nounwind`, `argmemonly`/`memory` | Not call-layout ABI, but semantic promises; preserve original encoding or normalize only with an exact reader/profile gate |

`nocallback/nofree/nosync/willreturn` are optimization/semantic annotations,
not parameter ABI contracts. They are still not safe to blanket-remove:
they encode behavior assumptions, and the target reader may require them or
may not know their newer codes.

## 4. Fail-closed review criteria

A future narrow converter should fail closed unless all of the following
hold:

1. **Profile/target gate:** exact supported converter profile, source/target
   triple, data layout, pointer width/address spaces, target features and
   reader identity are known; unknown profiles/triples/readers fail.
2. **Original-record restoration:** preserve the original PARAMATTR_GROUP
   records (function and parameter attributes) for intrinsic declarations;
   do not reconstruct them from materialized IR. If an original record is
   missing or ambiguous, fail closed.
3. **Non-intrinsic attributes:** every function/return/parameter attribute
   must be byte/token-identical; no normalization of user functions.
4. **Parameter contracts:** `immarg`, `nocapture`, `nonnull`, alignment,
   `byval`, `sret`, etc. must be exact.
5. **Raw/token equality:** compare the bitstream PARAMATTR_GROUP token
   streams; only explicitly allowlisted intrinsic-declaration
   materialization differences may remain. If the converter claims original
   preservation, require raw equality for those records (full raw hash
   equality where feasible).
6. **Semantic equality:** functions, blocks, instruction opcodes/operands/
   types, constants, global initializers, aliases, comdats, personalities,
   calling conventions, linkage/visibility/section/alignment must match.
7. **Metadata equality:** named metadata, module flags, debug info, TBAA,
   operand bundles and attachments must match.
8. **Retarget safety:** if the triple/data layout changes, validate pointer
   sizes, address spaces, ABI alignment and target features; reject
   unsupported combinations.
9. **Reader acceptance:** the exact target reader must parse the output
   without `Unknown attribute kind` or another diagnostic and return a real
   reply; no fallback/constant return or attribute deletion is acceptance.
10. **No textual-only gate:** identical `.ll` text is not proof of raw
    preservation or target-reader compatibility.

## 5. Focused regression-test design (review only, not implementation)

1. **Minimal lifetime fixture:** construct an LLVM16 module with
   `llvm.lifetime.start/end` declarations whose bitcode group omits
   71/62/63/61. Assert the original wire has none of them; run the future
   converter; assert the output restores the original group and the exact
   target reader accepts. This is an invariant/wire test, not a mirror of
   the converter's internal functions.
2. **Non-intrinsic guard:** a normal function with custom function and
   parameter attributes must be byte/token-identical after conversion.
3. **Textual-equality trap:** two modules with the same `.ll` text but
   different raw PARAMATTR_GROUP records must fail the equality gate unless
   the difference is explicitly allowlisted.
4. **Reader/profile gate:** unsupported triple, reader or profile fails
   closed with no output.
5. **Real F11H:** assert original/converted/request hashes, body/metadata/
   constant equality, only intrinsic declaration attribute differences, and
   target-reader acceptance; if the target reader lacks 71, the output must
   not contain it.
6. **Negative contracts:** removing/changing `immarg`, `nocapture` or any
   non-intrinsic attribute fails closed.
7. **No oracle by text:** tests compare raw token streams and semantic
   invariants, not only `llvm-dis` output.

## 6. Provenance and limits

- The converted F11H and type-15 request are byte-identical
  (`042303be…`); the original is a distinct raw module (`454ad619…`).
- R026's `6,916 B / ce018722…` original value is not reproduced by the
  preserved `orig-F11H.bc` (6,944 file / 6,924 raw / `454ad619…`). Backend
  R028 owns the provenance reconciliation; this review does not claim the
  original hash is fixed.
- No converter patch, attribute removal, device run, package/source/Git
  change or reader patch is made.
- The reader remains the Apple `libLLVM`
  `3C9D9D6C-CC92-326A-911C-141BC96DC8BE`; its exact wire table is not
  disassembled here (R027 limit). The runtime rejection proves only that
  code 71 is absent from the reader's table, not what 62/63/61 do.

Raw evidence:
`analysis/r028-attribute-review/llvm16/`
(source files and `llvm16-source-excerpts.txt` SHA-256 `aa7ee5bd…da1d`),
`sources.json` (SHA-256 `0c8476e6…fbc7`),
`text-vs-raw.txt` (SHA-256 `cc4a4de7…7c06`),
`upgrade-review.json` (SHA-256 `4b17610b…ea23`).

(handover to codex now)
