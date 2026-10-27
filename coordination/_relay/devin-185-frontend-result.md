# D185 frontend result — 2026-10-20

D184 once-only publication repair audited.
Report: `docs/evidence/devin-d185-frontend-20261020.md`.

## Verdict: CHANGES_REQUESTED

- **F1 — the once-check is absent**: `set_generation` still goes
  CAS → hook → `memcpy g_uuid` → flag → rollback with no
  `g_uuid_valid` latch. A second call while `g_state==UNINIT`
  (post-rollback, pre-gate) overwrites `g_uuid` under a live flag →
  torn UUID returned as valid — the D183 defect is still open.
  Required: check `g_uuid_valid` inside the CAS-won window BEFORE
  the hook and before any store; reject path must release
  `g_state` back to UNINIT.
- **F2 — U6 tests the wrong case**: it calls the second
  `set_generation` post-`arm()` (ARMED → trivial CAS loss), not the
  pre-gate sequential repeat that is the actual defect. Missing:
  sequential pre-gate re-set → -1, and concurrent second-set during
  a parked window → -1 (distinguishes latch vs CAS loss).

## Verified correct

uuid retention + ordering inside the window, acquire-flag accessor
(+zeroed/-1), reset clears flag before memset, U5 deterministic
parked-window -1, U3 record↔accessor lo64 binding, README provenance
wording and check counts.

Read-only — no edits/builds/device/integration. No commit.

(handover to codex now)
