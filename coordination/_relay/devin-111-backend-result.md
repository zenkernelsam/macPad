# D111 backend result — executable corrected-helper acceptance contract

Doc: `docs/evidence/devin-d111-backend-20261019.md`. Files (all task-local
under `analysis/d111-trace-contract/`): `CONTRACT.md`, `d111_helper.h`,
`d111_reference.c`, `d111_contract_test.c`, `run_contract.sh`. No frontend
artifacts touched; no production marker paths.

## Delivered

- **Adapter interface** (`d111_helper.h`): sentinel arm check (errno-safe,
  ≤1 probe/s), one-time validated open (regular file + owner + 0600,
  symlink/FIFO/dir rejected at lstat-before-open), held-fd emit (no
  per-record open, partial→dropped+marker), close, observability counters,
  `d111_trace_open_ex` declared-owner variant, iocall/lock-context probes,
  short-write injector.
- **Reference adapter**: satisfies every executable case — exists to prove
  the tests run; explicitly NOT the candidate.
- **11-case harness**: all bounded (`alarm(3)` per case, `alarm(30)`
  global, `_exit(124)` on timeout, `fflush` per line).

## Reference-adapter run — verbatim

```text
T1   PASS    errno after absent-sentinel check
T2   PASS    errno after successful emit
T3   PASS    directory rejected at open
T4   PASS    symlink+FIFO rejected, no block
T5   PASS    wrong-owner rejected
T6   PASS    partial write counted dropped
T7   PASS    drop observable via marker line
T8   PASS    zero I/O under lock
T9   PASS    caller result+errno unchanged
T10  PASS    3 records written, counters 3/0
T11  PASS    all cases completed inside deadline
```

## Honesty state

- Reference column: executable, all PASS (validates the contract is
  achievable + the tests work).
- **Candidate column: every case PENDING** — `./run_contract.sh
  <candidate_adapter.c>` runs the identical cases when a corrected
  adapter exists. No fabricated PASS.
- No-IO-under-lock proof offered two ways: instrumented iocall probe +
  lock-context hooks (demonstrated by the reference's deferred buffer),
  or a reviewed static call graph. `O_NONBLOCK` on regular files
  explicitly rejected as a wall-time bound.

(handover to codex now)
