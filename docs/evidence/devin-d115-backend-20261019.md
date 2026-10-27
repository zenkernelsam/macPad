# D115 — close D114 admission/quiescence race (isolated prototype)

Task: `coordination/_relay/devin-115-backend-task.md`; review:
`coordination/reviews/D114-20261019.md`. Scope: `analysis/d113-trace-transport/`
only. No production/frontend/device/Git changes. Prototype remains an
unaccepted candidate — **PENDING**.

## The defect

D114 used two separate atomics: `shutdown` (flag) and `in_flight`
(counter). `d113_record` loaded `shutdown==0`, then incremented
`in_flight`. A producer preempted between those two ops was invisible to
`d113_quiesced()` (in_flight==0), so `d113_shutdown` + `quiesced` +
`free` could all complete; the resumed producer then CAS'd `in_flight`
on freed memory — ASan-verified heap-use-after-free in the Codex
counterexample.

## The repair

Single atomic admission word `adm`: **bit63 = `CLOSED`, low 63 bits =
admitted-operation count** (`d113_transport.c` `admit()`/`leave()`).

- Producers (`d113_record`) and drainers (`d113_drain_fd`) admit through
  one bounded CAS (≤`D113_PUB_TRIES` = 3): `CLOSED` → refuse (-1);
  retries spent → refuse (-2). Admission is an atomic read-modify-write
  on the same word quiescence reads — there is no interleaving where a
  completed admission is omitted from quiescence.
- `d113_shutdown` = `fetch_or(adm, CLOSED)` — monotonic; after CLOSED,
  every admission fails, so `quiesced = CLOSED && count==0` is **stable
  once true** (count can never grow again — this is what makes the
  check-then-free race impossible).
- `d113_drain_final` (-2) and `d113_free` (-1) keep refusing unless
  quiesced. `in_flight`/`drain_active`/`shutdown` fields removed; `adm`
  replaces all three.
- Bounded-work property preserved: admission retry is capped; a
  contended caller drops/refuses rather than spins. No waits, no
  syscalls, no heap, no errno side effects on the admit path.
- Test instrumentation: `D113_HOOK(n)` compiled in only under
  `-DD113_TEST_HOOKS` (hook pointer NULL → zero overhead in normal
  builds). Points: 1 = record pre-admit, 2 = record admitted,
  3 = drain pre-admit, 4 = drain admitted.

## Stated residual (external lifetime rule)

A call that entered a function but has **not yet attempted** `admit()`
is not observable to any in-object protocol — every check dereferences
`t`. The owner MUST stop and join (or otherwise prevent) all API
callers before freeing; no raw-pointer API can protect calls that
begin after `free`. The admission gate bounds the residual to exactly
that window: pre-admit callers resume to a refused admit and never
publish.

## Deterministic tests — `d113_lifecycle_test.c`

Gate mechanism: `d113_test_hook` armed per point; worker spins inside
the hook until released — deterministic pauses at the exact D114 race
windows. Built with `-fsanitize=address -DD113_TEST_HOOKS`.

Commands:

```text
cd analysis/d113-trace-transport
clang -Wall -fsanitize=address -DD113_TEST_HOOKS -o d113_lifecycle \
    d113_lifecycle_test.c d113_probe.c d113_transport.c -lpthread
./d113_lifecycle
```

Results — all PASS, ASan clean:

```text
L1   PASS  quiesced=1 joined=1 record_rc=-1 em=0 dr=1 io_in_lock=0
           (pre-admit pause: shutdown proceeds, call refused on resume,
            no phantom publish — residual covered by join-before-free)
L1b  PASS  free after join+shutdown rc=0
L2   PASS  q_mid=0 final=-2 free_mid=-1/-1 joined=1 q_end=1 free_end=0
           rc=0 io=0   (admitted op never omitted from quiescence)
L3a  PASS  quiesced=1 drain_rc=-1  (drainer pre-admit → refused)
L3b  PASS  q_mid=0 free_mid=-1 joined=1 q_end=1 free_end=0 io=0
           (admitted drainer blocks quiescence and free)
L4   PASS  drain_rc=-1 em 0→0 dr 0→3   (concurrent drainer refused;
           only drain_final accounted the 3 residue records)
L5   PASS  free_refusals=5/5 io_in_lock=0 total_ops=0 joined=1 free_end=0
           (repeated free refusal under lock context, zero syscalls)
L6   PASS  all lifecycle cases completed inside deadline
```

## Regressions — all preserved

```text
cd analysis/d113-trace-transport
clang -Wall -o d113_test d113_concurrent_test.c d113_probe.c d113_transport.c -lpthread
./d113_test runs-cc                                    # C0–C12 all PASS

cd ../d111-trace-contract
./run_contract.sh runs-ref   d111_reference.c          # 16/16 PASS
./run_contract.sh runs-bad   d111_bad.c                # all sin-covering FAILs
./run_contract.sh runs-proto ../d113-trace-transport/d113_adapter.c \
                             ../d113-trace-transport/d113_transport.c
                                                       # 16/16 PASS
```

D114 invariants kept: R/T framing + `n=` length check + escaping,
`mach_timebase_info` ns conversion, `h<…>` opaque-handle semantics,
errno preservation, repeated-open fd hygiene, bounded-work (not
wait-free) and single-drainer limits. Conservation still exact
(attempts == emitted + dropped) since admission refusal counts dropped.

## One test-side correction during development

L4's first assertion wrongly expected `dropped` to be unchanged across
`d113_drain_final` — final itself legitimately drains residue into
`dropped` when fd==-1. Assertion corrected to `dr == dr_before + 3`;
the drainer's refusal is proven by `rc<0` and `em` unchanged.
