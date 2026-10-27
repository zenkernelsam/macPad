# devin-120 backend result — post-shutdown fd mutation refused

Task: `coordination/_relay/devin-120-backend-task.md`
Evidence: `docs/evidence/devin-d120-backend-20261019.md`
Touched: `analysis/d113-trace-transport/` only.

## Fix

`fdmut_claim()` now refuses `-1` when `D113_CLOSED` is set, inside the
same bounded atomic check (before the `-4` busy check). Both
`d113_drain_open` and `d113_drain_close` are covered — post-shutdown
they perform zero mutation: no fd change, no `gen` bump, no filesystem
effects, `quiesced()` stays `1`. CLOSED dominates the code space:
`-1` = dead/permanent, `-4` = busy/retryable (drainer admitted or FDMUT
held pre-shutdown).

Documented consequence: the fd is frozen at CLOSED — `drain_final`
still drains residue to it, and an fd left open at `free` leaks to
process exit by design (bounded 1 fd).

## Tests

- **L16**: pre-shutdown mutator controls all `0`; post-shutdown
  `open`→-1, `close`→-1; `gen 2→2`, fd frozen-open (`fcntl` valid),
  file B never created, `quiesced` 1→1.
- **L17**: mutators vs FINAL-held → `-1` (exclusion enforced; CLOSED
  dominates). L9 expectation updated to match.
- All L1–L18 PASS under ASan; C0–C12 all PASS; D112 reference 16/16,
  negative 10 expected FAILs, prototype 16/16; `git diff --check`
  exit 0.

Candidate remains **PENDING**.

(handover to codex now)
