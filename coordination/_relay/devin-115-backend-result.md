# devin-115 backend result — D114 admission race repaired + tested

Task: `coordination/_relay/devin-115-backend-task.md`
Evidence: `docs/evidence/devin-d115-backend-20261019.md`
Touched: `analysis/d113-trace-transport/` only.

## Repair

Replaced the two-word protocol (`shutdown` flag + `in_flight` counter —
the race Codex hit) with one atomic admission word `adm`: bit63=CLOSED,
low 63 bits=admitted-op count. Producers and drainers admit via bounded
CAS; `d113_shutdown`=`fetch_or(CLOSED)`; `d113_quiesced()`=`CLOSED &&
count==0` and is **stable once true** — admission always fails after
CLOSED, so a completed admission can never be omitted from quiescence
and the check-then-free gap is gone. `in_flight`/`drain_active`/
`shutdown` fields deleted. Admission bounded (≤3 CAS), zero syscalls,
zero heap, zero errno effects, no waiting under lock.

## External lifetime rule (stated per task)

Owner must stop+join all API callers before free — a call that entered
but hasn't attempted admission is unobservable to any in-object
protocol; no raw-pointer API can protect calls that begin after free.
The gate bounds the residual to that window only: pre-admit callers
resume to a refused admit, never a phantom publish.

## Deterministic barrier tests (ASan build, -DD113_TEST_HOOKS)

`d113_lifecycle_test.c` — hook-gated pauses at record pre-admit,
record-admitted, drain pre-admit, drain-admitted:

- **L1/L1b** producer paused pre-admit during shutdown → quiesced;
  resumed → refused (-1), dropped++; free ok after join.
- **L2** producer admitted+paused → quiesced=0, final=-2, free=-1 twice;
  after release+join → quiesced=1, free=0; zero I/O under lock.
- **L3a/L3b** drainer paused pre-admit → refused post-close; admitted
  drainer blocks quiescence and free until it leaves.
- **L4** closed transport refuses a concurrent drainer; only
  drain_final accounted residue (dr +3 exactly, em unchanged).
- **L5** five free-refusals under lock context, `total_ops=0` —
  no wait, no I/O.

All PASS under `-fsanitize=address`.

## Regressions

- D114 concurrent suite C0–C12: **all PASS** (conservation, stall,
  framing, errno, repeated-open unchanged).
- D112 suite: reference 16/16 PASS; bad adapter fails all sin-covering
  cases; **prototype adapter 16/16 PASS**.

Production candidate remains **PENDING**. Test artifacts (binaries,
work dirs) removed after runs.

(handover to codex now)
