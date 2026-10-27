# D130 — Post-reboot runtime acceptance of the D127 install

Date: 2026-10-19. Device: iPad13,11 / T8103, iPadOS 16.3 (20D47), Dopamine
rootless. Scope: exactly one owner-managed `macos_gui.sh start coexist
--no-vnc` attempt against the D127 package; no reinstall, no rebuild. All
D127-installed identities and the `/var/mnt/rootfs/var/jb/usr` symlink must
persist across reboot.

## 1. Pre-start state (post-reboot, read-only)

- Package: `ii com.kdt.macosbooter 0.3.4` — persists.
- Identities unchanged post-reboot:
  `/var/jb/usr/macOS/lib/libmachook.dylib` `42b75cf3…` (arm64e),
  `libmachook_arm64.dylib` `e84d992a…` (arm64), rootfs copies identical,
  rootfs provider `c97acda3…`.
- Trustcache still carries all four CDHashes (`a08fdb50`, `74d95de2`,
  `2e19270d`, `8864745f` — 2 entries each in `jbctl trustcache info`).
- `/var/mnt/rootfs/var/jb/usr -> /var/jb/usr` symlink persisted across the
  reboot; standalone `ensure_jb_usr_bind.sh` → **exit 0** (the D127 abort
  point is now satisfied).
- All sentinels absent; no WindowServer/GUI processes running.
- `macwshostd` (1821), `macwskeychaind` (1828), `macwsallocd` (1840),
  `autosignd` (13890) loaded; MacWSHost app open (pid 17584, rb-legacy).
- Boundary: `/tmp/macos_gui_start.state` held a stale record from the D127
  attempt (`pid=16263, phase=trust`, dead pid). `MacWSStartup.log` tail was
  the D127 mount_bindfs failure (1282 bytes).
- NOTE on timing: post-reboot device uptime continued (hostd still pid
  1821, MacWSHost 17584 — the "reboot" was the host computer, not the iPad).

## 2. The single start attempt — SUCCEEDED

`timeout 300 bash /var/jb/usr/macOS/bin/macos_gui.sh start coexist --no-vnc`
(pid 21584). The local SSH client timed out at 120 s but the remote
`timeout 300` run continued and completed: state file →
`phase=ready, detail="WindowServer and requested clients are ready"`,
127 s elapsed.

Live processes afterward: WindowServer 22527 (`-virtualonly -daemon`),
OSXvnc-server 22786 (localhost-only pointer proxy), Terminal 22802,
Dock 22769, ControlCenter 22707, SystemUIServer 22705, Finder 22701,
pbs 22671, pboard 22647, launchservicesd 22308, macwsinputd 22502,
macwsdisplayd 22644, macwsinteropd 22664, plus ~40 launchd jobs
(`windowserver`, `dock`, `finder-desktop`, `systemuiserver`, `terminal`,
`display`, `input`, `interop`, …). `macws_control_probe` status:
`protocol=12 rootfs=yes windowserver=yes` (vs the `windowserver=no`
baseline); `windowing-status`: `ready=yes abi=1 pid=1796 capabilities=0x7f`.

## 3. Layered verdicts

### Layer 1 — package/decode identities: PASS
Hashes, arch slices, package state, CDHash trust membership all verified
post-reboot (§1). The provider publish (`c97acda3`) and thin-split
libmachook pair are byte-identical in `/var/jb` and `/var/mnt/rootfs`.

### Layer 2 — provider mapped + permits observed: PASS (partial via direct
### evidence)

- `WindowServer.err` (and `osxvnc.log`) show the fail-closed provider
  actively consulting permits inside chroot processes: 17 `MACWSHook:
  REFUSED no permit for target` lines (SkyLight composite family,
  `xpc_connection_enable_sim2host_4sim`, featureflags, IOKit matching,
  libmachook_arm64-internal XPC hooks, etc.).
- Positive install evidence — the `IOMFB CANCEL-COMPLETION` QC observer
  is what calls `macws_publish_scanout_after_swap()`
  (`mac_hooks.m:21856`), and it only installs after
  `macws_register_hook_permit()` succeeds (`mac_hooks.m:19992`). displayd's
  `macws_final_composite.state` shows `state=ready producer=22527
  sequence=141→622 reason=validated-final-composite` — composites are
  being produced and validated continuously, which requires that hook to
  be installed. That is the positive permit+install witness.
- The composite-pipeline hooks (`StartComposite*`, `EndUpdate`,
  `WSCompositeDestinationCreate…`, `PrepareForUse`) are refused — they
  never had permit registrations in the source (only 9 registration sites
  exist; none cover those offsets). That is a **pre-existing design state**,
  unchanged from D035/R012: those are the tolerate-nil/pop-on-bail
  invariant-restorer hooks, not the publish path. The final-composite
  publisher flows through the permitted QC-cancel-completion observer.
- Residual honest limit: `SENDEROBS-INSTALL`/`CGXSYNTH-INSTALL` lines are
  `fprintf(stderr)`-gated by the diagnostics sentinel — not observable in
  this production run.

### Layer 3 — SENDEROBS canonical event: NOT REACHED this generation
The gate/dispatcher/impl hooks install only at SkyLight-load time when
`isWS && (diag || senderObs)` is true (`mac_hooks.m:6158-6174`) — i.e.
arming `/tmp/macws_sender_observation` now can produce no records in the
already-running WindowServer (production preflight confirmed all
diagnostic sentinels OFF). Observing a canonical event requires a
sentinel-armed cold start, which is out of this task's single-attempt
bound. No claim.

### Layer 4 — final-composite state/sequence/producer: PASS
- `/var/mnt/rootfs/tmp/macws_final_composite.state`:
  `state=ready producer=22527 sequence=622 reason=validated-final-composite`
  (updated continuously — sequence advanced 141→622 during verification;
  `producer=22527` is WindowServer).
- `macws_workspace_graph.state`: real desktop catalog — `canvas=2732x2048`,
  wallpaper (Dock surf=171, snapdone=1), menubar (WS surf=170),
  StatusIndicator (WS surf=166), Control Center items, Finder, Terminal
  (dest 0,50 1140x742 before my resize test).
- Replay-channel anomaly (documented, not blocking): displayd's
  replay-request/received exchange to WS's replay server answers
  `source=unavailable published=NO` for every request while the main
  final-composite channel validates frames normally — the replay source
  register inside WS never carries completion data. The validated delivery
  itself is proven by the state file + workspace graph, not the replay.

### Layer 5 — visible pixel inspection: PARTIAL / not directly captured
- OSXvnc is running (`-rfbnoauth -localhost`, detected HiDPI display,
  listening on 5900) but `--no-vnc` deliberately disables the RFB
  framebuffer bridge (`macws_vnc_fb` absent; "localhost-only pointer
  proxy"). An RFB handshake completes (fb 1366x1024 "macOS-iPad") but no
  FramebufferUpdate ever returns — the pixel bridge is off by design.
- `iosurf_probe` inside the chroot traps (rc=133) — unsigned-adhoc in
  this context; not a usable pixel path.
- What IS evidenced: displayd has validated 622+ composites from
  producer=WS; the workspace graph lists real surfaces (wallpaper 171,
  menubar 170, statusbar 166, `snapdone=1` on wallpaper); windowing
  bridge reports `ready=yes capabilities=0x7f`; the layer-6 resize below
  changed Terminal's real NSWindow frame.
- Not evidenced: byte-level pixel inspection in this mode, and no direct
  observation that the Host's UIKit window shows composited content on
  the physical panel (user-visible state unverified by me).

### Layer 6 — exact-window F1 route: PASS (live end-to-end)
- Mechanism reused: hostd's `SendAppInputRecord` →
  `/var/mnt/rootfs/private/tmp/macws_app_input.<pid>.sock` (SOCK_DGRAM) →
  AppInputBridge `MacWSInputRecordIsValid` →
  `MacWSAppInputRouteClassify` → native frame setter → `configureAck`
  published into the metrics sidecar + `MacWSPublishWindowMetrics`.
- Target: Terminal (pid 22802), AppKit window 27 (from its live
  `macws_window_metrics.22802.bin` entry: `win=27 flags=0x4d min=150x105
  max=16384x16384`, `configureAck` all zero, generation=6).
- Positive record sent: `MacWSInputRecord` (84 B, magic MWEV, ver=5,
  kind=15 ConfigureWindow, sceneID=`0x1B80000000` = window-flag|27<<32,
  x=640.0 y=480.0 pressure=2.0, targetPID=22802, source=Unknown).
  Result: sidecar updated — generation 6→7, `configureAck ts=… seq=1
  req=640.0x480.0 applied=640.0x469.0` (469 = Terminal's size-increment
  clamp applied by the REAL NSWindow setter, not bypassed validation).
- F1 negative control (the `4bcf20d` fix exercised live): identical record
  plus `flags=MacWSInputFlagGlobalSystemSurface` (0x40), different size
  (700×500). Result: **no ack, no generation bump** — the flagged
  exact-window record was rejected (`RejectGlobalOwner`), metrics
  unchanged. This is the first live end-to-end witness of F1's
  accept/reject split (previous evidence was classify-unit-tests only).
- Live `SLSPostEventRecord 1002` is a separate open route, unchanged.

## 4. Crash delta and iOS health

- New since the install/start window: `ExcUserFault_macwsinputd
  2026-10-19-224215` ×2 — `EXC_GUARD GUARD_TYPE_USER reason_code=9`,
  `XPC_EXIT_REASON_FAULT` — non-fatal user fault; inputd (22502) stayed
  alive and the input sockets/metrics kept publishing.
- `ExcUserFault_codesign-2026-10-19-204334` — during the D127 postinst
  (codesign sub-tool fault; install completed rc=0 anyway).
- `ExcUserFault_Music-224205` — unrelated iOS app fault.
- `backboardd.wakeups_resource-204534` — resource-wakeups report, not a
  crash.
- `routined-181107` — predates the session.
- Thermal: nominal, raw=0, low-power=no throughout; watchdog observe-only
  alive; no WindowServer/Dock/Finder/Terminal crashes.

## 5. Cleanup / leftovers

- Removed `/var/mobile/rfb_dev.py` + `/var/mobile/d130_frame.raw`
  (my pixel-capture script/artifact).
- No diagnostic or sender-observation sentinels created; all four marker
  paths verified absent after testing.
- No probes/debuggers/lldb/debugserver running.
- The restored `/var/mnt/rootfs/var/jb/usr -> /var/jb/usr` symlink
  preserved (verified working again post-reboot).
- GUI stack left running — `phase=ready`, all clients live. This is the
  verified-working state; nothing owned by the test was left attached.

## 6. Candidate status

**PENDING** — layers 1, 2 (partial-positive), 4 and 6 have direct
evidence. Layer 3 requires a sentinel-armed cold start (out of scope).
Layer 5 lacks byte-level pixel inspection in `--no-vnc` mode (the pixel
bridge is intentionally off). The composite replay channel's
`source=unavailable` while `validated-final-composite` advances is an
open channel-health anomaly worth its own task.
