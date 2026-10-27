# D156 frontend result — 2026-10-20

D152 RET aux contract corrected per D155 (zero-API-delta route).
Report: `docs/evidence/devin-d156-frontend-20261020.md`.

## Edits to `coordination/_relay/devin-152-backend-task.md`

- I5 CALL leg: added `aux = option` verbatim clause.
- I5 RET leg: `aux = kr` verbatim; "kr + option bits" overclaim
  removed — option bits stay on the paired CALL aux, recovered via
  same-tid pairing; `d141_mach_ret(msgid, kr)` signature unchanged.
- I5 test spec: added CALL/RET aux-preservation assertion.
- R5: names corrected contract + aux-preservation requirement.
- Revision map added at packet top (D154/D156 history).
- D9 verified — no packing step needed, enumeration unchanged.

Spec-only: no source/test edits, no builds, no device, no backend
contact. No commit.

(handover to codex now)
