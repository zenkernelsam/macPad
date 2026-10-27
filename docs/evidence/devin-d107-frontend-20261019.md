# D107 — concrete offline pacing-diagnostic candidate

Task: `coordination/_relay/devin-107-frontend-task.md`. Offline only —
no production edits, device, build, deploy, restart, or Git.

Artifacts (all under `analysis/d107-pacing-candidate/`):

| file | what |
|---|---|
| `d107-pacing-diagnostic.patch` | unified diff, `git apply --check` verified, 3 files +237/−14 |
| `pace_trace_parse.py` | trace consumer (missing/reordered/dropped first-class) |
| `test_pace_trace_parse.py` | 15 focused fixtures — **all pass** |
| `syntax_probe.c` | extracted delta, `clang -fsyntax-only -Wall -Wextra` clean |
| `README.md` | schema, lifecycle, corrected measurement model, gaps |

## 1. Frozen fingerprint

- `libmachook/mac_hooks.m` sha256
  `7321d92c530368b737e9b149a2548d524f6e67b429ee9bf10396fc37a2d9eb7c`
- `docs/runtime-switches.tsv` sha256
  `2a1976ad98ae401bdeb009dee85ffa061380ed1c9cc80c1d7cd1dcd274a7c354`
- patch applies via `git apply --check` (verified); flags script hunk is
  byte-identical to the `audit_runtime_switches.py --write-cleanup`
  generator run against the patched manifest (one sorted insert:
  `/tmp/macws_frame_pace_diagnostics` after `/tmp/macws_final_composite`).

## 2. Patch contents (per accepted D088/D090 plan + D107 records)

- Helpers inserted after `macws_keyboard_latency_trace` (patch @@ -9094):
  dedicated `write(2)` primitive (verbatim emit shape), ≤1 Hz cached arm
  check (`access(F_OK)` re-stat, atomic last_check/cached_value), 0→1
  edge `sentinel-armed` ack with `pid=`+`arm=` (D090), 8192-line/arm
  budget with one `trace-cap` marker, `_Thread_local` last-seq join key.
- Wait fn (:27489 region): `enter` (seq/pid/tid/ts_ns/req_us) once per
  call; `wake` records ≤8/wait with re-paced `target_ns`/`eff_us`;
  `exit` with ts, final target, `slept_us`, `iters`, `wakes`,
  `poll_errs`, `reason∈{deadline,clock-fail,fallback}`. Armed state
  snapshotted at entry so disarm mid-wait cannot split a pair.
- Callsite `call` lines at all three pace sites (begin :20055, SwapEnd
  :27607, ioctl-swapend :27690): `seq` joined via TLS
  (`last_seq` consumed-and-cleared; unarmed waits clear it → `seq=0`
  + `skipped=` distinguishes finish-depth bypass from stale join).
- Activity-pace mode site (:27396) converted to umbrella‖dedicated
  (D088 site-2); umbrella `fprintf` line unchanged.
- Registration: `flag /tmp/macws_frame_pace_diagnostics off` row.
- No return-value changes, no fake success, no umbrella-stdio changes
  for dedicated lines, no pacing adaptation, wake implementation
  (`macws_coexist_interaction_wake_socket`/`drain`/`wait_while_workspace_sleeping`)
  reused untouched.

## 3. Corrected measurement (fixes the D102 draft)

- D102's `t_recv`(0x185421978/0x1854219a8) → `t_dispatch`(0x185421f5c)
  is post-dequeue prelude only: a request queued during a pace interval
  reaches recv-return *after* the wait, so recv→dispatch ≈ 0 while the
  true residence was ~the pace duration. Correction implemented in
  `correlate()`: primary boundary = client `send_ts`→`reply_ts` RTT
  (same `CLOCK_MONOTONIC` domain); subtract only the intersection with
  observed `[enter_ts, exit_ts]` pace intervals → `pace_overlap_ns`;
  remainder reported as `residual_ns` = IPC + kernel delivery +
  port-set residence + wakeup/scheduling — explicitly **not** pure
  queue delay.
- `service_tid_candidate` = modal tid across waits: inference only;
  needs an external tid witness to name the services thread.
- The Quarantine review's correction stands cited in the doc: timer-pass
  callback at 0x1853e3148 `blraaz` means a display-callback edge exists
  even absent a direct branch — consistent with the candidate tracing
  the wait itself rather than inferring occupancy from static edges.

## 4. Parser verification (15 tests, all pass)

Clean window/pairing; unclosed enter; orphan exit (+merge when the late
enter arrives → `reordered`); dropped seq vs `capped-dropped` under
`trace-cap`; cross-tid interleave ≠ reorder; wake-count fidelity beyond
the 8-line emit cap (`wakes_total` authoritative); `skipped` callsite;
`orphan-call`; tid-mismatch; arm=0 window; multi-window seq continuity
(no false drops); malformed/non-pace lines; client-RTT overlap math
(overlap 500 ns, residual 600 ns on the fixture).

## 5. Documented gaps (not implemented — unsafe attachments refused)

- No WS-side recv/dispatch/flush stamps: hooking `post_port_data`'s
  `blraaz` (0x185421f5c) or the mach_msg sites inside the exact image is
  an unsafe attachment, not implemented.
- No kernel enqueue timestamps: mach_msg does not expose them; none
  invented.
- Overlap correlation cannot prove causal delay without a dispatch-side
  witness; residual stays residual.

Candidate is diagnostic-only; no build/device/runtime acceptance is
claimed or implied.
