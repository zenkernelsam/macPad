# D072 frontend result — acceptance-gap packet

Read-only; no device contact, deploy, build, restart, injection,
source or Git changes. Absence-from-artifacts is never claimed as
absence-on-device. Evidence:
`docs/evidence/devin-d072-frontend-20261019.md`.

## Gap A — current-gen pixel artifact

Last pixel proof is D037/D039-era PNG (prior generation). Gen-10447
has only the displayd-side state file (a validation claim, not pixels)
+ D041 operator sighting. **Exact closing read**: one bounded
presented-scene capture under the current generation (same MacWSHost
readback / VNC-frame mechanism the accepted captures used) saved with
`nonblack`/`mean` check + concurrent state seq — one frame, one
artifact, zero perturbation.

## Gap B — SENDEROBS-INSTALL witness

Zero lines in all saved artifacts; only Oct-14 gate-hook install
(`isWS=1 permit=0`, prior gen, predating disp/impl). **Exact closing
read**: `grep -nE "SENDEROBS-INSTALL|CGXSYNTH-INSTALL"
/var/jb/var/mobile/WindowServer.err | tail` — `permitDisp=0
permitImpl=0` → restart-free D068 observation; absent/nonzero →
restart-bound (separate authorization).

## Wait-measurement toggle (design only, not added)

Share-lesson recheck: service availability is already event-driven
(interaction-wake poll + 10-Hz producer wake + generation-gated
reopen); reopen freshness meets the visible-window/generation
standard. Sole missing piece is measurement — all `COEXIST`
diagnostics hang off the confounding umbrella sentinel. **Smallest
non-confounding toggle**: dedicated sentinel
`/tmp/macws_frame_pace_diagnostics` (uncached `access(F_OK)`, the
D041-proven `MacWSKeyboardLatencyDiagnosticsEnabled` pattern) gating
exactly the two existing `#### COEXIST` fprintf sites — ~4 lines,
fail-closed, live-disarmable, bounded emit (`interval=`/`slept=`/
mode/age). Activation would still be rebuild+redeploy-bound — prepared,
not executable this generation.

## Accepted evidence closed

D037 provider/PAC, D041 AppInput, composite delivery, D065 endpoint
map, D064 registry — unchanged, no duplicate work.

(handover to codex now)
