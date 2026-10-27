# D111 — executable corrected-helper acceptance contract

Task-local reusable contract under `analysis/d111-trace-contract/`:
`CONTRACT.md` (acceptance criteria + adapter interface semantics),
`d111_helper.h` (fixed adapter API), `d111_reference.c` (framework
reference — proves tests are executable, NOT the candidate),
`d111_contract_test.c` (11 bounded cases), `run_contract.sh` (adapter
parameterized runner). No production markers, no `/tmp/macws_*` paths, no
frontend artifacts touched.

## Run

```text
$ ./run_contract.sh          # adapter defaults to d111_reference.c
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
exit=0
```

All executions bounded: per-case `alarm(3)`, global `alarm(30)`, `fflush`
per result (survives alarm `_exit(124)`); work dir removed after run.
Compile clean (one cosmetic unused-var warning).

## Contract coverage → frozen-patch findings

| Case | Covers | Mechanism |
|------|--------|-----------|
| T1–T3 | F1 errno | caller errno checked before/after every helper call, disarmed and both emit outcomes |
| T4 | F2/F9 | lstat-before-open rejects symlink/FIFO/dir — open() never blocks on non-regular paths (the frozen bug was open-then-validate ordering; reference validates first — verified by the no-timeout completion) |
| T5 | F9 owner | declared-owner interface `d111_trace_open_ex` → local wrong-owner rejection without root |
| T6–T7 | F3 | injected short write (`d111_debug_next_write_max`) → dropped counter + `DROPPED-RECORD` marker line in file |
| T8 | F2 lock-context | instrumented `d111_set_iocall_probe` + `d111_lock_context_enter/exit` → emit inside lock defers to bounded buffer (flushed outside lock); zero I/O ops observed under lock — **not** O_NONBLOCK-based (contract explicitly rejects O_NONBLOCK-as-bound on regular files) |
| T9 | F1/T-callers | caller `result`+errno identical around emit |
| T10 | control | 3 healthy records, counters 3/0, file content exact |
| T11 | deadline | harness bounds self-verified |

## Candidate column — PENDING (not fabricated)

`run_contract.sh <candidate_adapter.c>` runs the identical 11 cases
against any implementation of `d111_helper.h`. Until a corrected adapter
exists, every candidate result is **PENDING** per `CONTRACT.md` — the
reference PASSes validate the framework only. The no-IO-under-lock proof
accepts either the instrumented probe (`d111_set_iocall_probe` +
lock-context hooks) or a reviewed static call graph, per CONTRACT.md.

(handover to codex now)
