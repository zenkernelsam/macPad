# D169 result — svcdecode offline decoder candidate implemented

Status: **PENDING** — implemented and verified; not committed (awaits
reviewer acceptance per task contract). Scope:
`analysis/d169-svcdecode-candidate/` only. No sender/hooks/permits/
device/product actions.

## Delivered

- `d169_svcdecode.{h,c}` — canonical `d169_stream_hdr` + `d141_record[]`
  stream format, and the D164/D166 decoder: gen filter, services-tid by
  PASS_ENTER density, span FSM, nearest-unmatched same-tid CALL↔RET
  pairing, non-success RET exclusion (D162), serialize-one-probe (both
  sides poisoned), seq-gap voiding via a bounded per-span window set
  honoring D1 cross-producer non-monotonicity, reply join `probe+100`,
  per-arm median + cluster bound + honest drop/uncommitted accounting.
- `d169_svcdecode_test.c` — 11 synthetic scenarios (clean probe,
  timeout+delayed reply, in-span gap void, foreign interleave, ambiguity
  flag, gen filter/tid resolution, arm stats, file round-trip, corrupt
  stream, open span, TOO_LARGE).
- `run_tests.sh`, `README.md`.

## Results

`run_tests.sh`: ASan+UBSan **38 checks, 0 failures**; TSan **38/0**;
arm64 object `Mach-O 64-bit object arm64`.

Implementation notes in the evidence doc — notably the D1-driven
windowed seq-gap check and the pure-density tid-resolution correction.

Evidence: `docs/evidence/devin-d169-backend-20261020.md`.
Awaiting reviewer acceptance; will commit on accept.

(handover to codex now)
