# D116 — isolated drain/fd/final/free teardown-ownership contract

Task: `coordination/_relay/devin-116-backend-task.md`; inputs:
`coordination/reviews/D115-20261019.md`,
`docs/evidence/devin-d115-frontend-20261019.md`. Scope:
`analysis/d113-trace-transport/` only. Prototype remains **PENDING** —
no production integration.

## Gaps closed (review → design → test)

Packed `adm` word extended: bit63=`CLOSED`, bit62=`FINAL`,
bit61=`FREEING`, bits32-60=admitted **drainer** count, bits0-31=admitted
**producer** count. All lifecycle predicates and phase claims are CAS
operations on this one word, so any two decisions are atomic against
each other and bounded (≤`D113_PUB_TRIES`).

| Review finding | Fix | Test |
|---|---|---|
| `d113_drain()` read `t->fd` before admitting (fd-alias window) | `d113_drain` now admits (drainer) BEFORE reading `t->fd`; fd mutators refuse `-4` while `DR_MSK` or `PHASE` bits set | L7, L8, L11 |
| `drain_close`/`drain_open` mutate fd with no coordination | both refuse `-4` when any drainer admitted or `FINAL`/`FREEING` set; producers never touch fd and don't block mutation | L7, L8, L11 |
| `drain_final` unregistered — concurrent finals, free-vs-final UAF | `drain_final` CAS-claims `FINAL` (else `-2`); `free` requires no `FINAL`/`FREEING` — same-word mutual exclusion, no wait | L9 |
| `free` check-then-free — double-free | `d113_free(d113_transport **pt)` CAS-claims `FREEING`, frees, NULLs `*pt`; sequential second call refuses `-1`. Concurrent double-free stays an external-rule residual (one-CAS window — loser's CAS can run an instruction after winner's `free()`; not closable in-object, stated) | L10 |
| Post-shutdown drain admit | already gated by `admit` CLOSED check (D115) — retested | L4 |
| Pre-first-touch callers | external rule unchanged and restated; pre-admit callers resume to refused admit | L1/L3a |

Contract shape chosen: **in-object bounded gates + owner-serialized
teardown** — `drain_open`/`drain_close`/`drain_final`/`free` are
single-owner operations whose misuse degrades to a `-4`/`-2`/`-1`
refusal rather than corruption; ordering remains the owner's job. No
gate waits or does I/O — safe under the lock context (verified
`io_in_lock=0` throughout L-cases).

API changes: `d113_drain_close` → `int` (0 ok, -4 busy);
`d113_free` → `int d113_free(d113_transport **pt)` (NULLs caller).
Adapter + both test files updated.

New hooks under `-DD113_TEST_HOOKS`: `5` = inside `drain_loop` before a
record write (mid-drain pause), `6` = inside `d113_drain_final` after
`FINAL` claimed (final-in-progress pause).

## Exact commands

```text
cd analysis/d113-trace-transport
clang -Wall -fsanitize=address -DD113_TEST_HOOKS -o d113_lifecycle \
    d113_lifecycle_test.c d113_probe.c d113_transport.c -lpthread
./d113_lifecycle
clang -Wall -o d113_test d113_concurrent_test.c d113_probe.c d113_transport.c -lpthread
./d113_test runs-cc
cd ../d111-trace-contract
./run_contract.sh runs-ref   d111_reference.c
./run_contract.sh runs-bad   d111_bad.c
./run_contract.sh runs-proto ../d113-trace-transport/d113_adapter.c \
                             ../d113-trace-transport/d113_transport.c
```

## Results

Lifecycle suite (ASan, deterministic barrier pauses) — all PASS:

```text
L1   PASS  pre-admit producer pause → quiesced; resume → refused (-1)
L1b  PASS  free after join+shutdown rc=0
L2   PASS  admitted producer → quiesced=0, final=-2, free=-1/-1; join→free=0; io=0
L3a  PASS  drainer pre-admit → refused post-close
L3b  PASS  admitted drainer → quiesced=0, free=-1; leave→free=0; io=0
L4   PASS  post-close drainer refused; only drain_final accounted residue (+3)
L5   PASS  5/5 free refusals under lock, total_ops=0
L7   PASS  drain_close mid-drain → -4; after drain leaves → 0; fileA=177B
L8   PASS  drain_open mid-drain → -4 before open(); fileB never created
L9   PASS  2nd final → -2, free vs FINAL → -1, close vs FINAL → -4; free after → 0
L10  PASS  free1=0 (T NULL'd), free2=-1
L11  PASS  close refused mid-drain; all 4 records → fileA (240B), fileD=0B
L12  PASS  all cases inside deadline
```

Regressions — all preserved:

- D114 concurrent suite `C0`–`C12`: all PASS (14 PASS lines, no FAILs)
  — conservation, stall, framing, errno, repeated-open unchanged.
- D112 suite: reference **16/16 PASS**; bad adapter fails all
  sin-covering cases (10 FAIL lines as designed); **prototype adapter
  16/16 PASS**.

## Limits kept explicit

Bounded CAS = bounded work, not wait-free; single drainer; `n=` is a
length check not a CRC; cooperative shutdown; concurrent double-free is
the stated external-rule residual (in-object gate shrinks it to one CAS
instruction, cannot eliminate it); pre-first-touch callers need the
owner's stop+join. Production candidate remains **PENDING**.
