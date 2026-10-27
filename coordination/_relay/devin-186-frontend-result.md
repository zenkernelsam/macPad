# D186 frontend result — 2026-10-20

Final D184 snapshot re-reviewed against working tree (D185 findings
post-dated the earlier audit).
Report: `docs/evidence/devin-d186-frontend-20261020.md`.

## Verdict: ACCEPT

- **Latch landed correctly** (`d141_svctime.c:70-73`): acquire-load of
  `g_uuid_valid` inside the CAS-won window — before `hook_gen_init`
  (:75), before `memcpy(g_uuid)` (:78), before pid/gen stores. Refused
  path release-stores UNINIT (:71). The pre-gate re-set torn-read
  window from D183/D185 is closed.
- **All exit paths sound**: CAS loser exits clean (:63); latch reject
  restores UNINIT; success publishes flag-then-state in the only
  correct order (:86 before :87); no path strands INITIALIZING; hook
  unreachable by either refusal class.
- **Test distinction delivered**: U6a = sequential pre-gate repeat →
  latch -1 + uuid intact (:754-762); U6b = post-gate (:777); t21 =
  concurrent CAS loser while writer parked → -1 without publishing
  (:852-855), then post-publish pre-gate → latch -1 (:863) — CAS-loss
  vs latch-rejection exercised separately.
- **Reset quiescence explicit**: valid=0 before memset (:263); header
  (:140-146) documents reset "requires quiescent test threads, NOT a
  production reset path" — the only residual racing-read path is
  contract-excluded.
- **Header contract complete**: first-set-wins, pre- AND post-gate -1,
  hi64 provenance-only caveat (D175 wording).

Read-only; no edits/build/device/integration; no commit.

(handover to codex now)
