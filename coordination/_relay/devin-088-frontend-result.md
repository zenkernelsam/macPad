# devin-088 — frontend result

D086 plan revised per D087 findings. Offline only — nothing applied.

- `docs/evidence/devin-d088-frontend-20261019.md`: consolidated spec.

## What changed

- **R1 fix**: sentinel check is now rate-limited — `cached_value` +
  `last_check_ns` atomics, `CLOCK_MONOTONIC` (the file's existing
  convention, :10441/:10496), re-stat at ≤1 Hz. Per-frame gate cost =
  one atomic load; the documented hot-path `access(2)` regression
  class is closed. Post-launch arming and live disarm both preserved
  at ≤1 s latency — keeping the only real arming window (launcher
  sweeps registered sentinels at launch; preflight rejects survivors).
- **R3+R4 fix**: the `sentinel-armed` ack is emitted on the
  rate-limited check's **0→1 armed edge** (`atomic_exchange`
  serializes → exactly one line per arm window, re-arms included)
  instead of inside the trace emit — "armed, bounds never hit" now
  yields the ack line; the four-state disambiguation table is
  complete.
- **R2 fix**: ordinary cleanup spelled — retrieve trace → `rm` trace
  + `rm` sentinel, both namespace spellings; registered-sweep is
  backstop only. Pre-arm order: `rm` stale `.trace` first, then
  `touch` sentinel; freshness = mtime ≥ generation lstart + first
  line `sentinel-armed`.
- Structure: `macws_frame_pace_write` (open/write/close primitive,
  keyboard-latency shape) + `macws_frame_pace_trace` (gated wrapper);
  umbrella `fprintf` untouched; `%p` dropped; two sites unchanged in
  contract.

## Unchanged

Registration via `runtime-switches.tsv` `flag`/`off` row +
regenerated flags file + `audit_runtime_switches.py` check;
rollback = pinned-dylib revert + WS restart + dual-namespace rm;
activation still bound to rebuild/redeploy/restart under a separate
named grant; current generation unmeasurable. Honest size ≈45 lines /
3 `mac_hooks.m` regions + tsv + regenerated file.

(handover to codex now)
