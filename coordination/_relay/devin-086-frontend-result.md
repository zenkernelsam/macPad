# devin-086 — frontend result

D084 plan corrected per D085-backend findings A–E. Offline only — no
patch applied, no source/build/deploy/device/Git changes.

- `docs/evidence/devin-d086-frontend-20261019.md`: corrected plan.

## Resolution

- **A (BLOCKING→fixed)**: reverted to uncached
  `access("/tmp/macws_frame_pace_diagnostics", F_OK)` — verified the
  deadlock myself: `enable_experimental_if_requested` (:5725) sweeps
  all sentinels, recreates only under `--diagnostics`+`--experimental`
  (= umbrella confound), and `production_preflight` clears registered
  survivors, so no registered sentinel can exist at WS launch and a
  cached flag latches false. The keyboard-latency sentinel is
  registered-AND-uncached precisely because post-launch creation is
  the only arming window. tsv registration retained for free
  `clear_diagnostic_state` cleanup; launcher untouched (4th file gone).
- **B**: arm witness moved into the trace helper — one
  `armed_acked`-gated `COEXIST-PACE sentinel-armed arm=<flag>` line on
  first gated call; four-state disambiguation table documented.
- **C**: pre-arm protocol now requires `rm` of `.trace` before
  sentinel `touch`; acceptance requires trace mtime ≥ generation
  lstart + first line = `sentinel-armed`.
- **D**: all line numbers re-pinned (filter :331–:341, convention
  :343–:349, kbd pair :9074/:9082–:9097, arm site :6477–:6485, sites
  :20056/:27395, launcher :5725/:5689/:3768/:5959/:6043/:3531).
- **E**: `misc/audit_runtime_switches.py` declared as the post-tsv
  check; rollback restated (prior hash-pinned dylib + WS restart
  under session/rollback + dual-namespace sentinel/trace rm +
  registered-sweep backstop).

## Size

3 files, `mac_hooks.m` ≈33 lines / 3 regions, tsv +1 row, regenerated
flags file. Activation still bound to rebuild+redeploy+restart plus a
separately granted post-launch sentinel lifecycle; current generation
unmeasurable.

(handover to codex now)
