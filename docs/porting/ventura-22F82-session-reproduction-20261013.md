# Reproducing the 2026-10-13 Ventura 22F82 desktop state

This is the "new agent starts cold" guide: everything needed to rebuild,
deploy, launch and verify the current state — `chroot CLI works`,
`WindowServer compositing`, `state=ready`, `wallpaper rendered`, `input
mapped but dead at the WS event subsystem` — without re-deriving the
five days of archaeology.

Read in this order before touching the device:

1. `AGENTS.md` — patch/evidence discipline (authoritative, load-bearing)
2. `docs/evidence/ventura-22F82-desktop-chain-20261011.md` — LS/XPC chain
   + trust/deployment recipes + display-hazard discipline
3. `docs/evidence/ventura-22F82-input-transport-and-composite-20261013.md`
   — composite fix, wallpaper fix, inode landmine, input dead-end map
4. THIS file — the operational runbook

---

## 1. Topology and identities

```text
Host (dev box)                iPad13,11 (M1)
macOS 13.7 x86, Xcode SDK     iPadOS 16.3 (20D47), Dopamine rootless
~/theos + iPhoneOS16.5.sdk    jailbroken, Procursus userland
        │                            │
        └── ssh root@<LAN-IP>:2222 ──┘
             (password auth; user-local expect wrappers exist on the
              dev host as `devssh`/`devscp` — do NOT commit credentials;
              the device LAN IP drifts across sessions, verify with
              `ping`/router rather than trusting a hardcoded one)
```

Key device paths (all absolute from the iOS root):

```text
/var/mnt/rootfs                      macOS Ventura 13.4 rootfs (bind-mounted)
/var/jb                              Dopamine rootless prefix
/var/jb/usr/bin/chroot               the chroot binary
/var/jb/usr/macOS                    project payload: bin/, lib/, LaunchDaemons/
/var/jb/usr/macOS/bin/macos_gui.sh   THE start/stop/status entry point
/var/jb/usr/macOS/bin/set_macos_version.py   iOS→macOS platform marker rewriter
/var/jb/usr/macOS/bin/macws_boot_trust.py    dynamic trustcache registrar
/var/jb/var/mobile/                  LOGDIR — every daemon's stdout/stderr
/var/mnt/rootfs/private/tmp/         chroot-visible socket dir
  macws_host_input.sock              input record socket (macwsinputd)
  macws_vnc_pointer_proxy.sock       OSXvnc native pointer proxy
  macws_app_input.<pid>.sock         per-app AppInputBridge endpoints
  macws_workspace_graph.state        displayd's layer-catalog dump
  macws_runtime_diagnostics          gate file: verbose #### logging
  macws_ws_dispatch_witness          gate file: WS-dispatch witness
```

Chroot invocation shape (from iOS side):

```sh
/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI        # must print HI
/var/jb/usr/bin/chroot /var/mnt/rootfs /usr/bin/true       # must rc 0
```

## 2. Daemon inventory (what should be running when healthy)

```text
WindowServer        SkyLight.framework/Resources/WindowServer -virtualonly -daemon
                    launchd label UIKitApplication:com.macwsguide.windowserver
macwsdisplayd       /usr/local/bin/macwsdisplayd (chrooted)
macwsinputd         /usr/local/bin/macwsinputd   (chrooted)
Dock                /System/Library/CoreServices/Dock.app/.../Dock
Terminal            /System/Applications/Utilities/Terminal.app/...
OSXvnc-server       /usr/local/bin/OSXvnc-server -rfbnoauth -swapbuttons :0
launchservicesd     /var/jb/usr/macOS/bin/launchservicesd (project-signed)
macws-cfprefsd      /usr/local/libexec/macws-cfprefsd (private copy)
watchdog            iOS-native launchd job (mandatory, crash-loop guard)
autosignd           signature service (see §7 trust)
pboard, pbs         pasteboard + services (menu completeness)
SystemUIServer      menu-extras
macwsworkspacectl, macwsinteropd, macwsaudiooutd ...
```

Expected health signals and where they live (LOGDIR=`/var/jb/var/mobile`):

| signal | file | healthy line |
|---|---|---|
| WS render loop | `WindowServer.out/.err` | `IOMFB GPU-FENCE unavailable ... no-work cancellation` (that's normal here), composite publish counters advancing |
| displayd graph | `macwsdisplayd.out` | `workspace-graph-state state=ready windowserver=<pid>` |
| input broker | `macwsdisplayd.err`/inputd log | `MACWS-INPUT` accept/reject lines |
| OSXvnc | `osxvnc.log` | `NATIVE-ALL event=...` on RFB input, `POINTER-PROXY ready` |
| Dock | `dock.log` | `LS-APP-ROUTE owner=Dock` on icon taps |
| Terminal | `terminal.log` | `APP-INPUT ...` bridge lines; `WS-DISPATCH` if/when WS dispatch revives |
| watchdog | `macos_gui_watchdog.log` | restart counters, thermal snapshots |
| graph dump | `/var/mnt/rootfs/private/tmp/macws_workspace_graph.state` | machine-readable layer catalog |

## 3. Build

Dev-host Theos build (the whole project):

```sh
cd macPad
make FINALPACKAGE=1 THEOS_PACKAGE_SCHEME=rootless \
    THEOS=/path/to/theos        # produces .theos + packages/
```

Iteration on libmachook only (the hot loop during bring-up):

```sh
make -C libmachook                 # or repo-root make; see libmachook/Makefile
# produces .theos/obj/... libmachook.dylib FAT (arm64+arm64e)
```

**Slice discipline** (`libmachook/Makefile` comment, load-bearing):
device dyld rejects a *fat* DYLD_INSERT_LIBRARIES dylib in this chroot —
launchdchrootexec inserts exactly ONE thin slice per target. The fat must
be split:

```sh
lipo -thin arm64e fat.dylib -output libmachook.dylib         # arm64e
lipo -thin arm64  fat.dylib -output libmachook_arm64.dylib   # arm64
# both get staged; consumers pick by subtype
```

The on-device path mirrors this in `misc/build_on_ios.sh` (which also
contains the fresh-inode comment — see §5).

## 4. Deploy — the ONLY safe recipe

For ANY signed file that gets mapped by other processes (libmachook,
OSXvnc, probes, every chroot binary), this sequence is mandatory. Getting
any step wrong produced a confirmed failure class this week:

```sh
NEW=/path/to/built-file
DST=/var/mnt/rootfs/usr/local/lib/libmachook_arm64.dylib   # example

python3 /var/jb/usr/macOS/bin/set_macos_version.py "$NEW"  # platform marker
ldid -S<entitlements.plist> "$NEW"                          # sign
ldid -S<entitlements.plist> "$NEW"                          # AGAIN — ldid/lipo
                                                          # invalid-page quirk
install -m755 "$NEW" "$DST.new" && mv "$DST.new" "$DST"    # FRESH INODE
python3 /var/jb/usr/macOS/bin/macws_boot_trust.py --readd "$DST"
# then restart every process that maps the old copy
```

Why each step exists (all runtime-confirmed):

- `set_macos_version.py` — iOS-platform Mach-Os are rejected by macOS dyld
  policy in the chroot; rewrite LC_BUILD_VERSION platform→macOS.
- `ldid` twice — a single sign on certain lipo'd output leaves one slice's
  signature stale; second pass is cheap insurance. (Observed: probe
  SIGKILLed at 137 → 133 after re-sign.)
- **fresh inode** — `cp`-overwrite keeps the inode; the kernel caches
  signature state per-vnode → every process that maps the file dies
  `CODESIGNING Invalid Page` at first page-in. `install+mv` (or rm+cp)
  gives a new vnode → clean state. This took the whole chroot down once.
- `macws_boot_trust.py --readd` — registers new cdhashes into the dynamic
  trustcache (`BOOT-TRUST {"added": N, "backend": "libjailbreak"}`).
- restart consumers — already-running processes keep the OLD inode mapped;
  they never see the fix.

`macos_gui.sh start` itself runs `restore_cold_boot_trust` (~83s,
`BOOT-TRUST {"added": 3728+}`). Note the corrected semantics (runtime,
10-07): `jbctl trustcache add` entries **survive reboot** on this Dopamine
build — but a jailbreak refresh/re-jailbreak can still lose them, and any
newly signed file needs `--readd` anyway. Treat the restore step as
verification+repair, always, via `macos_gui.sh` rather than manual starts.

## 5. Launch

```sh
# on the iPad, as root:
bash /var/jb/usr/macOS/bin/macos_gui.sh start coexist --no-terminal   # or omit --no-terminal
```

What `start` does in order (abbreviated): stale-transition lock →
`restore_cold_boot_trust` → mount/devfs + compat fixups
(`ensure_quartzcore_compat.sh`, `ensure_metal2metal_compat.sh`,
`ensure_objc_trampolines_arm64.py`) → private cfprefsd/lsd/launchservicesd
→ `diskarbitrationd` → WindowServer launchd job → readiness poll →
`macwsdisplayd`/`macwsinputd`/`macwsinteropd` → OSXvnc + pointer proxy →
Dock → Terminal → navigation-spaces → watchdog arm.

Modes: `coexist` (iPadOS panel + VNC mirror — the normal dev mode),
`exclusive` (macOS owns the panel — display-hazard territory, read the
2026-10-11 doc §"Display hazard" first).

Stop: `bash macos_gui.sh stop` — kills the stack, restores SpringBoard.
Status: `bash macos_gui.sh status`.

The watchdog is mandatory and cannot be disabled; it kills crash-looping
WS generations and trips on `--runtime-cap`. Refusals with `active test
lease` mean a prior test left `/tmp/macws_test_lease` — check/remove.

## 6. Verification ladder (each step has an expected witness)

```text
1. chroot        /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI  →  "HI"
2. processes     ps aux | grep -E 'WindowServer|macwsdisplayd|OSXvnc|Dock'
3. display list  ws_event_probe → "CGGetActiveDisplayList -> 1 displays: 1366x1024"
4. graph         grep 'workspace-graph-state' macwsdisplayd.out → state=ready
5. composite     macwsdisplayd.out → final-composite-received sequence advancing
6. visual        python3 misc/vnc_capture.py <ipad-ip> /tmp/fb.png → PNG of desktop
7. input route   python3 misc/macws_inject_record.py tap <x> <y> →
                 "sent kind=6 ..." + routing lines in bridge logs
8. WS dispatch   touch .../macws_ws_dispatch_witness, inject tap, then
                 grep WS-DISPATCH terminal.log — currently fires ZERO times
                 (the known dead end; see §8)
```

`misc/vnc_capture.py` is the fastest visual witness — raw-protocol RFB
client, no auth, writes a PNG. Its SetPixelFormat request must be the
32-bit BGRX packet; the earlier "connection reset" bug was a malformed
pixel-format message, fixed in-tree.

## 7. Diagnostic knobs (all file-gated, no rebuild needed)

```text
/var/mnt/rootfs/private/tmp/macws_runtime_diagnostics
    → #### verbose logging in libmachook/bridges (the bulk of #### lines)
/var/mnt/rootfs/private/tmp/macws_ws_dispatch_witness
    → AppInputBridge logs "APP-INPUT WS-DISPATCH pid=.. type=.. window=.."
      for every non-fabricated mouse/gesture event reaching sendEvent:.
      THE authoritative "did WS dispatch" witness.
/var/jb/var/mobile/macws_trace_gui_invocations
    → macos_gui.sh logs every transition with stack context
```

## 8. Input debugging toolset (all committed now)

- `misc/macos_ws_event_probe.c` — bootstrap service presence, event-port
  validity, CGS connection, display list, cursor-warp liveness, raw
  `SLSPostEventRecord` attempts. THE probe that proved the HID event
  server is absent. Build/deploy header comment has the exact recipe.
- `misc/macws_inject_record.py` — one-shot 84-byte record injector
  (kind/x/y/window/target/flags CLI). Correct field layout per
  `include/macws_host_protocol.h:446`; earlier injectors that produced
  `REJECT kind=0 point=(0,0)` had the wrong layout.
- `misc/host_input_matrix.py` — full input matrix with InputLab
  cross-validation (heavier, for regression passes).
- `misc/vnc_capture.py` — RFB framebuffer → PNG.
- `misc/cg_window_list.py` — CGWindowList dump inside the chroot for
  layer/surface identity (deploy under `/var/mnt/rootfs` and run chrooted).
- `misc/ls_menubar_owner_probe.c` — LS menu-bar ownership preflight state.

Log-first debugging rule for input: a `sent kind=6 ...` line only proves
the record reached macwsinputd. The chain to watch is:

```text
inputd accept → bridge route (SURFACE-PROXY/FRONT-OWNER) →
post attempt (CONN-POST/legacy/proxy) → WS-DISPATCH (currently never)
```

If REJECT: wrong wire layout. If routed but no WS-DISPATCH: the WS event
subsystem — not the transport — is the blocker (see the 10-13 doc §4).

## 9. Known landmines (all runtime-confirmed this week)

1. **`cp`-overwrite of signed mapped files** → device-wide `Invalid Page`
   SIGKILLs. Always fresh-inode install. (§4)
2. **Fat DYLD_INSERT dylib rejected by chroot dyld** — split thin slices.
   (`libmachook/Makefile` header)
3. **Dynamic HEIC wallpaper decodes black** — device-side fix swapped
   `DefaultDesktop.heic` for a static payload; NOT yet baked into
   `misc/build-rootfs-13.4.1.sh` → a fresh rootfs reinstall regresses it.
4. **`timeout` does not exist on the device** — use background+polling.
5. **`md5` missing** — use `shasum`/`cksum`.
6. **Diagnostic env flags leak into job plists** — leftover
   `MACWS_XPC_NAME_TRACE`/`MACWS_AUDIT_TRACE` in launchd plists blocks
   `macos_gui.sh start` (production preflight deny-list).
7. **ssh stalls on keyboard-interactive** — the device drops to
   password-interactive slowly under load; BatchMode fails. The LAN IP
   also drifts (68.55→68.64 observed); verify reachability first.
8. **Framebuffer-equality is not an input witness** — RFB updates are
   damage-driven; identical PNGs after input prove nothing either way.
   `WS-DISPATCH` or a state transition is the only delivery proof.
9. **Return code ≠ delivery** — `CGPostMouseEvent` returning 0 for down
   and `SLSPostEventRecord` returning 1002 both describe WS receiving a
   message; neither produced dispatch. Treat `rc` as WS's reply, not as
   proof of effect.
10. **`interactionAgeMS` in OSXvnc log is OSXvnc-side timing**, not WS
    acknowledgment — do not cite it as delivery evidence.

## 10. Current frontier (where the next agent starts)

Composite/graph/wallpaper/LS/trust are done. The blocker between "a
desktop you can see" and "a desktop you can use" narrowed further on
10-14/15 (see `coordination/DEVIN_STATUS.md` for the bounded evidence):

- `SLSGetEventPort` is **valid** — `rc=0` with a real receive right;
  the earlier "invalid port" read was an ABI artifact (fixed signature:
  `(cid, mach_port_t *)`).
- Canonical `SLSPostEventRecord` posts DO reach `_XPostEventRecord` and
  receive real replies — the 1002 comes from
  `CGXSenderCanSynthesizeEvents` returning 0, not a dead transport.
- `gCurMsgAudit` is populated on the post path (52-byte sender creds) —
  the "always-NULL audit" theory is disproven.
- The dispatcher decodes audit → `{pid | pidversion<<32}` → CFDictionary
  rights-cache → on miss, a lazily-resolved checker.  The checker name
  and the cache producer are still unknown — that is the open question.
- The hook provider was a no-op scaffold; it is now a real permit-gated
  trampoline verified live (`VERDICT=INSTALLED`, callable originals).

Ordered next steps (replacing the older HIDEvents-registration plan,
which was never confirmed as the gate):

1. Finish an IDA session on `analysis/SkyLight_22F82` (UUID
   96676A53-B1E0-3D7E-B98B-B73873CD1880) to name the lazily-resolved
   checker and find who populates the WS rights cache — the
   loginwindow/session-owner registration path is the leading suspect
   (`SLSSetLoginwindowConnection` entitlement
   `com.apple.private.SkyLight.loginwindow` is absent from our probe).
2. Session registration: `SLSCreateLoginSession` → `sid=0`.
   THEORY (unproven): a login-framework temporary bridge creates the
   session locally and WS is never notified; the cause of `sid=0` and
   the real owner-registration handshake are unestablished — determine
   them with exact RE/runtime evidence.
3. If revival is impossible: fallback menubar design — process-local
   NSEvent + HIToolbox menu-tracking entry points
   (`-[NSApplication _menuTrackingStart]` family) bypassing WS dispatch.

When the first `APP-INPUT WS-DISPATCH` line appears in any app log, input
is alive — that is THE acceptance test.
