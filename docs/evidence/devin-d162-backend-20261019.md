# D162 — T18 timeout-exclusion fix per D161 (test-only)

Date: 2026-10-19. Task: `coordination/_relay/devin-162-backend-task.md`.
Scope: `analysis/d141-svctime-candidate/d141_svctime_test.c` + README
test row only. No candidate implementation/API change, no production,
device, sentinel, GUI, or commit. Candidate remains **PENDING**.

## Changes

1. **Real `MACH_RCV_TIMED_OUT`** — `#include <mach/message.h>`; the
   previous placeholder `0x10000003` replaced by the SDK constant
   `0x10004003`, matching vendored
   `analysis/xnu-xnu-8792.81.2/osfmk/mach/message.h:1231`. The "-ish"
   comment removed; expected RET aux references the same constant.
2. **Decoder-side `pair_for` rejects non-success RETs first** —
   `recs[ret_idx].aux != MACH_MSG_SUCCESS ⇒ -1`, before scanning CALLs.
   This is the documented decoder-side pairing rule inside the test
   harness, not a candidate production API.
3. **T18 asserts the real exclusion property** — the `MACH_RCV_TIMED_OUT`
   RET pairs to `-1` and leaves the preceding `CALL(0x8000)` unmatched
   (`matched[2] == 0`); a delayed successful RET with a distinct id
   (`0x9000`) follows the timeout and pairs to that still-unmatched
   CALL — proving the timeout consumed nothing. Existing distinct-id
   join, async interleave, aux-verbatim, and per-leg errno checks
   preserved. Drain expectation updated 8 → 9 records.

## Results (`run_tests.sh`)

ASan+UBSan: **437 checks, 0 failures** (t18: 10 → 13 checks).
TSan: **437 checks, 0 failures, 0 race reports**.
arm64 object: `Mach-O 64-bit object arm64` — compiles clean.

Per-test counts identical across both sanitizer runs: t1 71, t2 4,
t3 4, t4 6, t5 4, t6 5, t7 4, t9 7, t10 4, t11 4, t12 3, t13 4,
t14 9, t15 6, t16 271, t17 18, t18 13.

No production, device, packaging, sentinel, or GUI actions. Commit
deferred pending reviewer acceptance.
