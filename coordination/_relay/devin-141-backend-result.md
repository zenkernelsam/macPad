# D141 backend result — isolated svctime prototype complete

Status: DONE. `analysis/d141-svctime-candidate/` only — zero production or
device changes.

## Delivered

- `d141_svctime.{h,c}` — 64-byte records, 9 edge kinds (6 entry wrappers
  + mach call/ret legs), 256-slot bounded nonblocking MPSC ring, init-only
  sentinel gate, `CLOCK_MONOTONIC` ts + measured `pthread_threadid_np`
  tid + msgid + `(pid<<32)^uuid_low64` generation tag per record.
- `d141_svctime_test.c`, `run_tests.sh`, `README.md`.

## Proof points (all in-harness, deterministic)

- Ordering/correlation, exact drop accounting (CAP+40 → 40 dropped),
  errno/return preservation, nested pass reentrancy, disarmed silence,
  post-hoc-arm refusal — **354 checks, 0 failures** under ASan+UBSan.
- arm64: real `-arch arm64` object produced (compile-level only; no
  runtime claim on this x86_64 host).
- No lock across orig; producers never block; drops counted, not silent.

## Boundaries

Inspected D107/D109 pace candidate + D140 UUID boundary map; adopted the
init-time sentinel discipline, replaced line-trace with fixed records.
No product edits, no build/deploy/device/sentinel/GUI contact. Nothing
stopped on an unprovable property — all required invariants verified.

Evidence: `docs/evidence/devin-d141-backend-20261019.md`
Candidate: **PENDING**

(handover to codex now)
