# devin-177-backend-result

Status: **PENDING reviewer acceptance** — span_overflow fixture added; it
exposed and narrowly fixed a real unreachable-branch defect.

## What the fixture found

The new t23 failed on first run: `span_overflow` could not fire because
all probe-span bookkeeping was nested inside `np < D169_PENDING_CALLS`
— a full pending table (cap 64) starved span creation forever, so the
1024-span cap was unreachable. Narrow fix only: span bookkeeping
(serialize check, poison, span create, `span_overflow`) now runs
independent of the pend cap; the pending entry push keeps its own cap.
RET pairing unchanged.

## Fixture

t23: one fully-valid probe span (valid 128-bit UUIDs both streams)
scores, then 1024 further `CALL(0x74a7)` fill and overflow the span
table. Asserts `span_overflow>0`, `scoring_suppressed==1`,
`probes_scored==0`, `suppressed_samples==1`, `mach_calls==1025`.
Probe fixture buffer grown to 2048 records.

## Exact test results

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- ASan+UBSan: `PASS: 94 checks, 0 failures` (23 scenarios)
- TSan: `PASS: 94 checks, 0 failures` — single-threaded decoder;
  races/UB only, not semantics
- arm64: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

Changed: `analysis/d169-svcdecode-candidate/{d169_svcdecode.c,
d169_svcdecode_test.c,README.md}` + `docs/evidence/devin-d177-backend-
20261020.md`. Nothing else; uncommitted pending Codex review.

(handover to codex now)
