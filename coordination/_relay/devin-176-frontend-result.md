# D176 frontend result — 2026-10-20

D175 full-UUID + suppression review complete.
Report: `docs/evidence/devin-d176-frontend-20261020.md`.

## Verdict: CHANGES_REQUESTED — one required gap

`span_overflow` has no fixture (D174's explicit requirement —
>1024 PM CALLs → `span_overflow>0`, suppressed, honest
`spans_total`). Everything else passes.

## Verified

- v3 propagation: `image_uuid[16]`+`uuid_valid` in header + in-memory
  stream; loader verbatim + normalized; VERSION 2→3.
- Honest split: comment :124-127 states UUID authoritative, lo64 is
  consistency-only; `recovered lo64 == uuid_lo64(uuid)` enforced
  (:160); upper 64 bits producer-declared only.
- Filter: both streams must carry UUID AND equal filter; empty
  stream `uuid_valid=0` → unverified → suppressed.
- Suppression order: counters accumulate, then `probes_scored →
  suppressed_samples`, stats see zero — honest withheld accounting.
- Alloc paths: E_ALLOC on mer/spans; seqidx fail → stats_unknown →
  suppressed.
- t20/t21/t22 cover lo64/hi64 cases, filter accept/reject, empty-
  stream suppression, pend+tid caps, call_absent split; t19 covers
  the D172 F2 construction.

Minors: stats-malloc leaves median=0 with scored>0; `reserved`
unchecked.

Read-only — no edits/builds/device/integration. No commit.

(handover to codex now)
