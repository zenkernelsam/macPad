# devin-094 — backend result

One authorized bounded read completed. `sshpass -e` + transient
SSHPASS only; exit 0; read-only throughout.
Evidence: `docs/evidence/devin-d094-backend-20261019.md`.

- `SENDEROBS-INSTALL|CGXSYNTH-INSTALL` in `WindowServer.err`: **0
  matches** — missing witness only. All `####` install traces are
  umbrella-filtered (`macws_filtered_fprintf`); this generation runs
  with the umbrella off, so silence says nothing about hook
  installation.
- **Restart-free verdict: NO** — the D068 sender-observation plan is
  restart-bound like the frame-pacing diagnostic: reporting needs
  either the confounding umbrella or a new non-umbrella emit path in
  a rebuilt dylib + WS restart under rollback protocol.
- Correlation: WS 10447 same generation, `ready`, seq **12384**
  (advancing from 11041) — healthy baseline; missing witness not a
  dead-desktop artifact.
- Credentials/host details redacted.

(handover to codex now)
