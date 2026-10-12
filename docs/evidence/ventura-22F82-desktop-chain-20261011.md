# Ventura 22F82 desktop chain — evidence and fixes (2026-10-08..11)

Runtime-verified state of the GUI bring-up on iPad13,11 / iPadOS 16.3 /
Dopamine rootless. Every claim here has a log line, crash report, or disasm
behind it; the witness files are named inline.

## What works now (all runtime-confirmed)

- `chroot /var/mnt/rootfs /bin/echo HI` → `HI`, rc 0 (after trust restore)
- WindowServer runs persistently and publishes `com.apple.windowserver`,
  `windowserver.active`, `CARenderServer`, `VirtualDisplay`,
  `macwsguide.windowserver.final-composite-replay` Mach services
- `launchservicesd` (stock Ventura binary, project-signed) checked in and
  serving; `lsregister -kill -seed` completes; csstore readable via
  `lsregister -dump` (~5.7 MB)
- session + system `lsd` running after `var/folders/zz` dirs were created
- private `macws-cfprefsd` running and answering real requests
- **Dock survives `_RegisterApplication` and receives a real ASN**
  (`C96172A0-CE74-4D62-AFEF-ECFE4D04D827`, asn.lo=4097) and registers its
  AppInput endpoint `/private/tmp/macws_app_input.<pid>.sock`
- SystemUIServer likewise survives past `_RegisterApplication`
- displayd: one IOSurface frame arrived (`frame-first stream=118
  surface=2732x2048`), recognized 1366x1024@2x AppKit display
- `diskarbitrationd` (Ventura, project-signed) alive under launchd;
  private endpoint `com.apple.macosbooter.DiskArbitration.diskarbitrationd`
  checked in `active=1` — root cause of the persistent `OS_REASON_EXEC`
  137s was the `com.apple.rootless.datavault.metadata` entitlement; see
  dedicated section below

## Root cause chain for the `Connection invalid` era (FIXED)

Symptoms: every XPC `send_message_with_reply_sync` to mapped
`com.apple.macosbooter.*` services returned `Connection invalid`; Dock
aborted inside `HIServices _RegisterApplication` printing
`unable to get application ASN ... error=#6`-style messages.

Investigation steps that mattered:

1. `bootstrap_look_up` on the mapped name succeeds — `bootstrap_port=0x807
   kr=0 port=0x1b03` (MACWS_BOOTSTRAP_PROBE in `mac_hooks.m`). Name
   resolution is NOT the failure.
2. `xpc_connection_create_mach_service` early interpose logs flags:
   **Dock passes `flags=0x2` = `XPC_CONNECTION_MACH_SERVICE_PRIVILEGED`**.
3. Controlled experiment (`misc/lsxpc_probe.m`, run via launchd job with
   the hook inserted): identical process, identical service —
   `flags=0` → real handshake reply; `flags=0x2` → `Connection invalid`.
   Runtime-confirmed on device.

Fix (in `libmachook/mac_hooks.m`, `macws_xpc_connection_create_mach_service_early`):

```c
if (name != originalName) {
    flags &= ~(uint64_t)0x2;   /* strip PRIVILEGED only on our mapped names */
}
```

Ventura's privileged-service lookup semantics do not map onto the iOS
launchd domain; the flag is meaningless for our own daemons. **Scope it to
mapped names only** — stripping it globally would break real privileged
lookups.

Diagnostic env vars used (all in the hook, env-gated):
`MACWS_XPC_NAME_TRACE`, `MACWS_BOOTSTRAP_PROBE`, `MACWS_LSXPC_TRACE`
(dumps full XPC request/reply dictionaries via interposed
`xpc_connection_send_message_with_reply_sync`), `MACWS_CONN_INSPECT`,
`MACWS_AGX_CRASH_DIAG` (dladdr-symbolized SIGABRT dump),
`MACWS_AUDIT_TRACE`.

## Deployment lessons (all hard-won)

### launchservicesd must be the stock fat binary

`misc/exec_to_dylib.py` produces an arm64 thin loader + arm64e payload
dylib — an arm64 process cannot dlopen an arm64e image, so the loader
always failed. The 13.4-validated deployment is: **stock fat
`launchservicesd` + `ldid -S<ent> -M` (twice) + register every slice
cdhash**. Source-of-truth commit `d4160fc` era `misc/postinst.sh` only
trusted the stock binary, never replaced it.

### Project signing recipe (for any new binary placed in rootfs)

```sh
ldid -S/var/jb/usr/macOS/bin/entitlements.plist -M <file>
ldid -S/var/jb/usr/macOS/bin/entitlements.plist -M <file>   # twice
misc/macws_boot_trust.py <file>    # register every slice cdhash
```

Stock-Apple-signed binaries are killed `OS_REASON_EXEC` (SIGKILL 137 at
exec) under CT policy 0x8. `SIGTRAP/133-134` is a different layer
(dyld/jit after exec succeeds).

### `com.apple.rootless.datavault.metadata` kills exec admission (diskarbitrationd fix)

Symptom: `diskarbitrationd` exited instantly with `last exit reason =
OS_REASON_EXEC` / rc 137 under every spawn context (launchd job,
`launchdchrootexec`, plain `chroot`, with and without `libmachook`,
with `_MSSafeMode=1`). `DYLD_PRINT_LIBRARIES`/`DYLD_PRINT_INITIALIZERS`
produced **zero output from the target** — everything previously
attributed to it (91 loaded images, 16 initializers) was actually the
*parent* `launchdchrootexec`'s own dyld run (same `dyld[pid]` prefix).
`fs_usage` showed `posix_spawn` → `exit` ~12 µs later: kernel exec
admission kill before dyld. This is the documented "137 wall" shape from
`ventura-22F82-bringup-state.md` — never trust dyld logs without
checking which pid emitted them.

Isolated via re-sign experiments on device (each step re-registered the
new cdhash via `macws_boot_trust.py`):

- `Identifier=com.apple.diskarbitrationd` + pure project ENT → **runs**
  (identifier is innocent)
- full merged ENT+native minus `xpc.launchd.ios-system-session` → still
  137
- signature containing **only** `com.apple.rootless.datavault.metadata`
  → **137** — this single key is the exec-fatal trigger on iPadOS 16.3
- merged ENT + all 6 other natives (LiveFS.connection,
  allow-external-storage, security.disk-device-access,
  security.storage-exempt.heritable, vfs.revoke-mounted-device,
  xpc.launchd.ios-system-session=false) → **runs**

Fix shipped: `diskarbitrationd-native-entitlements.plist` no longer
carries the datavault key, and
`sign_and_trustcache_merging_native_entitlements` gained a 6th
`strip_keys` parameter that (a) invalidates the already-signed fast path
when a stripped key is present in the current signature and (b) re-seals
the merged signature with a full `-S` replace after stripping — `ldid
-M` preserves stock keys, so merging alone can never remove datavault.
Verified end-state: pid alive under launchd, endpoint
`com.apple.macosbooter.DiskArbitration.diskarbitrationd` **active=1**
(check-in via the libmachook name remap succeeded).

Gotcha discovered in passing: the job's jetsam memory limits are
6 MB soft/hard while the daemon RSS is ~24 MB — survived so far, but a
jetsam kill under memory pressure is a plausible future failure mode.

### Required directory preconditions

- `$ROOTFS/var/folders/zz/zyxvpxvq6csfxvn_n0000000000000/0/` — lsd csstore
  parent; missing → `systemContentDatabaseStoreFileURLWithUID` nil-arg
  crash
- `/var/mnt/rootfs/private/var/.TemporaryItems` mode `01311`,
  `folders.0` `0700`, `folders.0/TemporaryItems` `0700` — cfprefsd
- `/usr/local/libexec/macws-cfprefsd` — private signed copy of
  `/usr/sbin/cfprefsd` (postinst recipe; the stock path collides with
  iOS cfprefsd name)

### Metal compiler service routing

`xpc_add_bundle` can only remap name→path for names launchd already
knows — it cannot register new service names. Same-name registration
races with Ventura Metal's own dlopen-time registration; winner is
nondeterministic (runtime-confirmed: a ping landed on the stock iOS
service pid). Fix: **`xpc_add_bundle` interpose that rewrites any
registration for `com.apple.MTLCompilerService` to the flat adapter
bundle path**, making the result order-independent.

### macabi bitcode must be produced by on-device llvm-16

`Target OS is incompatible` = spec-record field `+0x24` must be `7`
(LLVM `Triple::IOS`); macOS serialization writes `11`. The verified fix
is `metal2metal.py translate` on QC/SkyLight/MPSImage to
`air64-apple-ios19.0.0-macabi` companions + route manifest. **Local
llvm@22 round-trip output is unreadable by the service's
`fatalDiagnosticHandler`** — use device-side Procursus llvm-16
(`macws-llvm-dis`/`macws-llvm-as`). `ensure_metal2metal_compat.sh` does
this inside `macos_gui.sh start`.

### Trustcache is volatile — restore is mandatory

Every Mach-O executable AND dlopen'd plug-in loses CS admission across
jailbreak refresh. `macos_gui.sh start` runs `restore_cold_boot_trust`
(~83 s, `BOOT-TRUST {"added": 3728 ...}`). Thermal `serious` gates it
(`macwsthermal` reports `thermal-state`); nominal passes.

## Display hazard — read before iterating

Rebooting a WindowServer that is **going to** crash on the real display
path (`IOMFBDisplay::current_page_surface → create_pipeline_state`)
leaves the framebuffer owned by a dead process → the whole iPad screen
looks frozen. The 2026-10-08 "hang" was exactly this — ResetCounter
report showed `Boot faults: btn_rst,btn_seq_reset` (user's button
restart), NOT a kernel panic. Discipline: one bounded WS run, check
display-stack health (backboardd alive), then iterate.

## Open items (current frontier)

1. `final-composite.state=missing` — displayd's
   `final-composite-replay-request attempt=N service=<port>
   send=268435460` cycles unanswered. WS alive but never produces the
   completed composite. Next: identify the service port owner and why no
   reply; correlate with the earlier `EXC_GUARD reason_code 0x9`
   (LIBXPC) signatures.
2. `fullscreen-target-candidates catalog=[] layers=[]` — WS window
   catalog empty; no app window has been published yet.
3. Earlier `terminal 在发布 AppKit 窗口前被信号 6 终止` may be stale
   hostd `last_error` — needs a fresh launch test to confirm/deny.
4. `filecoordinationd` crash-loops (unrelated noise, not blocking).

## Device state snapshot (2026-10-11)

- iPad13,11 iPadOS 16.3 (20D47), Dopamine rootless, SSH `root@<LAN>:2222`
- Dev host moved to x86 macOS 13.7 — same major as the rootfs target;
  `~/theos` + `iPhoneOS16.5.sdk` builds fine, `ldid` = Procursus
  v2.1.5-procursus7 x86_64, `devssh`/`devscp` expect wrappers replace
  sshpass (hangs in non-TTY contexts)
- Production preflight deny-list: `macws_diagnostic_flags.sh` pattern;
  leftover `MACWS_XPC_NAME_TRACE`/`MACWS_AUDIT_TRACE` in job plists
  blocks `macos_gui.sh start` — remove before starting

## filecoordinationd check-in fix (2026-10-11, later)

Root cause of the Dock main-thread wedge (runtime-confirmed, not theory):

- Dock's main thread blocked forever in a synchronous XPC reply wait to
  `com.apple.ProgressReporting` → ProgressReportingProxy → upstream
  `com.apple.macosbooter.ProgressReporting`. `launchctl print` showed all
  three `com.macwsguide.filecoordination` endpoints with `active = 0`
  while `coreservicesd`'s endpoint had `active = 1` — i.e. launchd held
  the ports and filecoordinationd had never claimed them, so every relayed
  request was swallowed unanswered.

- Why: `macws_private_bootstrap_service_name` had no mapping for the
  FileCoordination/ProgressReporting names. Clients were steered to the
  proxy bundles by the earlier `xpc_connection_create_mach_service`
  intercept, but the daemon's own registration used the STOCK names:
  `bootstrap_check_in("com.apple.FileCoordination.kernel.ipc")`
  (RE-confirmed at filecoordinationd+0x2cf0; the FileCoordination and
  ProgressReporting listeners check in through the same name path).
  On iPadOS launchd those public names are not declared by the job, so
  every check-in was rejected → `macosbooter.*` endpoints stayed
  launchd-held → proxy upstream conn never answered.

- Fix: three entries added to the rename table
  (`com.apple.FileCoordination`, `com.apple.ProgressReporting`,
  `com.apple.FileCoordination.kernel.ipc` → `macosbooter.*`).

Verified after deploy + restart (all runtime witnesses):

- `com.apple.macosbooter.FileCoordination` / `.ProgressReporting` now
  `active = 1` (kernel.ipc stays 0 — on macOS it is `HostSpecialPort:30`
  bound by launchd, not check-in; no client blocked on it).
- progress-bridge.log: new proxy accepted client with NO
  `upstream returned non-dictionary reply`.
- Dock log: `LSXPC-CONN com.apple.ProgressReporting` request len=410
  answered by reply len=202 with a real 32-byte plist payload — the full
  Dock→proxy→filecoordinationd→reply chain completes.
- Dock's input thread returned to socket read (was stuck in
  dispatch_sync(main)); Dock continues doing iconservices XPC work.
  The `chroot /var/mnt/rootfs /bin/echo HI` acceptance still passes.

Remaining pointer item: `CGPostMouseEvent` all-false-button posts still
return 1002 — the down post of a tap succeeds (0), the up post fails
(1002). Warp (CGDisplayMoveCursorToPoint) returns 0 and moves the real
cursor; whether hover highlighting now works through WS hit-testing of
the warped cursor needs an on-screen look.

## 2026-10-13 — Fullscreen pointer routing fix (input end-to-end proven)

Symptom resolved: visible desktop (menu bar + Dock + Terminal), scroll
worked, but every tap appeared dead. Runtime capture showed real taps
reaching inputd with valid frame coords, `post=issued`, yet no app
responded.

Chain proven by log witnesses, then fixed and re-verified with synthetic
`MacWSInputRecord` injections (kind=6, flags=0x40, targetPID=Dock,
DIAG contact):

- `macwsinputd` fullscreen block resolved the SLS route correctly
  (`GLOBAL-POINTER ... route=app-input pid=68656 window=26` for a
  Terminal point), but the next else-if in the same target-selection
  chain (`record.targetPID > 1` → live Dock endpoint) overwrote
  `eventTarget.pid` back to the fullscreen producer (Dock 68625),
  keeping the SLS-resolved Terminal window number. The record then went
  to Dock's socket stamped `{Dock, window=26}`.
- Dock endpoint consumed it: `DOCK-SYSTEM ... route=system-mouse
  posted=NO` (22F82: every in-process CGS pointer post completes
  API-side with no visible WS effect) → `DOCK-RX posted=YES` enqueue
  fallback → `DOCK-NATIVE type=1/2` on Dock's own windows — dropped as
  out-of-strip content for app-window points. Taps that did land inside
  Dock's strip DID work (verified: Freeform icon tap produced
  `LS-APP-REQUEST /System/Applications/Freeform.app`, status -10810
  already-running fallback launched YES).
- Terminal's own bridge meanwhile received 999 records — all kind=14
  scroll only; zero tap/hover ever arrived.

Fix (macwsinputd/main.c): the `fullscreenGlobalPointerRecord` arm of the
target chain now short-circuits to a no-op when the endpoint block
already accepted the live SLS route (`endpointRouted`), instead of
falling through to the producer-PID rewrite. The Dock-as-poster
fallback is preserved for SLS misses / dead endpoints.

Post-fix injection witnesses:

- Terminal window point: `ROUTE route=appkit-socket pid=68656
  window=26` → Terminal `RX kind=6` → `HIT local=(304.89,371.51)` →
  `MOUSE-EVENT type=5/1/2 serial=1..3` + `MOUSE-RETURN` — a real
  down/move/up pair dispatched through sendEvent.
- Desktop center: routed Finder 68554 window 15 →
  `HIT view=NSKVONotifying_TDesktopView` → real desktop click.
- Dock strip: routed Dock 68625 window 25 → `DOCK-NATIVE` → Freeform
  LS launch (reproducible).
- Menu bar point: routed Finder window 12 → `DROP
  reason=target-window-closed` — menu-bar activation needs the existing
  ActivateTarget/system-menu preflight path, not a window dispatch.
  Same outcome as before the fix (Dock poster also discarded it);
  tracked separately, not a regression.

Deployed: macwsinputd arm64 (Theos macosx target), CDHash
49f830b17f42dd0c4a0d83b6a7f9d8a91f6c03b6 registered via jbctl;
both /var/jb/usr/macOS/bin and /var/mnt/rootfs/usr/local/bin copies
updated atomically; job reloaded via procursus launchctl.

Remaining to verify with a physical finger: UIKit-sourced taps now take
the identical path (earlier real records had the same shape: scene
0x80000000, flags 0x40, targetPID=Dock). Hover records now also route
to the SLS owner instead of Dock, which should restore Dock
magnification/highlight behavior.
