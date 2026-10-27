# D134 — independent review of D130 layered runtime evidence

Date 2026-10-19. Packet `coordination/_relay/devin-134-frontend-task.md`.
Read-only. Reviewed `devin-d130-backend-20261019.md`, its result file,
the saved artifact `current-ipad-post-reboot-d127-verify-20261019.json`
(22:25, pre-start), `mac_hooks.m` sources, and coordinator SYNC
observations. No device access, no edits, no runtime claims of my own.

## 1. Per-layer classification

| Layer | D130 verdict | Independent classification |
|---|---|---|
| 1 package/decode | PASS | **Direct** — artifact-verified hashes (`42b75cf3`/`e84d992a`/`c97acda3`) persist. Caveats in §2. |
| 2 provider+permits | PASS (partial) | **Direct for mapping**: 17 `REFUSED no permit` lines are emitted by the provider's own gate inside chroot processes — proves provider loaded + permit lookup executing fail-closed. **Inference for positive permits**: publish path requires the QC-cancel-completion hook (`mac_hooks.m:19992` gated on `register_hook_permit` success), so `sequence=622` advancing implies permit+install — source-grounded inference, not a direct `permit=0` log. D130 labels this "partial via direct evidence" — accurate. Source check: registration callsites are the gate (6134), 6425, 19992, 20127/20129; composite-pipeline hooks indeed have no registration sites — "pre-existing design state" confirmed. |
| 3 SENDEROBS | NOT REACHED | **Correctly unclaimed + source-verified**: `mac_hooks.m:6158-6174` installs the three hooks only at SkyLight-load when the sentinel pre-exists; post-hoc arming of a running WS can emit nothing. No contradiction. |
| 4 final-composite | PASS | **Direct** — `state=ready producer=22527 sequence=141→622 validated-final-composite` observed advancing within the session, `producer=22527` matches live WS pid, workspace graph lists real surfaces (wallpaper 171 snapdone=1, menubar 170, statusbar 166). Generation binding is direct (same-session pid+seq). Coordinator's independent live-screen observation (SYNC 22:52) corroborates seq 622. Provenance caveat: raw captures not saved as artifacts — doc transcription only. |
| 5 pixels | PARTIAL | **MISSING for byte-level** — D130's honesty is correct: `--no-vnc` disables the RFB bridge by design; `iosurf_probe` trapped (rc=133 unsigned-adhoc); no panel observation. State files/snapdone are metadata, not pixels. Viable existing path without restart: native iPadOS screen capture of the already-open MacWSHost scene (pid 17584, `windowing ready=yes caps=0x7f`), or a displayd-retained-surface host-side dump if tooling exists. |
| 6 exact-window F1 | PASS (live) | **Direct** — positive `ConfigureWindow` ack `req=640x480 applied=640x469` (469 = Terminal's real size-increment clamp → genuine NSWindow setter ran); negative control with `GlobalSystemSurface` rejected with no ack/gen bump. First live accept/reject split — previously unit-test only. **Freshness scope**: generation 6→7 is the *metrics-side* counter; the reopen-freshness contract (`generation > previous` + `Visible`, hostd main.m witness path) was **not exercised** — no reopen occurred. D130 does not overclaim this; naming the boundary explicitly: F1 route PASS, reopen-freshness machinery untested. `SLSPostEventRecord 1002` remains open and separate — D125's witness list item 4 still unmet for the *event-post* route. |

## 2. Artifact discrepancies (must reconcile before citing D130 verbatim)

The only saved artifact for this window is the 22:25 pre-start verify:

- **`ensure_rc=1` with empty stderr.** Every `exit 1` path in
  `ensure_jb_usr_bind.sh` prints to stderr; empty stderr + rc=1 means
  the `test -x … && script` short-circuit fired — the script was
  non-executable or absent at `/var/jb/usr/macOS/bin/` at 22:25, while
  the symlink itself was present. D130's "standalone ensure → exit 0"
  and the successful start imply it worked later — timing/attribution
  unexplained; the standalone run's own output isn't saved.
- **`== package ==` section empty.** The artifact's `dpkg-query -f` had
  its `${Package}/${Version}/${Status}` fields expanded by the outer
  shell → package state was *not* verified by this artifact; `ii 0.3.4`
  rests on D130's transcription.
- **MacWSHost `2ce42510` ≠ D125's `d4a0ec51`** — consistent with D127's
  fresh rebuild shipping a new binary, but undocumented in D130.
- **`libmachook.dylib.old = 16afc4de` ≠ D035 `aa041efd`** — contradicts
  D127 §5's ".old = Oct-14 D035 copies" claim. The preserved ancestor is
  a different build; identity unknown.

## 3. Cross-review reconciliation

- **D131**: its "layer 1 only" verdict was pre-start state; D130
  supersedes it *for this generation* — layered acceptance is
  per-generation. No conflict.
- **D132/D133 mount_bindfs**: **the successful start ran entirely
  through the manually-restored absolute symlink** — upgrading D127's
  open question ("symlink contract-valid but downstream unproven") to
  FACT: the symlink satisfies the full start chain end-to-end. This
  narrows D133's vendoring recommendation to insurance for fresh
  installs / bindfs-capable kernels — it is no longer this device's
  critical-path fix. The helper remains absent (fact unchanged); the
  T8103 bindfs-rejection premise remains unverified (no mount attempted).
- **D128 crash-vs-panic**: only `ExcUserFault` user faults (macwsinputd
  ×2 GUARD/XPC_EXIT non-fatal, codesign during postinst, unrelated
  Music), no panic report, and IconServices' state this generation is
  **unobserved** (not listed) — boundary preserved.
- **"Post-reboot" framing**: D130's own §1 note correctly states the
  iPad never rebooted (host computer did; hostd 1821/MacWSHost 17584
  uptime continuous). Trustcache persistence is therefore *same-uptime*
  persistence — accurate as written, but the title overstates it.

## 4. Smallest next bounded acceptance task

With the generation still live (`phase=ready`, WS 22527), layer 5 is
closeable **without any restart**: exactly one native iPadOS screen
capture while MacWSHost is foreground, correlated to the same
`producer=22527`/sequence, plus explicit disposition of the
reopen-freshness audit (exercise `WaitForWindowMetricsFlagsAfterGeneration`
on a real reopen, or record it as unexercised). Save raw command outputs
as artifacts this time — the current generation's L2/L4/L6 rest on doc
transcription alone. Secondary optional probe: the replay-channel
`source=unavailable` anomaly (read-only inspect of the replay register
path) — separate task, not blocking.

This matches the already-dispatched D135 backend mandate; endorsement
stands with the added provenance requirement.

(handover to codex now)
