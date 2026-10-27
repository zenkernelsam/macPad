# devin-039 frontend result — D037 runtime acceptance audit

Worker: macPad Devin D039 frontend 9589. Read-only; no device/backend
contact. Deliverable:
docs/evidence/devin-d039-runtime-acceptance-review-20261018.md

## Verdict: ACCEPTED — visible desktop restored, causally attributed

Six-rung ladder assessed independently:
probe PASS (arm64e braaz/auth/callable-orig/refusals on real bytes),
identity PASS (live IOMFB uuid 7AC531B7 + QC uuid match all 4 pins —
arm64e slice question resolved), changed-bytes PASS-via-function
(task_for_pid denied; final-composite-received producer=7763 is the
behavioral witness), counters PASS (replay-received 1..7), state PASS
(ready, seq 9→60, reason=validated-final-composite), pixels
**PASS — FACT**: backend published saved PNGs mid-audit
(`docs/evidence/devin-d037-gated-live-desktop-20261018/frame-*.png`);
this reviewer visually verified them — frame-macos-desktop.png shows
a real Aqua desktop (Finder menu bar 文件/编辑/…, Ventura forest
wallpaper, macOS Dock incl. chroot-typical `?` icons) under the
macPad 控制中心 host overlay; frame-pre-activation-ipados.png shows
real iPadOS WeChat — valid negative control.

Frames-vs-visible kept distinct: transport proven by counters/state;
visible desktop now proven by inspected pixel artifacts (FACT).

Confound check CLEAN against source: launch had --experimental WITHOUT
--diagnostics → preflight rm -f's the diagnostics sentinel
(macos_gui.sh:5747-57) → marker-gated alternate producer OFF → frames
provably via the new SwapEnd→cancel→complete→publish chain = first
direct causal witness for the D033 permit-gate attribution.
--no-vnc = production path only; runtime-cap=300 watchdog-enforced
fired correctly; lease released; iOS-side twin untouched; deployment
kept per keep-on-pass rule; teardown clean, iOS restored.

Residuals recorded (non-blocking): no in-WS raw byte read (functional
proof stands in; cache-extracted identity, not mapped-memory read);
saved PNG residual CLOSED (inspected + sha256-verified); cadence
explained by source-level 4s keepalive (seq 51/~190s ≈ 3.7s matches);
replay-request published=NO resolved as vestigial pull path.
Corrections per coordinator: restoration is vs the 10-13 baseline
(10-13 was visible — not "since Oct-12"); causation covers the
deployed bundle, not per-target necessity; confound verification is
enumerated-channel, not exhaustive. Input axis untouched by design.

Stopping after handover per packet — no next task claimed.

(handover to codex now)
