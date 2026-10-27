# D112 backend result — hardened acceptance framework

Doc: `docs/evidence/devin-d112-backend-20261019.md`. Files (task-local,
`analysis/d111-trace-contract/`): `CONTRACT.md`, `d111_helper.h`,
`d111_reference.c`, `d111_bad.c` (new negative control),
`d111_contract_test.c` (rewritten), `run_contract.sh`.

## Hardening delivered

- **TOCTOU closed**: required pattern is `open(O_NOFOLLOW|O_NONBLOCK|
  O_CLOEXEC)` + post-open `fstat` as sole validation authority
  (S_ISREG/uid/0600). lstat-before-open demoted to non-authoritative.
- **O_NONBLOCK limit documented**: bounds open() only; regular-file
  writes ignore it — lock-context exclusion is the real bound.
- **Probe integrity**: harness *defines* the 19 filesystem libc symbols;
  adapters bind them at link time, so I/O is counted whether or not the
  adapter cooperates. Real calls via `dlsym(RTLD_NEXT)` incl. explicit
  `$INODE64` stat-family names. T0 asserts interpose liveness every run.
  Lock context driven by harness; adapter only queries `_active()`.
- **Deterministic races**: injectable open hook = `g_subst_path` +
  `g_swap_after_open`/`g_swap_after_lstat` (rename→.orig + symlink) +
  `g_short_write` clamp. No threads/sleeps.
- **Marker reliability**: ` <DROPPED-RECORD …>` lands on the partial's
  own line — a short-write suffix cannot parse as a complete record.
- **Deadlines**: per-case fork; inner alarm(4)+`_exit(124)`, parent
  SIGKILL(10s). Blocking adapter fails its case only.

## Verbatim runs

Reference (`./run_contract.sh d111_reference.c`): all 16 lines PASS
(T0–T10, T12–T15, T11) — incl. `rc=-2`/`rc=-3` proving opened-object
validation, and T14 record landing fd-bound in `.orig`.

Bad adapter (`./run_contract.sh d111_bad.c`): FAILs on T1, T3, T4
(`inner deadline (4s)` — FIFO block contained), T5, T6, T7, T8
(`3 I/O ops under lock` — interposed, not self-reported), T12, T13,
T14 (`victim_size=6` — per-emit reopen followed the swap). Incidental
PASSes on T0/T2/T9/T10/T15/T11 match the expectation table.

## Honesty state

- Reference column: executable PASSes (framework proven).
- Negative column: every injected sin independently detected.
- **Candidate column: PENDING on all cases** — unchanged; no fabricated
  passes. `$INODE64` resolution fix documented in evidence.

(handover to codex now)
