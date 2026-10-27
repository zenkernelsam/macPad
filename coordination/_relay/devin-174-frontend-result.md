# D174 frontend result — 2026-10-20

D173 image-identity + cap fail-closed audit.
Report: `docs/evidence/devin-d174-frontend-20261020.md`.

## Verdict: CHANGES_REQUESTED

- **A. uuid_lo64 is partial identity**: gen carries `uuid[0:8]` only
  (`d141_svctime.c:66-69`); record and stream-header schemas have no
  `uuid[16]` — full 128-bit image UUID is unverifiable today.
  Recovery arithmetic is correct; label required: `image_lo64`
  verified, hi64 absent by schema (not "full UUID"). Closure = add
  `uuid[16]` to the stream header at format rev.
- **B. `tids_capped` fail-open**: 65th+ tid skipped from argmax could
  be the true services tid; `TID_OK` still returns and samples score
  on possibly-wrong `svc_tid`. Needs a capped tid_state gating
  scoring.
- **C. `pend_overflow`/`span_overflow` corrupt pairing, samples
  still score**: overflowed CALL → its RET steals the previous
  unmatched CALL → closes a different span; PM CALL beyond MAX
  vanishes while its HANDLER pollutes `open_span`. Needs a
  `truncated` flag voiding/marking affected samples.

## Verified correct

F2 fix (deduped seqidx + whole-stream interval check, overflow-
guarded, stats-unknown suppresses reconcile), per-record image
uniformity, rec-pid-vs-header check, empty-stream recovery,
image_id fail-closed, reconcile direction, bounded merge.

Gate for acceptance: **no affected sample scores after any
truncation counter increments** + lo64 labeled as partial identity.

Read-only — no edits/builds/device/integration. No commit.

(handover to codex now)
