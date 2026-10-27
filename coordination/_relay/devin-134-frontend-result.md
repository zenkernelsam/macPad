# devin-134 frontend result — D130 layered evidence review

Read-only review; full report at
`docs/evidence/devin-d134-frontend-20261019.md`.

## Classification

- **L1 PASS — direct** (hashes artifact-verified), with 4 provenance
  caveats: silent `ensure_rc=1` at 22:25 (test -x short-circuit —
  script non-executable/absent then; standalone exit-0 run not saved);
  empty `== package ==` (`${…}` format vars shell-expanded — `ii` rests
  on transcription); MacWSHost `2ce42510` (D127 rebuild, undocumented);
  `.old=16afc4de` ≠ D035 `aa041efd` (D127's ancestor claim unsupported).
- **L2 partial — refusal lines are direct mapping evidence; positive
  permits are source-grounded inference** (publish ← hook ← permit at
  :19992); "no composite-hook registrations — pre-existing" confirmed in
  source. No conflation found.
- **L3 NOT REACHED — correct + source-verified** (install only at
  SkyLight-load with sentinel).
- **L4 PASS — direct** (same-session producer=22527 + seq 141→622;
  coordinator live screen corroborates); provenance = doc transcription,
  raw outputs unsaved.
- **L5 MISSING for pixels** — honestly scoped by D130; viable no-restart
  path = native capture of open MacWSHost scene.
- **L6 PASS (F1 route) — direct** (real clamp 640x469 + negative control
  rejected). Reopen-freshness (`generation>previous`+`Visible`) **not
  exercised** — metrics gen 6→7 is the input-side counter only; named
  boundary, not an overclaim. `SLSPostEventRecord 1002` still open.

## Reconciliation

- Successful start **through the symlink** upgrades "symlink suffices
  downstream" to FACT — narrows D133 vendoring to insurance, not
  critical path; helper still absent, T8103 bindfs rejection still
  unverified.
- D131/D128 boundaries preserved; no panic; IconServices unobserved.
- "Post-reboot" = host reboot only (iPad uptime continuous) — D130 noted
  it; title overstates.

## Smallest next task

One native iPadOS capture with MacWSHost foreground, correlated to
producer/sequence — no restart — plus reopen-freshness disposition;
**save raw outputs as artifacts**. Matches dispatched D135 scope.

(handover to codex now)
