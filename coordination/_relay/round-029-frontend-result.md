# Frontend R029 result — saved R028 candidate audit

Status: **structurally valid but rejected as a working submission: wrong
target triple and provisional `Target OS is incompatible`.** Read-only audit
of stable files; backend R028 still Working and was not messaged/interrupted.
No shared/production/converter source, build, device or Git change.

## Exact findings

- `6916 vs 6924` is a scope difference, not a hash conflict: 6,916 B is the
  wrapper-declared payload (`ce018722…`); 6,924 B adds the 8 trailing zero
  bytes (`454ad619…`). The candidate preserves 6,916 + 8 zeros and
  `bcsize=6916`.
- Candidate module: 6,944 B, SHA `2f2de66b…`; exactly 12 differing bytes, all
  inside the 24-byte AIR triple at 6,873-6,896; prefix/suffix/trailing bytes
  identical. It contains the same 14 original PARAMATTR_GROUP records, no
  wire 71. No bitstream re-encoding, so abbreviations/group references/block
  sizes are not corrupted.
- Candidate MTLB: 1,825 records; 1,824 non-F11H modules byte-identical to
  converted; all hashes/offsets/MDSZ valid and sequential; header file/bitcode
  sizes −448, other header fields and function tag-group sizes unchanged;
  header-extension offsets shifted correctly.
- Candidate request: 7,184 B vs control 7,632 B; header differs only in six
  module/total size fields; module at offset 260 equals the candidate record
  (6,916 payload + 8 trailing zeros).
- Semantic fail: candidate target `air64-apple-ios19-macabi` (24 B) is not the
  converted/intended target `air64-apple-ios19.0.0-macabi` (28 B). The builder
  checks only same length and never compares to the converted target/profile.
  Provisional replay: `error=2`, `"Target OS is incompatible."`; control:
  `"Connection interrupted"`.
- Candidate construction replaces the whole F11H module with original + triple
  patch, not a targeted intrinsic-group restoration. For this snapshot
  orig/conv body/metadata are textually identical, so the observable
  invariants happen to hold; future non-attribute converter changes would be
  discarded.
- No reader/check patch in candidate data/builder. Replay uses the existing
  R024 diagnostic fatal-handler hook; provisional, not final acceptance.

## Accept / reject

- Accept structurally: bitstream/container invariants, original F11H attrs/
  body/metadata/calling conventions, request size-field consistency.
- Reject current candidate: target triple mismatch and target-OS error.
- Reject same-length arbitrary triple selection: the correct target is 28 B;
  an in-place 24-B patch cannot represent it.

Future gates: exact validated target/profile equality; bitstream-aware
re-encode with original intrinsic attr restoration when lengths differ;
fail closed on any non-allowlisted change; final no-hook reader acceptance and
real compiler reply; regression tests for triple length/equality and
same-length non-profile triples.

Pending backend: final candidate choice/hash, final no-diagnostic-hook replay
result/exit and cleanup. Current replay logs are provisional.

Files:
- `docs/evidence/r029-f11h-candidate-audit-20261018.md`
  (SHA-256 `635e38a27e7157fd936b14fec378ad381b835a3ceb633f3fcfac700b9defce04`)
- raw evidence under `analysis/r029-candidate-audit/`:
  `audit_candidate.py`, `audit-output.json`, `candidate-audit.json`,
  `replay-excerpts.txt`, `snapshot-hashes.txt`.

No background tasks remain running or queued.

(handover to codex now)
