# D107 frontend result — isolated pacing-diagnostic candidate

Evidence: `docs/evidence/devin-d107-frontend-20261019.md`.
Artifacts: `analysis/d107-pacing-candidate/` — `d107-pacing-diagnostic.patch`,
`pace_trace_parse.py`, `test_pace_trace_parse.py` (15/15 pass),
`syntax_probe.c` (clang -fsyntax-only clean), `README.md`.

## Delivered

- **Unified patch** applying cleanly (`git apply --check` ✓) to the frozen
  fingerprint (`mac_hooks.m` sha256 `7321d92c…`, tsv `2a1976ad…`), 3 files
  +237/−14: helpers block (~:9098, verbatim `write(2)` emit shape, ≤1 Hz
  cached arm check, 0→1 edge `sentinel-armed` ack with `pid=`+`arm=`,
  8192-line/arm budget + `trace-cap` marker, TLS seq join key);
  instrumented wait fn (enter/wake≤8/exit records: seq, pid, tid,
  `ts_ns`, `target_ns`, `slept_us`, `iters`, `wakes`, `poll_errs`,
  `reason`); dedicated `call` lines at all 3 pace sites with
  consume-and-clear seq join (`skipped=1` for finish-depth bypass);
  activity-pace mode site converted to umbrella‖dedicated (D088 site-2);
  tsv `flag/off` row + regenerated `macws_diagnostic_flags.sh` (verified
  byte-identical to `--write-cleanup` output).
- **Parser + verification**: missing (unclosed enter), reordered
  (late-enter merge → `reordered`), dropped (`dropped-seq` vs
  `capped-dropped` under `trace-cap`), orphan call/exit/wake,
  tid-mismatch, arm=0, multi-window, malformed, wake-cap fidelity, and
  client-RTT overlap math — 15/15 fixtures pass.

## Measurement correction

D102's recv-return→dispatch is post-dequeue only — a request queued
during pacing reaches recv-return after the wait, so that interval shows
~0 while true residence ≈ pace duration. Corrected: client `send_ts`/
`reply_ts` (same `CLOCK_MONOTONIC`) intersected with observed
`[enter,exit]` pace intervals → `pace_overlap_ns`; remainder reported as
`residual_ns` (IPC + kernel residence + wakeup + scheduling — never pure
queue delay). `service_tid_candidate` labeled inference pending external
tid join.

## Boundaries kept

Diagnostic-only; default-off registered sentinel; post-launch arm ≤1 s;
no return/fake-success/umbrella changes; no pacing adaptation; wake
implementation reused; no kernel-enqueue fabrication; no SkyLight recv/
dispatch hooks (unsafe attachment — documented gap, not implemented).
No production edits, device, build, deploy, restart, or Git.

(handover to codex now)
