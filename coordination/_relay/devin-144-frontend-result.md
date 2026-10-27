# D144 frontend result — 2026-10-19

Frozen-input concurrency audit of `analysis/d141-svctime-candidate/`
complete. D143 files not read. Report:
`docs/evidence/devin-d144-frontend-20261019.md`.

## Verdict: PENDING — one P0, one P1

**P0 — slot-reuse race**: drain skips uncommitted slots AND advances
`g_tail` past them → a preempted producer's late `s->rec` write +
`seq=c+1` commit can clobber a re-claimed wrapped slot (`c+CAP`),
silently destroying a committed record or tearing a delivered copy.
`emitted+dropped==N` conservation still passes — the corruption is
invisible to the accounting. Minimal fix proven in the report: drain
stops at the first uncommitted slot (bounded prefix drain); `tail > c`
then implies commit `c+1` was observed, so `c+CAP` cannot be claimed
while `c` is outstanding. Wedged producer → CAP-bounded stall + counted
drops; honest degradation.

**P1 — unfillable field**: `d141_wrap_pass(exit_aux,…)` fixes aux before
`fn` runs — the required per-pass handled-count cannot be carried.
Post-call aux path needed.

## P2s

seq-alloc-before-claim → drained order ≠ seq order (contract must say
claim-order stream, seq is correlation id); seqs count attempts not
commits; `g_pid`/`g_gen` two-word tear under contract-violating
concurrency (fold: derive pid from gen); drain multi-consumer unguarded;
T8 models same-msgid on both mach legs (combined traps differ); nested
mach pairs need stack/tolerance rule.

## Test gaps

T7 joins workers before draining — zero produce/drain overlap, so the
P0 window is unreachable; `seen[]` loop is a vacuous self-assert.
14-row deterministic matrix supplied (R1–R14) incl. parked-producer
wrap, concurrent-drain churn, seq-gap/reorder, gen race, multi-drain,
combined-trap leg join, timeout tagging, nested legs, per-pass count,
wedge-stall bound.

P0 is a ring-policy defect in the prototype only — does not falsify the
D140 design.

(handover to codex now)
