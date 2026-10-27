# Devin D033 — read-only device regression inventory (2026-10-18)

Worker: Devin CLI backend D033 (replaces qoder backend). Task:
`coordination/_relay/devin-033-backend-task.md`. Scope: read-only inventory.
No service start/stop/restart, no deploy/build/commit, no marker or
trustcache mutation, no device file writes. All SSH commands used
`sshpass -e` with a transient password env; no secret was written to disk,
printed, or passed as a process argument.

Local times below are Asia/Hong_Kong (+08:00). Device time observed at
22:43–22:49 HKT.

## 1. Hardware / build / uptime

- iPad13,11 (T8103), iPadOS 16.3 (20D47), XNU `8792.82.2~1/RELEASE_ARM64_T8103`.
- `uptime`: 2 days 9:56 (kernel boot ≈ Oct 16 12:47); load avg 1.75.
- Thermal: `thermal-state=nominal raw=0`, virtual-temp 33.2 °C at 22:43.
- (`/usr/sbin/sysctl` is absent on this device; `uname`/`ps`/`launchctl`
  sufficed.)

## 2. Native iOS health

| Process | PID | lstart | note |
|---|---|---|---|
| SpringBoard | 872 | Oct 18 12:26:48 | alive |
| backboardd | 905 | Oct 18 12:26:48 | alive |
| native tccd | 883 | Oct 18 12:26:48 | alive (`/System/.../tccd`) |
| locationd | 911 → 93749 | 12:26:48 → respawned 22:28:04 | see §5 |
| lockdownd | 922 | 12:26:48 | alive |

SpringBoard/backboardd/tccd all restarted at 12:26:48 today while kernel
uptime was already >1.5 d — a userspace/respring event at midday, not a
kernel reboot.

## 3. Project state — this is a RUNNING failed generation, not a stopped desktop

A live, externally-owned generation exists and was **not** started by this
round:

- `macos_gui.sh watchdog coexist --no-vnc --no-terminal --experimental`
  pid 92972, lstart 22:26:42, ppid=1 (launchd-spawned, watchdog armed
  `restarts>=12/45s`, observe-only thermal, nominal samples every 300 s).
- WindowServer 93627 `/System/Library/PrivateFrameworks/SkyLight.framework/
  Resources/WindowServer -virtualonly -daemon`, lstart 22:27:54,
  elapsed 21 min, ~1.6 % CPU — alive and stable, not crash-looping.
- `macwsdisplayd` 93724 (UIKitApplication job), `macwsinputd` 93619,
  `macwsinteropd` 93737, `macwslocationd` 93739, `OSXvnc-server` 93880,
  `MacWSHost` 93835 (UIKit app), `macwstccd` 93432
  (`com.macwsguide.tccd-system`), plus ~27 `com.macwsguide.*` launchd jobs.
- Long-lived iOS-side project daemons unchanged since 12:26:48:
  `macwshostd` 897, `macwskeychaind` 904, `macwsallocd` 916.

### Composite/display state — zero frames in this generation

- `/var/mnt/rootfs/private/tmp/macws_final_composite.state` (and the
  `/tmp` twin):
  `state=missing producer=0 sequence=0 updated=1792333683.793304
  reason=displayd-start` — written once at 22:28:03 by displayd startup
  and **never updated since** (displayd writes it at launch; only a real
  producer updates it after that).
- `macws_workspace_graph.state`: `state=missing windowserver=0
  publisher=0 reason=displayd-start`.
- `macwsdisplayd.err` current segment (after last `READY` line, 19378+,
  i.e. this generation): `frame-first`=0, `final-composite-received`=0,
  `accepted`=0, `rejected`=0, `surface=`=0, `workspace-graph-state`=0.
  Only subscribe/unsubscribe/replay-request chatter for windows 31/36
  (Host-side per-window stream requests that find no producer).
- `WindowServer.err` (current gen, 1411 B, complete file reviewed):
  two `#### AGX device-info ABI probe … requested=0x78
  result=0xe00002c2 fallback=0x70 result=0 selected=legacy-0x70`
  lines (the known kIOReturnNoBandwidth → legacy fallback), and **zero**
  `FINAL-COMPOSITE`, `COEXIST API SwapEnd hook installed`,
  `CANCEL-COMPLETION`, or `graphics ready` lines.
- `macwsinputd.err` is 6.9 MB and growing — all
  `MACWS-INPUT POLL-TIMEOUT fd=N revents=0 …` (inputd is alive, polling an
  input record file whose mtime advances ~60 s with no new content).
- `workspace-controller.log`: `wallpaper-ready screens=1
  path=/usr/local/share/macws/wallpapers/macws-forest-lake.png` — the
  wallpaper payload resolved; the compositor simply never delivers it.
- `dock.log`: Dock 93862 alive ("XType: Using static font registry",
  "Couldn't connect to the usernoted").

**Distinction asked for by the task:** this is a *running failed
generation* — the whole stack is up under a watchdog, WS is 21 minutes
old, and the display pipeline has delivered nothing, ever.

## 4. Root-cause evidence: every SkyLight-internal hook is refused in WS

`WindowServer.err` hook line counts (verbatim `grep -c`):

| file | era | `REFUSED` | `SwapEnd hook installed` | `FINAL-COMPOSITE`/`CANCEL-COMPLETION` |
|---|---|---|---|---|
| `WindowServer.err.prev2` | Oct 11 16:16 | 0 | 3+ (install lines) | present |
| `WindowServer.err.bak` | Oct 12 03:37 | **0** | 10 | 654 lines incl. `replay-receiver-ready port=120379`, `replay-received count=1..6`, 5× `QC-DESKTOP-TARGET`, 2× `MACWS-METAL-TARGET`, `SkyLight PrepareForUse/StartComposite/EndCurrentComposite/EndUpdate … hook installed` |
| `WindowServer.err.previous` | Oct 18 19:50 (R013 gen) | 58 | **0** | **0** |
| `WindowServer.err` | Oct 18 22:27 (live gen) | 21 (+2 `MSG-REFUSED bad args`) | **0** | **0** |

Source-level mechanism (`RE-confirmed` against current tree):

- `misc/cydiasubstrate_macos.m:433` `MSHookFunction` is fail-closed:
  `macws_permit_for()` (line 382) requires a registered permit —
  `(uuid,offset)` match plus 16-byte expected-prologue `memcmp` —
  and the only refusal log is `MACWSHook: REFUSED no permit for target`
  (line 459). Refused calls return the callable original (no crash) —
  the hook simply never happens.
- Permits enter `g_permits` only via `MACWSHookPermitV2`. In libmachook
  the **sole** registration call site is the CGXSYNTH diagnostic gate at
  `libmachook/mac_hooks.m:6077`, which is additionally gated by
  `isWS && macws_runtime_diagnostics_enabled()` — the diagnostics marker
  is absent in production (verified: all six markers absent in both
  namespaces), so **zero permits are registered in a production WS**.
- The composite publisher chain is entirely downstream of those refused
  hooks: the IOMFB `publicSwapEnd` target is `MSHookFunction`-installed
  at `libmachook/mac_hooks.m:6299` (the `#### COEXIST API SwapEnd hook
  installed` line); the swap-completion callback calls
  `macws_publish_graphics_ready()` and `macws_publish_scanout_after_swap()`
  (mac_hooks.m:21690–21699); publish requires
  `MarkContentValidated`/`PublishSurface`, which alone can start the
  replay receiver (`MacWSFinalCompositePublisher.m:189/205` →
  `replay-receiver-ready` log at :778). With SwapEnd refused, nothing in
  that chain ever runs — matching the observed absence of every
  `FINAL-COMPOSITE`/`CANCEL-COMPLETION`/`graphics ready` line.
- The same refusal kills the rest of the working-era hook surface
  visible in `.bak`: SkyLight `PrepareForUse`/`StartComposite`/
  `EndCurrentComposite`/`EndUpdate`/`WSCompositeDestinationCreateWithMetalTexture`
  tolerate-nil/completion hooks, `QC-DESKTOP-TARGET` `_MTLLibrary`
  swizzles, `MACWS-METAL-TARGET` `_MTLFunctionInternal` swizzles,
  `STRAY-DRAWABLE`, `XPC_MTL`, `MACWS_HID_BYPASS`, `MACWS_UTTYPE`,
  pre-lock pacing, FSNode repair.
- `MSHookMessageEx` has no permit gate but reported
  `MSG-REFUSED bad args` ×2 in the current WS — failed argument/method
  resolution, separate minor issue (Oct-12 `.bak` had zero `MACWSHook`
  lines at all).

**Provider identity check:** both the currently installed provider and
the preserved R010 baseline contain the permit gate:
`grep -c "no permit for target"` = 2 on both
`/var/mobile/…/r010-baseline/CydiaSubstrate.r010.baseline` (`7320a210…`)
and the installed `/var/mnt/rootfs/…/CydiaSubstrate` (`b2ae21da…`).
So the refusal regime is **not new to today's 19:30 install** — every
post-permit provider has refused all unregistered targets.

Git context (host): `misc/cydiasubstrate_macos.m` history —
`fa76294` (Oct 06, initial 22F82 provider), `4841131` (Oct 14, R002 real
trampoline), `685624f` (Oct 14, R003 permit hardening), `3c4d581` (Oct 18,
method-hook/ABI repair). The Oct-11/12 logs (`SwapEnd hook installed`,
zero REFUSED) imply a then-working MSHookFunction — i.e., the regression
boundary for *hook-driven* paths is the era where the project provider
replaced the working one, not any single recent commit. **Exact swap
date is THEORY** — no install log retained pins it.

## 5. Crash inventory (CrashReporter, today = 2026-10-18)

`ls -lt /var/mobile/Library/Logs/CrashReporter` + `.ips` JSON headers
parsed on-device. Counts = files named `^<name>-2026-10-18` (crash) or
`ExcUserFault_<name>-…` (userfault):

| process | real crashes | ExcUserFault | attribution |
|---|---|---|---|
| MTLCompilerService | **25** | 0 | iOS-native binary — crash `slice_uuid 6d2cfe56-8d88-…` equals `/System/…/MTLCompilerService.xpc` on-disk UUID exactly; chroot binary lives at `Versions/A/XPCServices/…`. SIGABRT at `MTLCompilerObject::readModuleFromBinaryRequest` → `expectedToErrorCodeAndEmitErrors` → `fatalDiagnosticHandler` → abort: LLVM fails to *decode* the module (the known F11H read failure). Cluster 22:30:16–22:31:29 in this generation, then stopped. Which client submitted the module is THEORY (iconservicesagent's MPS path is a documented sender). |
| iconservicesagent | **17** (+ still looping) | 25 | chroot binary — crash uuid `c0891064-…` matches the chroot file's arm64e slice (fat `[6a93e10f, c0891064]`); iOS file uuid `eb76fa5e` differs. Loaded image list includes `/usr/local/lib/libmachook.dylib` (`c439dbec`). SIGABRT via `__assert_rtn` ← `MTLReportFailure` ← `MPSLibrary::MPSKey_Compile` (R013 signature). **Active crash loop right now**: latest report 22:49:00, process 97432 spawned 22:48:57, ~40–90 s/cycle. |
| UIKitSystem | 3 | 25 | chroot — uuid `1e97e27c` matches chroot `UIKitSystem.app` arm64e slice; loads libmachook `c439dbec`. `EXC_BREAKPOINT`/`Trace/BPT trap: 5`, dies ~0.6 s after launch (22:30:50 sample). Unsymbolicated frames. |
| macwsdisplayd | 0 | 22 | lightweight `EXC_GUARD/GUARD_TYPE_USER namespc 7 reason 9` class only (R022/R023 classification) — daemon itself survived (pid 93724). |
| macwsinputd | 0 | 8 | same ExcUserFault class; process alive (93619). |
| macwsinteropd | 0 | 12 | same; alive (93737). |
| CoreLocationAgent | 0 | 22 | same burst 22:28. |
| WindowServer | 0 | 0 | no crash; stable 21 min. |
| ReportCrash | 24 | — | ReportCrash itself faulting while generating reports for the crash cluster. |
| routined | **25** | — | iOS-native location routines daemon; two newest 22:41:41 + 12:29:38. **Unattributed** — plausible correlation with `com.macwsguide.geod`/`macos-locationd`/`locationbridge` activity (locationd respawned 22:28:04), but no causal line captured. Flagged for the next round. |

The 22:28–22:33 window noted by the qoder final handover = this
generation's startup ExcUserFault burst + the MTLCompilerService /
UIKitSystem / iconservicesagent abort cluster. Everything observed is
consistent with a fresh generation trying to start, not random decay of
a running system.

## 6. Installed identities (verified now vs recorded)

| artifact | device state | recorded (R012/R013) | match |
|---|---|---|---|
| package | `com.kdt.macosbooter 0.3.4` install-ok | 0.3.4 | ✅ |
| provider | `/var/mnt/rootfs/…/CydiaSubstrate` inode 250671913, SHA-256 `b2ae21da…e1423e7`, mtime Oct 18 19:30:39 | same | ✅ |
| libmachook arm64e | `…/usr/local/lib/libmachook.dylib` SHA `47f78880…d9432`, inode 250671919, mtime 19:30:41 | same | ✅ |
| libmachook arm64 | `…/libmachook_arm64.dylib` SHA `6de9b688…cbd18`, inode 250671920 | same | ✅ |
| `macos_gui.sh` | mtime Oct 18 18:57:14, 294789 B | (edited during today's rounds) | recorded |
| `macwsthermal` | mtime Oct 18 19:21:44 | — | recorded |
| R010 baseline backup | `/var/mobile/macws_r012_evidence_20261018/r010-baseline/` all five files present: provider `7320a210…7337`, libmachook `4fa53411…9322` (iOS+rootfs copies), arm64 `d2f110fd…d4cd` (iOS+rootfs copies) | matches recorded values | ✅ |

Markers: all six `/tmp` + chroot-namespace diagnostics markers absent.
R031 analysis staging on device (`/var/mobile/macws_r031_evidence_20261018/`)
is inert files only; `MacWS/r014` endpoint tree absent; no owned project
process from qoder R031 remains.

## 7. Last known rendered baseline (not just "the R010 backup")

1. **Canonical documented baseline:** 2026-10-13 —
   `docs/evidence/ventura-22F82-input-transport-and-composite-20261013.md`:
   `state=ready`, continuous `final-composite-received sequence=278+`,
   wallpaper rendered, WS 87761 `-virtualonly -daemon`, displayd 88080.
2. **Log-derived:** the surviving `macwsdisplayd.err` (continuous since
   Oct 8) last records `state=ready … wallpaper=ready dock=ready
   menubar=ready backing=ready` for WS pid 59329, inside displayd pid
   75434's lifetime, bounded by its exit marker at Oct 14 16:20:26
   (log line ~17912). Later generations show only `state=recovering`
   plus sparse `frame-first` window=0 layer surfaces (Dock/"Window Server"
   owner overlays) through a generation ending Oct 18 01:18:35 — partial
   per-window surface delivery without a final composite.
3. **WS-side positive control:** `WindowServer.err.bak` (Oct 12 03:37)
   shows the full working hook surface (§4) including `FINAL-COMPOSITE
   replay-receiver-ready` — the exact machinery today's logs lack.
4. Since the Oct-18 19:30 R012 package install: three generations
   (19:33 R012, 19:50 R013, 22:27 live) — **zero frames each**.

So the visible-desktop regression boundary is *not* a single recent
commit: `state=ready` was last seen ≤ Oct 14; sparse partial frames
persisted until ~Oct 18 01:18; zero frames since the 19:30 install.
The refused-hook regime (§4) covers at minimum the post-permit-provider
era; whether the R010-baseline provider era could still deliver a
composite is unproven either way (permit gate present in that binary too).

## 8. Comparison: deployment vs historical visible baseline

| | working (Oct 12 `.bak`) | today (live gen) |
|---|---|---|
| WS SwapEnd hook | installed ×10 | absent (refused) |
| SkyLight composite hooks | installed | absent (refused) |
| QC/MTL swizzles | installed | absent |
| `replay-receiver-ready` | present | absent |
| `CANCEL-COMPLETION delivered` | ×654 class events | 0 |
| displayd composite state | ready | `missing` since displayd-start |
| frames reaching displayd | seq 278+ (10-13 doc) | 0 |
| MTLCompilerService | (no crash lines in `.bak` sample) | 25 SIGABRT today |
| iconservicesagent | — | chroot MPS abort loop, live |
| AGX probe | `0x78→0xe00002c2 → legacy-0x70` | identical (unchanged) |

## 9. Candidate reversible next actions (for the next round; none executed)

Priority order for "restore visible desktop first":

1. **Register exact permits for the load-bearing WS targets** — the
   provider contract already supports it (`MACWSHookPermitV2`: uuid +
   image offset + 16-byte prologue pin). Minimum set: IOMFB
   `publicSwapEnd` (ABI table `kMacWSIOMFBABIs` already pins offset per
   build) plus the SkyLight composite chain sites that fed the working
   `.bak` build. One-variable test: add the SwapEnd permit alone and
   look for `SwapEnd hook installed` + `replay-receiver-ready` in a
   bounded owned generation. This preserves the fail-closed contract —
   it authorizes the exact intended targets instead of bypassing the
   gate.
2. **A/B against the R010 baseline** (hashes in §6): restoring provider
   `7320a210…` would NOT lift the refusal regime (permit strings present
   in it) — it is therefore *not* a candidate "known-good" for this
   defect, only for regressions elsewhere. The real known-good provider
   is whatever produced the Oct-12 log; its binary is not among the
   preserved backups.
3. **Do not conflate with the F11H/MPS decode failure** — the chroot
   MPS/MTLC aborts are real but a separate defect class (module decode),
   likely the iconservicesagent path; the display path is dead even
   without them because the publish chain never arms. Fixing the hook
   refusal is the bounded first move; then re-observe whether Metal
   decode becomes the next blocker (WS's own compile requests would hit
   the same iOS MTLC decode wall — THEORY until a compile request is
   captured from WS specifically).
4. Leave the currently running external generation untouched
   (coordinator rule); plan generation changes through the owner.

## 10. Limits of this inventory

- Read-only: no `launchctl print`, no lldb, no `log show`, no process
  maps of the live WS were taken (bounded reads only).
- `REFUSED` lines don't name targets — the specific 21 refusals in WS
  are counted, not individually attributed; the SwapEnd-site refusal is
  proven by the *absence* of its install log plus the source contract,
  not by a named-target line.
- Exact date the working provider was replaced: THEORY bounded to
  post-Oct-12 (working logs) and pre-R001-discovery (Oct 14, scaffold
  found deployed).
- The midday 12:26:48 respring/userspace restart is recorded as a fact
  of the timeline; no causal link asserted.
- routined×25 crashes are reported without attribution.

(handover to codex now)
