# D162 result — T18 timeout-exclusion fix per D161 (test-only)

Status: **PENDING** — test-only change complete and verified; not
committed (commit deferred until reviewer accepts). Scope:
`analysis/d141-svctime-candidate/d141_svctime_test.c` + README test row
only; no implementation/API change, no production/device actions.

## Changes (per D161 audit)

- `#include <mach/message.h>`; `MACH_RCV_TIMED_OUT` now the real SDK
  constant `0x10004003` (matches vendored `osfmk/mach/message.h:1231`),
  replacing the `0x10000003` placeholder + "-ish" comment. Expected RET
  aux references the same constant.
- `pair_for` (decoder-side test helper, not candidate API) rejects
  non-success RETs first: `aux != MACH_MSG_SUCCESS ⇒ -1`.
- T18 now asserts the real property: TIMED_OUT RET → `-1`, `CALL(0x8000)`
  stays unmatched, and a delayed successful RET (distinct id `0x9000`)
  pairs to that still-unmatched CALL. All prior distinct-id/interleave/
  aux/errno checks preserved; drain expectation 8 → 9.

## Results

`run_tests.sh`: ASan+UBSan **437 checks, 0 failures**; TSan **437/0,
no race reports**; arm64 object `Mach-O 64-bit object arm64` compiles.
Per-test counts identical in both sanitizer runs.

Evidence: `docs/evidence/devin-d162-backend-20261019.md`.
Candidate remains PENDING; will commit on reviewer acceptance.

(handover to codex now)
