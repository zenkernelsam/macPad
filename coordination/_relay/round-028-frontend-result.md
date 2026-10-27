# Frontend R028 result — intrinsic auto-upgrade review

Status: **review complete; no implementation, no attribute removal.**
Offline/public-source only: no device, source/package/build/Git edit.

## Mechanism (exact LLVM16 sources)

The materialization is in the bitcode reader, not llvm-dis formatting:

- `BitcodeReader::globalCleanup` calls `UpgradeIntrinsicFunction` for every
  function (`BitcodeReader.cpp:3678-3687`).
- `UpgradeIntrinsicFunction` unconditionally re-applies
  `Intrinsic::getAttributes` when the function is an intrinsic, even if no
  signature upgrade occurs (`AutoUpgrade.cpp:1171-1182`).
- `Function` construction also sets intrinsic attrs (`Function.cpp:413-422`;
  `Module.cpp:144-154`).
- `Intrinsics.td:127-139` marks `IntrNoCallback/NoSync/NoFree/WillReturn` as
  default; lifetime declarations are `DefaultAttrsIntrinsic`
  (`Intrinsics.td:1189-1200`).
- `llvm-dis` prints the materialized attrs; `llvm-as` serializes them via
  `writeAttributeGroupTable` (`BitcodeWriter.cpp:822-866`), producing wire
  71 (`nocallback`), 62 (`nofree`), 63 (`nosync`), 61 (`willreturn`).

Serialized scope: the function-level PARAMATTR_GROUP record for
`llvm.lifetime.start.p0` and `llvm.lifetime.end` declarations. Calls use a
different group (#6) and are unchanged.

## Exact preserved evidence

- Original F11H raw: 6,924 B, `454ad619…6264`; lifetime record
  `[0,45, 0,18]` (argmemonly, nounwind); no 71/62/63/61.
- Converted F11H and type-15 request raw: 7,372 B, `042303be…b849`;
  record `[0,45, 0,71, 0,62, 0,63, 0,18, 0,61]`.
- Original and converted `.ll` text differ only in the ModuleID line; both
  show `nocallback nofree nosync nounwind willreturn memory(argmem:
  readwrite)`. Textual equality is expected after reader materialization and
  is **not** raw/provenance equality.
- The textual `memory(...)` is the upgrade of wire `argmemonly` 45, not wire
  memory 86.
- The reader stops at 71; acceptance of 62/63/61 is not proven. Handling
  only 71 is insufficient.

## Conservatism and attribute classes

Preserving the original intrinsic declaration attribute records is
semantically conservative: LLVM16 re-applies intrinsic defaults in memory,
while an older reader receives only codes it knows. It is conservative only
if parameter contracts and all non-intrinsic attrs are preserved and the
target profile/reader is gated.

- Parameter/ABI contracts: `immarg`, `nocapture`, `nonnull`, `align`,
  `dereferenceable`, `byval`, `sret`, `inalloca` — must remain exact.
- Function semantic/optimization annotations: `nocallback`, `nofree`,
  `nosync`, `willreturn`, `nounwind`, `argmemonly/memory` — not call-layout
  ABI, but semantic promises; do not blanket-remove.

## Fail-closed criteria supplied

Profile/triple/data-layout/reader gate; original PARAMATTR_GROUP restoration
for intrinsic declarations; byte/token-identical non-intrinsic and parameter
attrs; raw token-stream comparison with only allowlisted intrinsic
differences; IR/body/constants/metadata/calling-convention equality; retarget
ABI validation; exact target-reader acceptance with no `Unknown attribute
kind`; no textual-only equality gate.

## Regression-test design supplied

Minimal lifetime fixture (original wire has no 71/62/63/61; restored output
and reader acceptance), non-intrinsic attribute guard, textual-equality trap,
unsupported profile/reader fail-closed, real F11H body/metadata/constant
equality, negative `immarg`/`nocapture` changes, and wire-token/semantic
assertions rather than implementation-mirroring tests.

## Provenance limit

R026's original `6,916 B / ce018722…` is not reproduced by the preserved
`orig-F11H.bc` (6,944/6,924, `454ad619…`); backend R028 owns reconciliation.
No claim that the original hash is fixed.

Files:
- `docs/evidence/r028-intrinsic-upgrade-review-20261018.md`
  (SHA-256 `836237831757922c28161cb9ec4053fd5e6512a2dd1447b991214e423f0706e7`)
- raw evidence under `analysis/r028-attribute-review/` (gitignored):
  `llvm16/llvm16-source-excerpts.txt`, `llvm16/sources.json`,
  `text-vs-raw.txt`, `upgrade-review.json`.

No background tasks remain running or queued.

(handover to codex now)
