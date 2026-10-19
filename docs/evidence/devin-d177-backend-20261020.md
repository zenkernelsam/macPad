# D177 — span_overflow fail-closed fixture (+ exposed real defect fix)

Task: `coordination/_relay/devin-177-backend-task.md`. Add a fixture
reaching `span_overflow` (> `D169_MAX_PROBES` probe CALL spans) with
full valid UUID metadata, asserting the counter, `scoring_suppressed`,
`probes_scored==0`, and `suppressed_samples` retention. Test-only scope
unless a real defect surfaced — one did.

## Defect found by the new fixture

`MACH_CALL` handling nested ALL span bookkeeping inside
`if (np < D169_PENDING_CALLS)`. Once the pending-CALL table filled
(>64 CALLs), every subsequent probe CALL hit `pend_overflow` without
ever reaching span creation — `span_overflow` was **structurally
unreachable**. t23 failed exactly as designed on first run.

**Fix (narrow):** span bookkeeping moved out of the pend-cap guard —
serialize-check/poison/span-create/`span_overflow` now run
independently; the pending entry is pushed only under its own cap.
Both caps remain bounded and counted; RET pairing is unaffected (pend
still holds the first 64 CALLs; skipped CALLs still suppress scoring
via `pend_overflow`).

## Fixture t23

- One fully-valid probe span (CALL→HANDLER→ENQUEUE→RET, valid 128-bit
  UUIDs both streams) → exactly one would-be scored sample.
- Then 1024 further `CALL(0x74a7)` records: spans 1..1023 fill the
  table, CALL #1024 overflows.
- Asserts: `span_overflow>0`, `scoring_suppressed==1`,
  `probes_scored==0`, `suppressed_samples==1`, `mach_calls==1025`.
- `PR` fixture buffer grown 512→2048 to hold the 1026 probe records.

## Verification (exact results)

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- **ASan+UBSan**: `PASS: 94 checks, 0 failures` (23 scenarios)
- **TSan**: `PASS: 94 checks, 0 failures` — concurrency sanitizer on a
  single-threaded decoder; races/UB only, not semantics
- **arm64**: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

## Scope

Files: `analysis/d169-svcdecode-candidate/{d169_svcdecode.c,
d169_svcdecode_test.c,README.md}` + this report. No other changes;
uncommitted pending Codex review.
