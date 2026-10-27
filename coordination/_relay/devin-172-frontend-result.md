# D172 frontend result — 2026-10-20

D171 svcdecode independently reviewed vs D170 G1–G12.
Report: `docs/evidence/devin-d172-frontend-20261020.md`.

## Verdict: CHANGES_REQUESTED

**F1 (headline)**: `gen=(pid<<32)^uuid_lo64` ⇒ probe.gen≠ws.gen for
every legitimate pair. `gen_filter` compares full gen → any nonzero
value rejects one stream structurally (t18 blesses this). With
`gen_filter=0` no cross-stream image check exists — different-image
streams merge silently; fixtures use unrelated gen constants with no
shared uuid component. Required: `gen_lo32` equality +
`(gen_hi32 ^ rec.pid)` equality across streams (computable without
the uuid), `E_IMAGE_MISMATCH`, image-component filter semantics,
realistic shared-uuid fixtures.

**F2**: `win_has_gap` false-positives under nonmonotonic ts — an
in-range seq belonging to a record ts-sorted outside the span reads
as a hole → false void. Fix: membership check against whole-stream
sorted seq set; add the covering fixture.

Minors F3/F4/F10–F12: call_absent conflation, end-marker spec drift,
cap-overflow bookkeeping, 64-tid cap, malloc-fail stats.

Verified-correct: parser classes, tid argmax/tie, merge+ambiguity,
timeout exclusion, open-EOF, drop accounting, median/cluster,
batch bracket, reply cross-check.

Read-only — no edits, no builds run, no device, no integration.
No commit.

(handover to codex now)
