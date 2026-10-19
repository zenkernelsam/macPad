# D206 — typed int(int,double) PASS wrapper (implemented)

Task: `coordination/_relay/devin-206-backend-task.md`. Offline
contract candidate under `analysis/d206-pass-wrapper-candidate/` only;
accepted candidates and production unchanged; not committed.

## Implementation

```c
int d206_wrap_pass(int (*orig)(int, double), int x, double d) {
    int s0 = errno;
    d141_emit(D141K_PASS_ENTER, 0, 0);
    errno = s0;                      /* caller's errno reaches orig    */
    int r = orig(x, d);              /* exactly once, typed            */
    int s1 = errno;
    d141_emit(D141K_PASS_EXIT, 0, 0);
    errno = s1;                      /* orig's errno reaches caller    */
    return r;
}
```

Emits use accepted `d141_emit` only; both are errno-save/restored
(emit calls `clock_gettime`/`pthread_threadid_np` — not
errno-neutral). No locks/alloc/write/block around `orig`. A
non-returning `orig` yields no `PASS_EXIT` by definition — documented
in the header, not bypassed.

## Coverage (5 scenarios, stub originals)

arg bits verbatim incl. `-0.0` (0x8000000000000000) and finite
doubles; return int and errno preserved **both** directions
(stub returns caller's errno to prove enter-side isolation); called
exactly once; ENTER/EXIT order + same measured tid + adjacent seq +
monotonic ts + gen/pid metadata; disarmed path runs orig with zero
records; two-thread run → per-tid brackets. Bounded `d141_drain`
collection after originals return.

## Exact results

`bash analysis/d206-pass-wrapper-candidate/run_tests.sh`:

- ASan+UBSan: `PASS: 33 checks, 0 failures`
- TSan: `PASS: 33 checks, 0 failures` (w4 = real two-thread run)
- arm64: `Mach-O 64-bit object arm64`

No commit; PENDING Codex review.
