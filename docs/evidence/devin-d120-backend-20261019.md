# D120 — fd mutators refuse after CLOSED

Task: `coordination/_relay/devin-120-backend-task.md`. Scope:
`analysis/d113-trace-transport/` only, offline prototype. Candidate
**PENDING**.

## The defect

`fdmut_claim()` rejected `D113_DR_MSK | D113_PHASE` but not
`D113_CLOSED`. Post-shutdown `d113_drain_open`/`d113_drain_close`
succeeded and mutated the fd while `d113_quiesced()` continued to
report stable quiescence — Codex's standalone repro showed
`postshutdown open=0 quiesced=1 fd=3`.

## The fix

One-line gate change inside the same bounded atomic admission check:

```c
if (a & D113_CLOSED) return -1;               // dead: fd is frozen
if (a & (D113_DR_MSK | D113_PHASE)) return -4;  // drainer/phase live
```

CLOSED dominates the refusal codes: `-1` = permanently dead, `-4` =
busy/retryable. Since `FINAL`/`FREEING` can only exist post-CLOSED,
mutator-vs-teardown-phase now also reports `-1`. FDMUT itself can never
be claimed post-CLOSED, so a post-shutdown mutator performs zero
mutation: no fd change, no `gen` bump, no filesystem effects.

Lifecycle consequence (documented): after `CLOSED` the fd is frozen —
`drain_final` may still drain residue to a still-open fd, and any fd
left open at `d113_free` leaks to process exit by design (bounded 1
fd). `d113_free`/`d113_drain_final` predicates unchanged.

## Tests added

- **L16** — pre-shutdown mutator success controls (open→close→reopen
  all `0`); post-shutdown `drain_open` → `-1` and `drain_close` → `-1`;
  `gen` unchanged (2→2), `T->fd` frozen open (`fcntl` still valid),
  target file B never created; `d113_quiesced()` stays `1` throughout.
- **L17** — FINAL-exclusion controls: `drain_open`/`drain_close` while
  `FINAL` held → `-1` (CLOSED dominates; exclusion enforced).
- **L9** updated to the same `-1` expectation for close-vs-FINAL.

## Exact commands + exits

```text
cd analysis/d113-trace-transport
clang -Wall -fsanitize=address -DD113_TEST_HOOKS -o d113_lifecycle \
    d113_lifecycle_test.c d113_probe.c d113_transport.c -lpthread   # exit 0
./d113_lifecycle                                                  # exit 0
clang -Wall -o d113_test d113_concurrent_test.c d113_probe.c d113_transport.c -lpthread
./d113_test runs-cc                                               # exit 0, 14 PASS lines
cd ../d111-trace-contract
./run_contract.sh runs-ref   d111_reference.c                     # 16 PASS
./run_contract.sh runs-bad   d111_bad.c                           # 10 expected FAILs
./run_contract.sh runs-proto ../d113-trace-transport/d113_adapter.c \
                             ../d113-trace-transport/d113_transport.c  # 0 FAIL
cd ../.. && git diff --check -- analysis/d113-trace-transport/      # exit 0
```

All L1–L18 PASS under ASan; C0–C12 all PASS; controls preserved.
