# D207 — D206 concurrency fixture repair

Task: `coordination/_relay/devin-207-backend-task.md`. Test-only
changes in `analysis/d206-pass-wrapper-candidate/`; wrapper source
unchanged; not committed.

## Repairs

1. **Shared-global race removed** — w4's concurrent stub previously
   wrote plain globals (`s_arg_dbl`/`s_arg_int`/`s_errno_seen`/
   `s_calls`) from both threads; a quiet TSan schedule proved nothing.
   The dedicated `w4_stub` now observes/writes only `_Thread_local`
   state; the sole shared state is the atomic `w4_arrived` counter,
   and results are copied out to per-index `W4RES[i]` slots
   (disjoint writes, no overlap).
2. **Deterministic rendezvous** — inside the stub, an atomic
   arrive-barrier guarantees both threads are inside their orig
   before either returns (bounded spin — fails at 2×10⁸ rather than
   hanging), so interleaved brackets are proven, not scheduled around.
3. **Strict per-tid assertions** — for each observed tid: exactly one
   ENTER AND exactly one EXIT, ENTER before EXIT (the old total-count
   check could pass a tid with two ENTERs and another with two EXITs);
   per-thread each orig called exactly once with its own int/double
   bits, own return value, and errno preserved in both directions
   (per-thread caller errno → orig → back to caller).

Single-thread stub tests (w1–w3, w5) and `d206_pass_wrapper.c` are
byte-identical.

## Exact results

`bash run_tests.sh`: ASan+UBSan **45 checks, 0 failures** (5
scenarios; w4 6→18); TSan **45/0** — real two-thread run, all shared
state atomic; arm64 `Mach-O 64-bit object arm64`.

No commit; PENDING Codex review.
