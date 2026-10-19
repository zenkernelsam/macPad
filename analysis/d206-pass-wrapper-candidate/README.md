# D206 — analysis-only typed PASS wrapper

**Status: PENDING — offline contract harness. No MSHookFunction,
permit, binary offset, runtime symbol, WS/SkyLight/sender invocation,
or product integration exists or is permitted.**

`d141_wrap_pass`'s `uintptr_t(void*)` callback cannot carry the
services-pass ABI `int f(int, double)` identified in D205 (args in
`x0` AND `d0`). This is the typed contract:

```c
int d206_wrap_pass(int (*orig)(int, double), int x, double d);
```

- emits `PASS_ENTER` (`msgid=0, aux=0`) via accepted `d141_emit`,
  calls `orig` **exactly once** on its typed signature (no cast
  through a wrong function type), emits `PASS_EXIT`, returns `int`
  unchanged.
- errno discipline: caller's errno reaches `orig`; `orig`'s errno
  reaches the caller — both emits are bracketed save/restore (emit
  calls `clock_gettime`/`pthread_threadid_np`, not errno-neutral).
- no locks, allocation, write, or blocking call around `orig` (D9/D10).
- a **non-returning** `orig` (fatal path) produces no `PASS_EXIT` —
  by definition; documented, not bypassed.

## Tests (`run_tests.sh`, w1–w5)

| w | proves |
|---|---|
| w1 | full bracket: `-0.0` double bits + int arg verbatim, return/errno preserved both directions, called once, same measured tid, adjacent claim-order legs, monotonic ts, gen/pid metadata |
| w2 | finite double bits verbatim, zero return |
| w3 | disarmed (no sentinel): orig runs, **zero** records emitted |
| w4 | two threads, **TLS** stub + atomic arrive-barrier (both origs provably entered before either returns): per-thread own args/return/errno (both directions), called-once; strict per-tid shape — each tid exactly one ENTER then one EXIT (a tid with two ENTERs or two EXITs rejected) |
| w5 | stub returning caller's errno proves emit isolation both ways |

## Results

ASan+UBSan **45 checks, 0 failures**; TSan **45/0** (w4's only shared
state is the atomic rendezvous; per-thread results are TLS →
joined before reset); arm64 `Mach-O 64-bit object arm64`. Links `d141_svctime.c` read-only; accepted candidates
unchanged. Sentinel here is the test's own `/tmp` file — no product
sentinel choice is made.
