# D071 frontend result — install evidence, frame-wait audit, ledger reconciliation

Read-only local audit; no device access/mutation, no probes, no
build/deploy/restart/trustcache/password/source/Git changes. Evidence:
`docs/evidence/devin-d071-frontend-20261019.md`.

## 1. SENDEROBS-INSTALL in current WS generation — ABSENT from saved evidence

Zero `SENDEROBS-INSTALL` lines exist in any local artifact. The only
install witnesses are Oct-14 `CGXSYNTH-INSTALL … isWS=1 permit=0`
(gate hook only, prior WS generation, pre-dating the disp/impl
instrumentation). For gen-10447 hook residency is UNKNOWN: hooks arm
only at WS launch under the chroot sentinel, and that generation's
stderr was never captured. Deciding read =
`grep SENDEROBS-INSTALL /var/jb/var/mobile/WindowServer.err` on
device — an approved read-only pattern but requires access not granted
this packet → missing input (the D068 prerequisite, unchanged).

## 2. Frame waits / service messages / reopen freshness

- Pacing = completion-timestamp waits
  (`macws_coexist_wait_for_completion_slot`) with a poll-based
  interaction-wake socket that shrinks the interval mid-wait + bounded
  10-Hz producer wake on the activity fd; moved pre-lock after the
  `/tmp/ws-magnify.sample` mutex evidence. Service-message
  availability IS event-driven; the residual artificial wait is the
  pace floor when no wake arrives.
- Reopen freshness verified in source: `WaitForWindowMetricsFlagsAfterGeneration`
  gates reuse on generation + **at least one real visible window**
  (Terminal-scene-discard lesson in-code); monotonic generations;
  dedicated child reaper.
- **Runtime wait measurement: NOT available** — every `COEXIST`
  diagnostic is gated on the umbrella `macws_runtime_diagnostics`
  sentinel, which also arms the alternate display producer
  (non-causal). No dedicated non-confounding toggle exists; none
  armed this generation. Any measurement needs a new sentinel +
  deploy — out of scope.

## 3. Layered ledger — only missing witnesses

Witnessed (do not redo): decode/asset (static wallpaper), provider
permit/PAC path (D037 ALL PASS), producer→final composite (state file,
seq advancing), displayd validation (`workspace-graph-state` ready ×4),
Host pixels (D037/D039 PNG captures, visually verified).

Missing only: (a) a **current-generation** pixel artifact post-D041 —
state seq is a displayd claim, not pixels; (b) gen-10447
SENDEROBS-INSTALL status (§1); (c) a causal wait measurement (needs
new dedicated toggle — no current path).

(handover to codex now)
