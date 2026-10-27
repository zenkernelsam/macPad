# devin-116 backend result — drain/fd/final/free ownership contract closed

Task: `coordination/_relay/devin-116-backend-task.md`
Evidence: `docs/evidence/devin-d116-backend-20261019.md`
Touched: `analysis/d113-trace-transport/` only.

## Design (narrow, documented)

`adm` word extended to carry the whole lifecycle:
`bit63=CLOSED | bit62=FINAL | bit61=FREEING | bits32-60=admitted
drainers | bits0-31=admitted producers`. Every lifecycle predicate and
phase claim is a bounded CAS on that one word → mutually exclusive,
stable, never waits, never does I/O (safe under the lock context).

- `d113_drain` now **admits before reading `t->fd`**.
- `d113_drain_open`/`d113_drain_close` refuse `-4` while any drainer is
  admitted or a phase bit is claimed — an in-use fd can never be closed
  or rebound mid-write (producers never touch fd).
- `d113_drain_final` CAS-claims `FINAL` (else -2); `d113_free` CAS-claims
  `FREEING` (else -1) — final-vs-final, free-vs-final, and fd-mutator-
  vs-final are single-word exclusions.
- `d113_free(d113_transport **pt)` NULLs the caller's pointer →
  sequential double-free refuses -1. Concurrent double-free is the
  stated residual (one-CAS window, not closable in-object).
- Owner-serialized teardown is the contract: gates turn misuse into
  bounded refusal; ordering is the owner's job. External rule retained:
  stop+join all callers before free.

## Deterministic tests (ASan, barrier hooks 5=mid-drain, 6=final-held)

- **L7** close mid-drain → -4, records land, close after → 0
- **L8** reopen mid-drain → -4 before `open()`, file B never created
- **L9** concurrent final → -2, free vs FINAL → -1, close vs FINAL → -4
- **L10** double free: first=0+NULL, second=-1
- **L11** disposable fd-reuse target: all records → fileA, fileD=0B
- **L1–L6** D115 cases all still PASS (pre-admit refuse, admitted-op
  quiescence blocking, 5/5 free refusals, zero I/O under lock)

## Regressions

- C0–C12: all PASS (no FAILs).
- D112: reference 16/16, bad adapter all expected FAILs, **prototype
  16/16** — framing/timebase/errno/bounded-work preserved.

Candidate remains **PENDING**. Test binaries/dirs cleaned.

(handover to codex now)
