# D148 frontend result — 2026-10-20

Independent verification of `coordination/reviews/D143-D145-20261020.md`
against the frozen candidate + D145 checklist. Report:
`docs/evidence/devin-d148-frontend-20261020.md`.

## Verdict

CHANGES_REQUIRED / PENDING — **replicated**. All 16 gate rows verified
row-by-row against source/test lines; every FAIL/PARTIAL stands.

## Corrections to the review

1. **T12 stale-`hook_entered` claim is wrong** — `test:278` resets both
   atomics; `reset_for_test` clears the hook pointer. The defect doesn't
   exist. (B1 still fails on the same-slot-attempt gap.)
2. **Review missed a real defect — T13 is flaky-by-design**: a
   `gate_init` that loses the CAS to a concurrent `set_generation`
   spins on a loop that never matches UNINIT and never re-CASes →
   returns -1 → `CHECK(gate_rc == 1)` fails on a coin-flip interleaving.
   Same wart means a first gate call can return -1 for observing another
   thread's setup; fix = re-CAS on UNINIT or accept {1,-1}+retry.

## Confirmed substantive gaps

B1 (no same-slot c+CAP attempt), B2 (no overlapping drain), D3 (pass
exit-aux still pre-call — README even claims the impossible field), D4
(same-id legs, no timeout exclusion), D1/C3/D8/D10 missing contract
lines, A2 bound untested, D2 pattern validation absent.

## Minimal follow-up scope

5 items listed in report §"Minimal isolated follow-up" — gate re-CAS,
same-slot-refusal assertion, real overlap churn, post-call aux, and the
mach-leg/contract rows. Not implemented — read-only.

Milestone reminder logged: commit only after the reviewer accepts a
concrete change; this task required none.

(handover to codex now)
