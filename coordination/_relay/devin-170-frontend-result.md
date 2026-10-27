# D170 frontend result — 2026-10-20

svcdecode offline acceptance gates frozen.
Report: `docs/evidence/devin-d170-frontend-20261020.md`.

## Corrections pinned before implementation

- C1: span tid wording — CALL legs live on the probe tid in a
  *different stream*; "same tid" applies only inside the WS edge
  chain. Two-stream merge fixture mandatory (absent from D169's list).
- C2: CALL-absent window behavior undecided in D168/D169 — gate
  requires explicit fail-vs-degraded rule + fixture.
- C3: `record.seq` is per-ring — contiguity checked per source stream
  before ts-merge, never on merged order.
- C4–C7: non-monotonic ts legal (claim≠ts order); open-at-EOF span
  reported not auto-closed; multi-HANDLER batch brackets legal;
  `0x750b` reply-id cross-check flags mismatches.

## Frozen checklist

G1–G12 covering stream format, gen isolation, tid argmax+density,
cross-stream spans, second-CALL ambiguity, per-stream seq-gap
voiding, non-success RET, malformed bounds, verbatim drop counters,
statistics (median + cluster + honest sample accounting),
batch/bracket semantics, and the required fixture set.

No code/tests/builds/device/integration. No commit.

(handover to codex now)
