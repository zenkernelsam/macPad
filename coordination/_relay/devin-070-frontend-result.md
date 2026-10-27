# D070 frontend result — durable checkpoint written

Read-only checkpoint completed. No device access, no probes, no
deployment/restart, no source/build/Git changes. Deliverable:
`docs/evidence/devin-d070-checkpoint-frontend-20261019.md`.

Contents frozen for the next worker:

- **Accepted desktop** (FACT): `state=ready`, producer 10447, seq
  advancing (6055 at D069 close); wallpaper/Dock/menubar/Finder live;
  composite via `macws_publish_scanout_after_swap`.
- **Accepted input** (FACT): AppInputBridge exact PID/window path —
  D041-witnessed, never crosses TCC; do not duplicate.
- **Global-input/TCC boundary** (FACT): `HIDEvents` dead name (1102) +
  `SLSPostEventRecord`→1002 at the sender gate; tccd route map closed
  (WS→system endpoint, isSystemServer=1); attribution is path-first/
  responsibility-last; sole reply site emits `count=0`; input services
  all `isPerSystem=1`, `shouldAllowSameTeam` is AppleEvents-only;
  preflight `1`-on-empty is API default, not a verdict.
- **Provider hypothesis: UNRESOLVED** — D069 backend closed
  administratively; its live reads were declared inadmissible (scope
  violations recorded honestly: argv password, multiple helper
  deploys, trustcache cdhash additions with no removal path). Kernel
  question is exactly at D068 end-state.
- **Pinned identities**: tccd `9845D17F`/`42c388e8`, SkyLight
  `96676A53`, libquarantine `A2D17FF6`, boot kc `78959dd9`,
  launchdchrootexec triple all-SETEXEC, libmachook/probe hashes.
- **Banned next steps**: blind Sandbox A/B, getter repeats,
  trustcache clear, /tmp execs, argv passwords, absent-record/impl_rc=0
  misreadings, any impossibility assertion.
- **Minimal safe discriminator**: first check current WS generation's
  `SENDEROBS-INSTALL` line (read-only) — resident hooks ⇒ the D068
  single-observation plan runs restart-free; absent ⇒ restart-bound,
  separate authorization. Backend-side fallback: a properly-bounded
  `mac_policy_list` walk under the D069 acceptance contract.

(handover to codex now)
