# Backend R028 result — provenance reconciliation and isolated F11H candidate

No production compiler/WS/generation/package/source/Git/policy/marker
change. One bounded direct type-15 control vs candidate in the owned
private endpoint. Credential transient.

## Provenance reconciled

- Original F11H is one 6,944-byte MTLB record at 11,426,375
  (`0fa8f789…`). Wrapper 6,936 (`e3876cba…`), wrapper-declared payload
  6,916 (`ce018722…`). The disputed 6,924/`454ad619…` is payload plus the
  record's 8 zero padding bytes; there is no second original.
- Converted F11H: 7,392 record, payload 7,372/`042303be…`; type-15 request
  byte-identical.

## Exact wire change and auto-upgrade

- Original wire group 4: `[argmemonly(45), nounwind(18)]`.
- Converted/request group 4:
  `[argmemonly(45), nocallback(71), nofree(62), nosync(63), nounwind(18), willreturn(61)]`.
  The converted module has 16 PARAMATTR groups vs original 14; the extra
  group is the split of the shared write-texture group.
- LLVM16 applies canonical intrinsic attributes on function construction
  and in `UpgradeIntrinsicFunction`
  (`Function.cpp:418-422`, `AutoUpgrade.cpp:1174-1181`,
  `BitcodeReader.cpp:3676-3684`, lifetime definitions
  `Intrinsics.td:1191-1196`). Therefore llvm-dis/as cannot preserve the
  original lifetime record; R027's textual equality was the reader's
  in-memory upgrade. ID71 (`nocallback`) is introduced by the round-trip,
  not present in the original wire. R026's ZeroExt attribution remains
  withdrawn.

## Candidate

The original F11H record was retargeted **in place** from the 24-byte
`air64-apple-macosx13.4.0` to the same-length `air64-apple-ios19-macabi`,
preserving every original wire attribute, body and metadata (only 12
suffix bytes inside the triple differ; no llvm-dis/as, no attribute
removal). Candidate module 6,944/`2f2de66b…`; wire groups identical to
the original (no ID71). Candidate metallib 17,293,876/`ebfd4f92…` built
from the converted container: F11H replaced, bitcode −448 bytes, all
1,824 other modules byte-identical, container macabi 19.0 unchanged.
Candidate type-15 data 7,184/`38f086cf…` with archive size fields and
wrapper `bcsize` updated.

## A/B

The library-path attempt was invalid for candidate testing: the
metal2metal adapter's fixed route resolves the installed companion, so
both runs sent the converted module (identical FNV). Recorded as a
harness limit.

The valid direct replay:

- Control (7,632/`ffa7daa5…`): `Unknown attribute kind (71)` fatal,
  `Connection interrupted`, service pid 90530.
- Candidate (7,184/`38f086cf…`): no ID71 fatal, hook installed
  (`changed=1`), real reply
  `errorMessage = "Target OS is incompatible."`, `error = 2`,
  service pid 90568.

Acceptance: a real compiler reply with a downstream error, not ID71.
Full specialization was not produced; the target-OS check is the next
concrete blocker. Disappearance of ID71 alone is not success.

## Next artifact

A real bitstream re-encoder (or supported writer) that changes the
original record's triple to the 28-byte
`air64-apple-ios19.0.0-macabi` while preserving all other records, then
the same direct A/B. A variable-length byte patch is not acceptable.

## Cleanup

Owned endpoint/client/data moved to
`/var/mobile/macws_r028_evidence_20261018/` (owned/r014, candidate
metallib, diag log, crashes); production `MacWS/r014` and `r028` paths are
absent. No owned process; production compiler count 14; native
SpringBoard 872 / tccd 883 / backboardd 905; package 0.3.4; provider and
both libmachook hashes unchanged; all six markers absent. Trustcache:
R024 reused `b3e29eef…`, `d0b584a8…`, `44767b6a…`; R028 clients
`dd8a01ce…`, `2f0d5143…`. No fresh thermal sample.

## Corrections

R026/R027 slice definitions are corrected: 6,924/`454ad619` is
payload+8 zero padding, 6,916/`ce018722` the payload; one original
record. R027 textual equality is explained by the intrinsic auto-upgrade;
it does not prove original wire preservation. Wire ID71 remains
`nocallback`.

Evidence: `docs/evidence/r028-f11h-compatibility-candidate-20261018.md`,
`coordination/artifacts/R028-f11h-compatibility-candidate-20261018.json`,
`analysis/r028-f11h/`, device
`/var/mobile/macws_r028_evidence_20261018/`.

(handover to codex now)
