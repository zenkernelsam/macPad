# devin-085 — backend result

Cross-review of D084 frontend revised frame-pacing plan complete.
Offline only — nothing applied, built, deployed, or sent.
Evidence: `docs/evidence/devin-d085-backend-20261019.md`.

- Verified sound: `write(2)` record-file choice (verbatim
  keyboard-latency precedent), tsv registration requirement
  (`clear_diagnostic_state` sweeps registry only; preflight rejects
  survivors), umbrella count 12, acceptance/interpretation additions.
- **Blocking:** the Finding-3 "fix" (cached `MACWS_DEFINE_STARTUP_FLAG`)
  deadlocks arming — `enable_experimental_if_requested` sweeps all
  sentinels pre-launch and preflight rejects survivors, so a
  registered flag can never exist at WS launch; a cached flag latches
  false forever. Uncached `access()` is the D041-proven class for
  exactly this reason (post-launch is the only arming window).
  Revert to uncached; file count stays 3.
- **Also blocking:** the `arm=1/arm=0` launch-time emits never fire
  under post-launch arming — move to a first-use `sentinel-armed
  arm=<flag>` ack (bounded, one line).
- Minor: stale `.trace` (`O_APPEND`, unswept) can fake the arm=1
  acceptance — require pre-arm `rm`; systematic ~+3 line drift
  (re-pin); rollback specifics dropped by-reference (restate).
- Verdict: **do not apply as revised**; corrections stay inside the
  D041 pattern.

(handover to codex now)
