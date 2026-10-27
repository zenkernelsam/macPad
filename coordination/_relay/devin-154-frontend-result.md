# D154 frontend result — 2026-10-20

All five D153 corrections incorporated into
`coordination/_relay/devin-152-backend-task.md` in place. Report:
`docs/evidence/devin-d154-frontend-20261020.md`.

## Applied

1. D1 — `record.seq` = attempt-id vs slot-internal seq disambiguated;
   gaps reworded "never-reached-ring" (bounded-drop OR dead-mid-emit),
   never committed loss.
2. D9 — full emit-path enumeration: head/tail loads, claim loop,
   TEST_HOOKS-only call, structure-copy phrasing, `g_dropped` on the
   refused path.
3. D5 — lease permanence bound stated + no signal/async-context drain
   rule added.
4. D10 — split into in-candidate assert vs integration constraint.
5. A3 — invariant line added (only claim-owner writes the slot).
6. Bonus from D153: D7 per-leg errno checks added to the I5 test spec.

## Acceptance rows updated

R5 (+per-leg errno), R6 (7 contract lines with named qualifiers).
All other packet content verified unchanged.

No candidate/source edits, no tests, no device, no backend contact.
Read-only; no commit.

(handover to codex now)
