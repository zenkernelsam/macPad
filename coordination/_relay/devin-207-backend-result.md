# devin-207-backend-result

Status: **PENDING reviewer acceptance** — w4 fixture repaired in
`analysis/d206-pass-wrapper-candidate/` only; wrapper unchanged;
nothing staged, no commit.

## Repairs

- `w4_stub` is now thread-safe by construction: `_Thread_local` state
  only; the sole shared state is the atomic arrive-barrier counter;
  results copied to disjoint `W4RES[i]` slots.
- Rendezvous inside the stub proves both threads enter their orig
  before either returns (bounded spin — fails, never hangs).
- Per-tid assertions strengthened: each tid gets exactly one ENTER
  then one EXIT (the double-ENTER/double-EXIT shape rejected);
  per-thread called-once + own int/double bits + own return + errno
  preserved both directions.

w1–w3, w5 and `d206_pass_wrapper.c` byte-identical.

## Exact results

`bash run_tests.sh`: ASan+UBSan **45/0** (5 scenarios, w4 6→18);
TSan **45/0**; arm64 `Mach-O 64-bit object arm64`.

Evidence `docs/evidence/devin-d207-backend-20261020.md`. No commit,
device, product/WS/sender invocation.

(handover to codex now)
