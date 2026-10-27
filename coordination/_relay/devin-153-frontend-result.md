# D153 frontend result — 2026-10-20

Final source-accuracy audit of `devin-152-backend-task.md` complete.
Report: `docs/evidence/devin-d153-frontend-20261020.md`.

## Verdicts per question

1. `rec.seq` allocated before claim — literal fact (:127→push). But two
   counters share the name `seq` (record attempt-id vs slot claim-idx+1)
   — D1 needs a disambiguating clause; and "gaps = dropped attempts"
   overclaims (dead-mid-emit producer gaps without a dropped bump →
   reword "never-reached-ring").
2. D9 enumeration substantively right; elides head/tail loads,
   drop-path `g_dropped` bump, struct-copy vs memcpy phrasing, and the
   TEST_HOOKS-only call — precision edit suggested.
3. D8 umbrella-exclusion wording correctly scoped — prototype flags the
   obligation without claiming enforcement. PASS.
4. D10 nested-record assert is fully in-scope for the candidate; the
   "unreachable from instrumented boundary" clause is integration
   constraint — split the wording.
5. Tid lease sound (unique-per-process-lifetime) but permanence bound
   unstated — dead consumer freezes drains forever; and same-tid
   signal-context drain is unhandled — add contract line or counter.

## R1–R8 vs D145

All mandatory rows map except: A3 (needs one contract line), D5
same-tid reentry (uncovered), D7 per-leg errno (add checks inside I5
mach-leg tests). Everything else covered including the
destroyed-commit blind spot via per-record validation.

Five precise corrections listed for a D152-bis; packet otherwise
accurate. Read-only; no edits/tests/device/backend contact; no commit.

(handover to codex now)
