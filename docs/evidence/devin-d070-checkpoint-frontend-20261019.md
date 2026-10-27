# D070 — durable read-only checkpoint for the next worker

2026-10-19. Compiled from AGENTS.md, `_relay/SYNC.md`, and the evidence
docs cited inline. FACT/THEORY labels are explicit. This document
changes nothing and asserts nothing new — it freezes the stop point
after the D069 administrative closeout.

## 1. Accepted: visible desktop (FACT, current generation)

- `macws_final_composite.state`: `state=ready`, `producer=10447`,
  `reason=validated-final-composite`, sequence continuously advancing
  (695 → 1529 → 4494 → 6055 across the day's reads — same generation).
- Visible: wallpaper, Dock, menu bar, Finder frontmost with on-screen
  window 32 (`223,149 920x436`), Terminal win 27 (dead session).
- Live pids: WS `-virtualonly -daemon` **10447**, macwsinputd 10436,
  macwsdisplayd 10540, Dock 10668, Finder 10595, watchdog 9845,
  MacWSHost (iOS) 8318, chroot tccd-system **10300**, iOS tccd 883.
- Evidence: `docs/evidence/devin-d037-gated-live-desktop-20261018.md`,
  `devin-d041-input-boundary-20261019.md` §1,
  `ventura-22F82-input-transport-and-composite-20261013.md`.
- Composite delivered via `macws_publish_scanout_after_swap`
  (EndUpdate never runs on this build). Desktop generation: WS started
  2026-10-19 00:17:23.

## 2. Accepted: exact AppInput path (FACT)

- AppInputBridge delivers KeyDown/KeyUp to an **exact PID/window** at
  the AppKit layer — accepted on device (D041 §3): Finder 10595 win 32,
  ownership verified twice (app-published metrics + WS window list).
- Per-app endpoints `/private/tmp/macws_app_input.<pid>.sock` exist for
  all live app pids.
- This path **never crosses TCC** — it is not a global event-synthesis
  sender and does not hit SkyLight's sender-permission gate.
- It is the working input mechanism; do not build a duplicate
  transport.

## 3. Boundary map: global input / TCC (FACT unless labeled)

Two independent upstream denial surfaces, both BEFORE payload content:

1. `com.apple.WindowServer.HIDEvents` → `bootstrap_look_up` rc=1102 —
   unregistered name; `CGEventPost(kCGHIDEventTap)` dispatches to
   nothing. CGS control channel itself works (WARP-OK, SLSGetEventPort
   valid receive right).
2. `SLSPostEventRecord(canonical)` → **1002** at WS's
   `CGXSenderCanSynthesizeEvents` gate (SkyLight `+0x318464`) → chain
   gate→dispatcher `+0x3407ec`→impl `+0x340da0` returns 0.

TCC/tccd side (verified on exact `tccd_arm64e` `9845D17F`):

- Routing closed (D065): WS (uid 0) → `com.apple.tccd.system` →
  libmachook maps → `com.apple.macosbooter.tccd.system` → launchd
  endpoint of pid 10300 (`tccd system`, isSystemServer=1). Server
  check-in uses the same map. `com.apple.tccd` unmapped → iOS tccd 883.
- Attribution order **path-first, responsibility-last**
  (`initWithMessage:` → getAuditToken (kernel peer) →
  `createProcessFromAuditToken` (`proc_pidpath_audittoken`) →
  `_evaluateResponsibleProcess` → libquarantine
  `responsibility_get_responsible_audit_token_for_audit_token` →
  `__mac_syscall("Quarantine",0xb6)`). D060 evidence.
- Sole reply-authoring site emits empty `x23` dict (`count=0`) on bail
  — observed live: `TCCAccessRequest` from chroot probe →
  `responsibility_get… (#103) Policy not found` → `Failed to create
  attribution chain` → `count=0` (D050, R002 artifact sender_pid=46525
  — a probe, not WS).
- Service registry hardcoded (`0x35200–0x35700`): `isPerSystem=1` on
  PostEvent/ListenEvent/ScreenCapture/Accessibility/
  SystemPolicyAllFiles/DeveloperTool; `shouldAllowSameTeam=1` only on
  AppleEvents; `hasIndirectObject` on AppleEvents/FileProviderDomain.
  D064 evidence.
- SkyLight consumes exactly PostEvent/ListenEvent/ScreenCapture;
  check is fail-closed (missing symbol → deny). D064 §4.
- Non-system tccd alternative structurally excluded for input
  services (`0x6710` early-bail, same empty reply) — but this kills
  only the *alternative*; the system endpoint is the legitimate path
  and is blocked only **conditionally** on the provider hypothesis.
  (D066 correction — do NOT cite "unreachable by construction".)
- Probe-side `TCCAccessPreflightWithAuditToken` returned `1` on an
  **empty** reply — an API default encoding, NOT an explicit verdict
  (per coordinator + D041; do not read as allow or deny).
- `#103` producers formally split (R009): no-match default
  (`w21=0x67` preset @ `mac_syscall` `0x865de80`, image-verified),
  foreign-policy precheck veto (ops+0x388), dispatched-handler return.

## 4. Provider hypothesis — UNRESOLVED

- iOS dyld cache table: zero libquarantine images (2609 entries, FACT
  at artifact level). Native getter unreachable to non-platform
  helpers (R004: dlsym NULL, dlopen ENOENT, flat-namespace SIGABRT
  crash artifact).
- R004 controls already ran: Ventura chroot getter on chroot-self
  token AND live native token (pid 51948) → both `errno=103
  "Policy not found"`. **Do not repeat.** `__mac_syscall` #381 entry
  proven live on the iOS kernel by those returns.
- Kernelcache image: `"Quarantine"` policy name 0 hits across 80MB;
  AMFI/Sandbox names present. Boot kc SHA `78959dd9…` = repo img4
  (CHIP=0x8103 = T8103; "T8112" filename is mislabel). Payload↔img4
  byte linkage never closed (bvx2 decompressor absent locally) —
  image-level evidence only.
- **D069 invalidation**: backend's live KRW reads declared
  inconclusive/inadmissible (scope violations honestly recorded:
  password-in-argv, multiple helper deploys, trustcache cdhash
  additions with no per-hash removal — dead entries remain,
  `trustcache clear` correctly NOT run as it would wipe ~311
  legitimate entries). No policy snapshot was produced; the provider
  question is **exactly** at D068's end-state.

## 5. Known deployed hashes (pinned identities)

| Artifact | Identity |
|---|---|
| tccd | fat `42c388e8…`, arm64e thin `039f64b7…`, UUID `9845D17F-0409-3884-8A17-260C729636E5` |
| SkyLight | UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880` |
| chroot libquarantine | UUID `A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3` |
| boot kernelcache | SHA-256 `78959dd9…` == repo img4; `xnu-8792.82.2~1` |
| launchdchrootexec | reviewed `eecf7c3c`, staged `1004c790`, deployed-share `438c6482` — all three carry `POSIX_SPAWN_SETEXEC` (D058) |
| libmachook | arm64e `aa041efd…`, arm64 `21f81b5c…` |
| macwsinputd | `0bc473d1…` |
| probes | event `0f4eac6e…`, tccd `6802512f…`, winlist `cf1cefd0…`, cgwl `fcaa3c94…` |

Device: iPad13,11, iPadOS 16.3 20D47, Dopamine rootless, Ventura
13.4/22F82 chroot at `/var/mnt/rootfs`.

## 6. Unsafe or unproven next steps — do not do

- Blind `__mac_syscall("Sandbox",0xb6)` A/B — selector semantics are
  policy-specific, no read-only contract (withdrawn D066).
- Repeating R004 getter controls — exhausted, both token classes.
- `trustcache clear` — wipes legitimate entries; dead cdhash entries
  from D069 remain by necessity.
- Executables from `/tmp` or `/private/var/tmp` — SIGKILL'd
  (launch constraint); `/var/jb/tmp` only.
- Password on argv (`sshpass -p`) — recorded violation; env-only.
- Reading "no impl record" as "impl never called" — needs install
  proof + drop accounting (D068).
- Reading `impl_rc=0` as an explicit policy verdict — conflates
  unresolved-symbol / eval-failure / denial (D068); and `impl_rc≠0`
  means *allowed* (D042 inversion corrected).
- "Provider must be absent" / "global input impossible" — both
  overclaims; the former is hypothesis, the latter conditional.
- Any assertion/TCC/check NOP, forced branch, or constant-return —
  the AGENTS.md band-aid table stands.

## 7. Minimal safe next discriminator (designed, unexecuted)

**First (read-only, no restart)**: check the CURRENT WS generation's
stderr for `SENDEROBS-INSTALL … permitDisp=0 permitImpl=0` —
decides whether the sender-observation hooks are resident in pid 10447.
- If resident: the D068 plan runs restart-free — arm
  `/var/mnt/rootfs/tmp/macws_sender_observation`, one bounded owned
  post (`macos_ws_event_probe post`), decode the 144-byte records
  (D049 decoder: seq gaps = drops; `lazy_pre/lazy_post` split
  unresolved-symbol from reached-call; `rc` + `invocation` chain),
  cross-witness `tccd-system.err` same window, disarm+cleanup.
  Requires explicit signoff for the *transitive* WS→tccd request.
- If not resident: restart-bound = heavyweight separate authorization
  (session/rollback protocol).
- Kernel-side alternative (backend, unexecuted): one properly-bounded
  `mac_policy_list` walk (D067/D068 contracts; D069 attempt voided) —
  the acceptance contract is `devin-d069-policy-snapshot-independent-
  review-20261019.md` §2.
- Junk-name `__mac_syscall("ZZZmacwsnop",…)` control — zero-dispatch
  discriminator for name-miss-vs-precheck — designed D066 §3,
  unexecuted, needs authorization.

## 8. Share-lessons mapping (active checklist `coordination/reviews/share-lessons-active-checklist-20261018.md`)

- Visible desktop preserved first — layered diagnosis
  decode→producer→displayd→Host→pixels; uptime ≠ success. ✔ used here.
- One-variable tests with frozen hashes + rollback path; record
  rejected hypotheses before handover. ✔ the D068 plan is exactly that.
- Verify deployed identity/callability before reimplementation. ✔ §5.
- No constant-return/protocol bypasses. ✔ §6 lists them as banned.
- Sequoia supplier mechanisms are research inputs only, not Ventura
  acceptance. (unchanged)

## Provenance of this checkpoint

Inputs: AGENTS.md (patch/evidence discipline), SYNC.md heartbeat
history, D033/D037/D041/D050/D055/D059/D060/D064–D069 evidence docs,
R004/R008/R009 artifacts, `share-lessons-active-checklist-20261018.md`.
No device access, no probes, no deploy/restart, no source/build/Git
changes.
