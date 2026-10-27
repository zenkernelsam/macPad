# D118 — atomic fd-mutation phase + honest quiesced predicate

Task: `coordination/_relay/devin-118-backend-task.md`; input review:
`coordination/reviews/D116-20261019.md`. Scope:
`analysis/d113-trace-transport/` only. Prototype remains **PENDING**,
offline-only.

## The reproduced defects

1. **fd check-then-mutate race** (Codex verbatim repro on a temporary
   instrumented copy): `d113_drain_close` loaded `adm`, checked
   `DR_MSK|PHASE` clear, then `close(t->fd)` — a drainer admitted
   between the check and the close captured the old fd; close ran, the
   fd number was reused by a replacement file, and the resumed write
   landed there: `close_rc=0 drain_rc=1 fileA=0B fileD=71B`. Same gap in
   `d113_drain_open`.
2. **`d113_quiesced()==1` while `FINAL` held** — the public predicate
   ignored phase bits, contradicting its documented contract.

## The fix

New `D113_FDMUT` bit60 in `adm`, folded into `D113_PHASE`
(drain-admit count shrank to bits32-59):

- `fdmut_claim()`: bounded CAS — refuses `-4` if any drainer is admitted
  or any phase is set, otherwise sets `FDMUT` in the same atomic step.
  There is no check-then-claim window: the count check and the claim
  are one CAS.
- `d113_drain_open`/`d113_drain_close` claim `FDMUT` first, hold it
  through close/open/fd-publish, release on every error and success
  path. While `FDMUT` is held every `admit()` refuses (PHASE) — a
  drainer can never capture an fd that a mutation is about to close.
- `d113_quiesced()` = `CLOSED && !(CNT | PHASE)` — false while FINAL,
  FREEING, or FDMUT is active.
- New hooks (test builds only): `7` = inside `drain_close` with FDMUT
  held pre-close; `8` = inside `drain_open` with FDMUT held pre-mutate.

Ordering argument: a drainer either admits before the claim (claim
sees `DR_MSK` → `-4`) or after (admit sees `FDMUT` → `-1`) — mutually
exclusive on one word, no third interleaving.

## Exact commands + exits

```text
cd analysis/d113-trace-transport
clang -Wall -fsanitize=address -DD113_TEST_HOOKS -o d113_lifecycle \
    d113_lifecycle_test.c d113_probe.c d113_transport.c -lpthread   # exit 0
./d113_lifecycle                                                  # exit 0
clang -Wall -o d113_test d113_concurrent_test.c d113_probe.c d113_transport.c -lpthread
./d113_test runs-cc                                               # exit 0
cd ../d111-trace-contract
./run_contract.sh runs-ref   d111_reference.c                     # 16 PASS
./run_contract.sh runs-bad   d111_bad.c                           # 10 FAIL (expected)
./run_contract.sh runs-proto ../d113-trace-transport/d113_adapter.c \
                             ../d113-trace-transport/d113_transport.c  # 0 FAIL
cd ../.. && git diff --check -- analysis/d113-trace-transport/      # exit 0
```

## Results

Lifecycle suite (ASan, deterministic barrier pauses):

```text
L1–L12   all PASS (D115+D116 preserved; L9 now also asserts
         quiesced==0 while FINAL held — the D118 predicate fix)
L13  PASS  exact Codex interleaving: close paused with FDMUT held →
           drainer refused (-1), close completes (0), fileA=0B fileD=0B
           em=0 — nothing written to either file, replacement target
           stays empty
L14  PASS  open/rebind paused with FDMUT held → drainer refused (-1);
           reopen completes → records land in fileB (120B), fileA=0B
L15  PASS  all cases inside deadline
```

Regressions: C0–C12 all PASS; D112 reference 16/16, negative adapter
10 expected FAILs, prototype 16/16. `git diff --check` exit 0.

## Limits

Producers now also refuse while `FDMUT` is held (PHASE membership) —
records drop during the bounded mutation window; counted, correct by
the drop contract, and bounded by the mutation's I/O duration (mutation
runs outside producer lock context only). Concurrent double-free stays
the documented one-CAS external-rule residual; pre-first-touch callers
remain owner-join territory. Production candidate **PENDING**.
