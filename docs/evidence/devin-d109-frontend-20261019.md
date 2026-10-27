# D109 — repair isolated candidate concurrency/identity/interval-union invariants

Task: `coordination/_relay/devin-109-frontend-task.md`. Offline only —
repairs confined to `analysis/d107-pacing-candidate/`; the production
tree is untouched (fingerprints re-verified below).

## Baseline preserved

- `libmachook/mac_hooks.m` sha256 `7321d92c530368b737e9b149a2548d524f6e67b429ee9bf10396fc37a2d9eb7c` — unchanged.
- `docs/runtime-switches.tsv` sha256 `2a1976ad98ae401bdeb009dee85ffa061380ed1c9cc80c1d7cd1dcd274a7c354` — unchanged.
- Regenerated patch re-verified: `git apply --check` clean, 3 files
  +276/−15.

## R1 — arm-check concurrency (was: load/stat/store could stat
concurrently and reorder cached state)

Replaced with a nonblocking single-owner protocol:

- `arm_check_owner_ns` (atomic owner epoch, 0 = free). A caller that
  misses the ≤1 Hz window races a CAS on the owner epoch: winner stats
  once, losers return the cached bit immediately — verified racing
  callers produce exactly one `access(2)` per window and no concurrent
  `access()` (T1/T2, inflight-guarded stub).
- **Ack-before-events**: on a 0→1 edge the owner writes the
  `sentinel-armed` line *before* the release-store of `cached_value=1`;
  readers acquire-load it, so no event line can precede the ack
  (asserted: every thread that observed armed found the ack already in
  the captured trace — T2).
- **Bounded rearm**: edges visible only at the ≤1 Hz re-check; one ack
  per 0→1 edge; per-window line budget resets on the edge (T3: disarm→
  re-arm produces exactly one fresh ack, one stat each).
- **Stale-owner reclaim**: an owner epoch >4 s old means the owner died
  mid-`access(2)`; a caller CAS-steals it. Bounded double-stat is the
  only consequence (T4: live owner → no stat; stale owner → reclaimed).

Verified by `test_arm_check_concurrent.c` — extracts the exact patched
function (`arm_check_snippet.inc`, byte-for-byte from the patch) under a
virtual clock + instrumented stat stub + captured trace. 13 assertions;
25/25 consecutive clean runs after fixing one harness artifact (a torn
`captured[]` read — mutex-serialized now).

## R2 — parser identity (was: seq-only keying; reuse across processes/
arms could misjoin)

- Wait records keyed by `(arm-window, seq)`; every record kind now emits
  `pid=` (patch updated: lock/wake/call/mode all carry pid).
- A record whose pid ≠ the window's pid is segregated into `foreign[]`,
  never joined (`test_foreign_pid_segregated`).
- Records before any arm edge go to `limbo[]` (`record-before-arm`),
  never attributed to a window.
- Strict validation: every required field must exist and parse as int —
  `malformed-field` rejection, never a crash, never silent acceptance
  (`test_missing_required_field_rejected`,
  `test_non_numeric_field_rejected_no_crash`).

## R3 — correlate interval union (was: summed overlapping waits →
double counting)

- `merged_overlap_ns()` merges intervals before clipping: overlapping
  pace intervals on different tids/arms now yield union overlap
  (fixture: [1000,2000]+[1500,2500] vs [1200,2300] → 1100, not 1600).
- Scope: `--scope tid=N|all|auto` — auto uses the modal
  `service_tid_candidate`; `linkage` reports
  `unverified-modal-tid`/`explicit-tid`/`unknown-scope-tid`, and an
  explicit tid with no waits is flagged `unknown-scope-tid`.
- Validity flags (reported, never clamped): `negative-rtt`,
  `overlap-exceeds-rtt`, `negative-residual`; residual meaning string
  states "correlation only, not proven pacing-caused latency".

## R4 — wall vs wait interval (was: enter→exit conflated contention,
trace I/O, pacing)

- New `lock` record right after `pace_lock` acquisition; `exit` gains
  `start_ns` (loop-entry clock). Parser decomposes: `contention_ns`
  (enter→lock), `setup_ns` (lock→start: wake-socket init +
  workspace-sleep wait), `wait_ns` (start→exit), `wall_ns`
  (enter→exit). `correlate()` intersects client windows with the
  **wait-proper** `[start_ns, exit_ts]` span, so pace_lock contention
  and trace emit cost can't inflate the reported pacing overlap;
  observer overhead is confined to the spans and never alters
  `target_ns` (deadline derives from `last_completion_ns + interval`
  before any emit).

## Verification re-run (all green)

| check | result |
|---|---|
| `git apply --check` on frozen tree | clean, +276/−15, 3 files |
| `clang -fsyntax-only -Wall -Wextra` on extracted delta | clean (only expected `-Wundefined-internal` for cross-refs) |
| `test_pace_trace_parse.py` | 22/22 fixtures pass |
| `test_arm_check_concurrent.c` | 13 assertions, 25/25 runs clean |
| flags-script regeneration | byte-identical to `--write-cleanup` output |
| production fingerprints | unchanged (sha256 above) |

## Still honest gaps

- `service_tid_candidate` remains an inference until an external tid
  witness joins it; `linkage` field labels it so.
- The bounded double-stat path exists only when an `access(2)` stalls
  >4 s (dead-owner reclaim); documented, unreachable in practice.
- No WS-side recv/dispatch stamps and no kernel-enqueue timestamps —
  unsafe/absent-ABI attachments, deliberately not implemented.
- Diagnostic-only candidate; no build/device/deploy/trustcache/restart
  or source/Git changes made.
