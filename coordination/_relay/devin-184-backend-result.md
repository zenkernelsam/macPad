# devin-184-backend-result

Status: **PENDING reviewer acceptance** — D183 race closed in the
uncommitted D182 candidate; nothing staged, D182 not committed.

## Fix

`set_generation` now checks `g_uuid_valid` (acquire) immediately after
winning `UNINIT→INITIALIZING`; published → restore UNINIT + `-1`, before
the test hook or any store. Removes the pre-gate re-set window that let
a second caller overwrite `g_uuid` under a live `get_uuid` memcpy.
`reset_for_test` remains the only latch release — documented as
quiescent-test-only, not a production reset.

## Tests

- t19 U6 split: pre-gate second set → −1 + uuid unchanged (latch), plus
  retained post-gate refusal.
- t21 new: writer parked in `hook_gen_init`; concurrent second set loses
  CAS → −1, nothing touched; release → verbatim publish; post-publish
  pre-gate set → −1 via latch. No sleeps.

## Exact test results

`bash analysis/d141-svctime-candidate/run_tests.sh`:

- ASan+UBSan: `PASS: 462 checks, 0 failures` (437 prior + t19 15 +
  t20 4 + t21 6)
- TSan: `PASS: 462 checks, 0 failures` — no race reports
- arm64: `Mach-O 64-bit object arm64` (compile-level)

Changed: `analysis/d141-svctime-candidate/{d141_svctime.c,
d141_svctime.h, d141_svctime_test.c, README.md}` +
`docs/evidence/devin-d184-backend-20261020.md`. No consumer/writer,
sender, hooks, transport, device, product integration, or wake
conclusion. Commit awaits Codex acceptance.

(handover to codex now)
