# D072 frontend — acceptance-gap packet (current generation)

Read-only. No device contact, deploy, build, restart, injection,
source or Git changes. Absence from local artifacts is **never**
asserted as absence on device — only that no saved witness exists.

## 1. Gap packet — two missing current-generation witnesses

### Gap A — visible-pixel artifact for WS generation 10447

- **Status**: last pixel-level proof is D037/D039-era PNGs
  (`frame-macos-desktop.png`, 2732×2048, nonblack=0.984, visually
  verified Aqua desktop; iPadOS negative-control frame) — prior
  generation. Current generation (WS 10447, started 2026-10-19
  00:17:23, seq advancing to 6055+) is covered only by the
  displayd-side state file (`state=ready`, `validated-final-composite`)
  and the D041 operator sighting. **The state file is a validation
  claim, not pixels.**
- **Exact single read that closes it**: one bounded capture of the
  presented scene under the current generation — the same mechanism
  the accepted captures used (MacWSHost readback / VNC frame pull,
  read-only to the desktop), saved to
  `docs/evidence/devin-d072-current-gen-frame.png` with a
  `nonblack`/`mean` numeric check + the state-file seq at capture time.
  One frame, one artifact; no producer/display changes needed.
- **Not a gap**: the composite itself — `reason=validated-final-composite`
  + advancing seq already proves frames land through the accepted
  chain. Only the *pixel-level presentation* witness is missing.

### Gap B — `SENDEROBS-INSTALL` for WS generation 10447

- **Status**: zero `SENDEROBS-INSTALL` lines in all saved artifacts.
  Only install witness on disk is Oct-14 `CGXSYNTH-INSTALL … isWS=1
  permit=0` (gate hook, prior generation, pre-dating the disp/impl
  instrumentation). Hooks arm only during `loadImageCallback` at WS
  launch when the chroot sentinel exists at that moment; whether it
  existed at the 00:17:23 launch is not recorded locally.
- **Exact single read that closes it**:
  `grep -nE "SENDEROBS-INSTALL|CGXSYNTH-INSTALL"
  /var/jb/var/mobile/WindowServer.err | tail` on device — one grep,
  bounded output. Interpretation table:
  - `SENDEROBS-INSTALL … permitDisp=0 permitImpl=0` → hooks resident;
    the D068 single-observation plan runs restart-free.
  - line absent / `permit≠0` / `permitFn` null → not resident; the
    observation is restart-bound (separate heavyweight authorization).
  - `CGXSYNTH-INSTALL … permit=0` alone → gate-level only, same
    restart-bound conclusion for impl coverage.

Both reads are individually bounded and read-only; neither restarts
or perturbs the desktop. They are simply not yet authorized in any
packet.

## 2. Pacing/wake + reopen freshness vs share lessons — rechecked

Share lesson: *"Measure artificial frame wait against service-message
availability before adopting wakeup changes."* Recheck verdict:

- Service-message availability is already event-driven:
  interaction-wake socket (`poll`, shrinks `target_ns` mid-wait,
  `mac_hooks.m:27489–27550`), bounded 10-Hz producer wake on the
  activity fd (`:27355`), generation-gated descriptor reopen
  (`:27354` region). The residual artificial wait is only the pace
  floor when no wake arrives — by design.
- Reopen freshness is in-code and correct
  (`WaitForWindowMetricsFlagsAfterGeneration`, macwshostd
  `main.m:1444`): generation-gated reuse *plus* the real-visible-window
  requirement; monotonic generations; dedicated child reaper. Meets
  the lesson's "current request/result and fresh metrics instead of
  relaunching" standard.
- **The missing piece is only measurement**: every pace diagnostic
  (`#### COEXIST pre-lock completion pace` `:20056`, `#### COEXIST
  DIAGNOSTIC activity pace` `:27404`) is gated on
  `macws_runtime_diagnostics_enabled()` — the umbrella sentinel that
  also activates an alternate display producer (confound).

### Smallest non-confounding toggle (design only — not added)

Precedent exists and is field-proven:
`MacWSKeyboardLatencyDiagnosticsEnabled()` (`AppInputBridge.m:963`) =
`access("/tmp/macws_keyboard_latency_diagnostics", F_OK) == 0`,
uncached per call, scoped to its own trace sites, D041 lifecycle
(arm ~10 min → extract → remove, zero residue).

Minimal equivalent for frame waits: one dedicated sentinel
`/tmp/macws_frame_pace_diagnostics` (chroot `/tmp`, host
`/var/mnt/rootfs/tmp`) checked uncached at **exactly the two
existing `#### COEXIST` fprintf sites** — either replacing their
umbrella gate or OR-ing alongside it. Properties:
- ~4 lines of source (one `_enabled()` helper + one gate at each of
  two sites); no state, no producer/display side-effects, no hooks;
- fail-closed (absent file → silent, identical to today);
- live disarm semantics identical to the keyboard/sender-obs
  precedents (uncached `access`);
- emit volume already bounded (`sequence<=4 || %600` /
  mode-change-only);
- gives `interval=` and `slept=` per paced update + mode/age fields —
  the exact "artificial wait vs message availability" measurement the
  lesson asks for, with zero confound.

Cost to activate later: one libmachook rebuild + redeploy = WS-restart-
bound (same as any code change). Cannot be armed in the current
generation at all — flagging the toggle is only *prepared*, not a
measurement that can happen this generation.

## 3. Accepted evidence stays closed

D037 provider/PAC (ALL PASS), D041 AppInput acceptance (exact
PID/window), composite delivery (state-file ready, seq advancing),
D065 endpoint map, D064 service registry — all remain accepted; no
re-verification or duplicate implementation proposed.
