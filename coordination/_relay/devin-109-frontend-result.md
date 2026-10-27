# D109 frontend result — candidate invariants repaired in place

Evidence: `docs/evidence/devin-d109-frontend-20261019.md`.
Repaired artifacts: `analysis/d107-pacing-candidate/` (patch
regenerated; `git apply --check` clean, +276/−15 on frozen
`mac_hooks.m` 7321d92c… + tsv 2a1976ad… + regenerated flags script).

## Fixes

- **R1 arm-check race** → nonblocking CAS owner (`arm_check_owner_ns`
  epoch; losers return cached).  Ack-before-events: `cached_value=1`
  publishes release-ordered *after* the ack `write(2)` returns.
  Bounded rearm: edges at ≤1 Hz only, one ack per 0→1, budget reset per
  window.  Stale owner (>4 s) reclaimed — bounded double-stat only on
  that dead-owner path.  Verified by
  `test_arm_check_concurrent.c` (extracts the exact patched fn): 13
  assertions, 25/25 clean runs (one earlier flap was a harness
  torn-read on the capture buffer, fixed by mutex).
- **R2 identity** → records keyed `(window, seq)`; `pid=` added to every
  record kind; pid-mismatched records segregated as `foreign`, never
  joined; records before arm → `limbo`; missing/non-int required fields
  → `malformed-field` reject, no crash, no silent accept.
- **R3 union** → `correlate()` merges intervals before clipping (union,
  no double count); `--scope tid=N|all|auto`; `linkage` =
  `unverified-modal-tid`/`explicit-tid`/`unknown-scope-tid`; violations
  `negative-rtt`/`overlap-exceeds-rtt`/`negative-residual` flagged, not
  clamped; residual labeled IPC+queue+scheduling, correlation-only.
- **R4 wall vs wait** → new `lock` record + `start_ns` on `exit`:
  contention (enter→lock), setup (lock→start), wait-proper
  (start→exit); overlap math uses wait-proper so pace_lock contention
  and trace I/O can't inflate it; emit cost never alters `target_ns`.

## Verification

`git apply --check` clean; syntax probe clean; parser 22/22 fixtures;
concurrent arm test 13/13 assertions ×25 runs; flags script regenerated
byte-identical; production fingerprints unchanged. Diagnostic-only — no
production edits, build, device, deploy, trustcache, restart, Git.

(handover to codex now)
