# Ventura 22F82 — composite resurrection, wallpaper fix, input transport dead-end map (2026-10-12/13)

Overnight bring-up session on iPad13,11 / iPadOS 16.3 / Dopamine rootless,
Ventura 13.4 (22F82) chroot. Three durable outcomes:

1. **The desktop compositor is alive end-to-end** — `state=ready`,
   continuous final-composite sequence delivery, wallpaper rendered.
2. **A documented deployment landmine was hit and confirmed**: `cp`-overwriting
   a signed dylib kills every chroot process with `Invalid Page`. Always swap
   fresh inodes.
3. **The input dead-end is now fully mapped by disasm + runtime A/B**, and the
   remaining failure is narrowed to one hypothesis: WindowServer's CGX event
   dispatch subsystem never initializes under `-virtualonly` without a login
   session. Every client-side posting API was tried; the transport is not the
   blocker — the server-side pipeline is.

Every claim below names its witness.

---

## 1. Deployment: never `cp`-overwrite signed dylibs (device-wide kill)

### Symptom

After deploying a rebuilt `libmachook_arm64.dylib` with a plain `cp` onto the
live path, **every** chrooted binary — `/bin/echo`, `/usr/bin/true`, `ls`,
bash — was SIGKILLed at exec. `dmesg` / `log show` reported
`CODESIGNING Invalid Page` at fault address `dyld private area` on the first
dylib map.

### Root cause (runtime-confirmed)

`DYLD_INSERT_LIBRARIES` injects `/usr/local/lib/libmachook_arm64.dylib` into
**every** chrooted process. `cp` overwrites the file's contents in place —
**same inode**. The kernel (via libjailbreak/amfi) caches code-signature state
per-vnode; overwriting contents invalidated the cached page hashes for a file
that was already trusted, so the first page map of the injected dylib failed
codesigning → `SIGKILL` in dyld before `main`.

`misc/build_on_ios.sh` already documents the exact pitfall:

```sh
# rm before cp so target gets a FRESH INODE — avoids the stale-codesign-cache
# AMFI Invalid Page bug
sudo rm -f /var/jb/usr/macOS/lib/libmachook.dylib
sudo cp "$BUILT" /var/jb/usr/macOS/lib/libmachook.dylib
```

### Fix applied

`install -m755 <new> <dest>.new && mv <dest>.new <dest>` (or `rm` first, then
`cp`) — a fresh inode gives a fresh vnode → clean signature state. Recovery
required nothing else; `chroot /bin/echo HI` returned `HI` immediately after
the atomic reinstall. The deployed recipe that works end-to-end:

```sh
python3 /var/jb/usr/macOS/bin/set_macos_version.py <file>     # iOS→macOS marker
ldid -S<libmachook_ent.plist> <file>                          # twice — ldid/lipo
ldid -S<libmachook_ent.plist> <file>                          # invalid-page quirk
install -m755 <file> <dest>.new && mv <dest>.new <dest>
python3 /var/jb/usr/macOS/bin/macws_boot_trust.py --readd <files...>
```

**Lesson**: any signed file that is memory-mapped by other processes must be
replaced atomically. This applies to `libmachook*`, OSXvnc, probes, and every
binary in the chroot closure. The same trap previously produced
`rc=137/133`-style failures for temporary probes that lacked arm64e slice,
`set_macos_version` patching, entitlement signing, or trust registration.

---

## 2. Composite channel resurrected: publish scanout per delivered swap

Commit `0614a5b` — `libmachook/mac_hooks.m`.

### Finding

`MetalContext::EndUpdate` **never runs** on this build (hook installed,
zero invocations) → `g_macws_last_end_update_metal_context` permanently NULL →
the EnqueueCompletion-based publish path could never observe a completed frame
→ displayd received at most **one** final-composite frame per WS generation
(`sequence=1` then silence), while the WS render loop kept producing
`IOMFB GPU-FENCE unavailable → no-work cancellation` swaps.

But every coexistence swap is cancelled *by design*; the delivered synthetic
completion is the real per-frame boundary, and the **owned scanout IOSurface**
(discovered via `lease-new #1` → `IOSurface 26`) already holds final pixels at
that point.

### Fix

`macws_publish_scanout_after_swap()` publishes the currently owned scanout
surface on each delivered swap. Result (runtime-confirmed):

```text
MACWS-DISPLAY workspace-graph-state state=ready windowserver=87761
  canvas=2732x2048 wallpaper=ready dock=ready menubar=ready backing=ready
final-composite-received sequence=278+ (continuous)
```

The live framebuffer at that moment contained real rendered content:
Terminal window with shell output, front-app menu bar with ticking clock,
full Dock strip — PNG witness captured through the WS mmap path.

### Companion fix — graph reconciliation deadlock (same commit family)

`macwsdisplayd` oscillated `ready ↔ recovering` because `dockReady` required a
canvas-sized surface while the live final composite **intentionally
suppresses** per-layer Dock/backing captures. Fix: structural matching for
Dock/backing layers whose `CGWindowList` names are empty, plus treating the
live final composite as authoritative for suppressed layers. Graph now holds
`state=ready` through topology churn.

---

## 3. Wallpaper: dynamic HEIC decodes as black — static PNG works

### Symptom

`NSWorkspace.setDesktopImageURL` returned YES, the desktoppicture.db record
was written (00:17), and the wallpaper layer had a real surface
(`frame-first ... surface=2732x2048`) — yet the composite rendered pure black.

### Root cause (fix-witnessed; decode-path is inference)

Dock renders `file:///System/Library/CoreServices/DefaultDesktop.heic` — the
**dynamic Solar-variant HEIC**. The wallpaper surface existed in the catalog
(`layer=22 owner=Dock surf=169`) yet rendered black. A direct IOSurface
pixel readback was NOT possible (cross-process surface lookup unavailable
in this environment), so "the decode produced black" is inference from the
fix, not a measured pixel: the file itself decodes (6016×6016) on a real
path, but Dock's render of it is black in the chroot.

### Fix applied (device-side only — not yet in build-rootfs)

Replaced the dynamic HEIC payload with a static PNG at the same path —
ImageIO content-sniffs and renders it regardless of the `.heic` name.
Immediate result: forest/lake wallpaper rendered, framebuffer sample
(61,73,32) ≠ black.

**TODO for rootfs build**: bake this substitution into
`misc/build-rootfs-13.4.1.sh` (swap `DefaultDesktop.heic` for a static
payload at install time), otherwise every fresh rootfs reinstall brings back
the black wallpaper.

---

## 4. Input transport dead-end — complete map

This is the big negative/positive result of the session. **Every** plausible
event-injection API was exercised and instrumented; the failure is provably
server-side.

### 4.1 The HID event server port does not exist

Runtime probes inside the chroot — the ad-hoc `/tmp` probes from this
session are now committed as `misc/macos_ws_event_probe.c` (build/deploy
recipe in its header comment) and `misc/macws_inject_record.py`:

Verbatim probe output (2026-10-13, inside the chroot):

```text
probe: com.apple.windowserver                    -> rc=0(OK) port=6659
probe: com.apple.windowserver.active             -> rc=0(OK) port=3075
probe: com.apple.windowserver.session            -> rc=1102(unknown-service) port=0
probe: com.apple.windowserver.session.check-in   -> rc=1102(unknown-service) port=0
probe: com.apple.WindowServer.HIDEvents          -> rc=1102(unknown-service) port=0
probe: com.apple.private.SkyLight.event.monitor  -> rc=1102(unknown-service) port=0
probe: com.apple.iohid                           -> rc=1102(unknown-service) port=0
probe: com.apple.multitouch.user-access          -> rc=1102(unknown-service) port=0
probe: com.apple.windowserver.displays           -> rc=1102(unknown-service) port=0
probe: event port=1000
probe: mach_port_type rc=15 type=0x0 (send=0 recv=0 sendonce=0 deadname=0)
```

Every session/event/display service is absent — not just the HID port.
The core `com.apple.windowserver` lookup and the `.active` endpoint DO
resolve, which is why CGS connections, cursor warp and compositing work.

WindowServer simply never created/registered its event server port. Every
`CGEventPost` variant — `kCGHIDEventTap`, `kCGSessionEventTap`,
`kCGAnnotatedSessionEventTap`, `CGEventPostToPid` — posts into a dead name
(the mach send silently fails or the port doesn't exist). `CGWarpMouseCursorPosition`
**works** (cursor moved and read back (600,500)) — the client→WS *control*
channel is alive; only the *event* channel is absent.

This matches the WS log: `Using Smart Event Tap -- HID for console user`
(OSXvnc) — OSXvnc also picked the HID path, which is dead here.

### 4.2 `SLSPostEventRecord` is a live transport — and it returns WS's verdict

RE of `SkyLight` __TEXT `0x185156788`:

```text
SLSPostEventRecord(connID, record, recordSize, flags):
  CGSConnectionByID(connID)        -> NULL => 0x3e8 (1000)
  CGSScoreboard()                  -> NULL => 0x3e8
  assert(record->length == arg2)
  assert(arg2 == 0xf8)             // sizeof(SLSEventRecord) = 248
  fill session field via CGSConnectionGetEventState
  fill timestamp via SLSCurrentEventTimestamp when zero
  tccPostEventService()->requestAccess()   // TCC post-event check
  CGSEncodeEventRecord -> mach_msg(conn->send_port, bits=0x1513
                                   = MACH_SEND_MSG|MACH_RCV_MSG|MACH_SEND_TIMEOUT)
  reply: w19 = reply[+0x20]        // = WindowServer's return code
  // (0x1513 is the mach_msg OPTION BITS field, NOT a msgid; the actual
  //  CGS MIG msgids observed on the wire are small ids like 1012/1023/1025)
```

**The transport works**: `mach_msg` succeeds; `rc` is WS's reply. So when a
probe returns `1002 (kCGErrorInvalidConnection)` for a mouse record, that's
**WindowServer actively rejecting the post** — not a dead channel. Same reply
for session-bound app connections (Terminal, pid 93780: `down=1002 up=1002`)
as for sessionless probes — i.e., WS rejects raw mouse records from every
connection type tried.

`_SLPSPostEventRecordTo` (RE at `0x1853dffb0`) posts the encoded record on
the caller's primary connection send port — same funnel.

### 4.3 The button-transition path is a different MIG call entirely

`SLSPostMouseEventsVButtons` (the body behind `SLPostMouseEvent` /
`SLSPostMouseEvent` / `CGPostMouseEvent`) does **not** post button
transitions via `SLSPostEventRecord`. It:

1. `CGSConnectionGetEventState` → reads the connection's tracked button array
2. computes per-button transitions into new/old state byte arrays
3. calls **`CGSMouseStatePostMouseButtonsChangedEvents`** for down/up
4. calls `SLSPostEventRecord` only for the *move/drag* record (type
   5/6/7/0x1b = CGEventType move/LDrag/RDrag/OtherDrag)

So a raw hand-built mouse record posted via `SLSPostEventRecord` hits a
validation path WS doesn't accept for buttons — consistent with the 1002.

### 4.4 Scroll works through the same funnel — the difference is the payload

`SLPostScrollWheelEvent` (RE at `0x1851355cc`) builds a 248-byte record
(type=0x16 at +0x08, wheel units at +0x88/+0xa8.., session=-1 at +0x38,
scoreboard point at +0x10/+0x18) and calls
`SLSPostEventRecord(SLSMainConnectionID, rec, 0xf8, 0)` — **the identical
funnel**. The in-tree comment at `AppInputBridge.m` ~6007 asserts the
legacy `CGPostScrollWheelEvent` "still reaches WindowServer from this same
CGS-connected process", and scrolling was user-observed working in this
build — but note the bridge also has an `NSWindow.sendEvent`-discrete
process-local fallback (`SCROLL-CAPABILITY` log picks the route), so the
WS-side scroll acceptance is inferred, not isolated. Mouse/keyboard
records are rejected — suggesting type-specific gating in WS's record
handler (or the pointer-injection privilege — the TCC `postEvent` service
is checked client-side and WS-side policy may require it).

### 4.5 `CGPostMouseEvent` partial result

From the Terminal process's own connection (pid 88250):

```text
legacy down -> rc=0      (accepted by WS)
legacy up   -> rc=1002   (rejected)
```

No `APP-INPUT WS-DISPATCH` witness fired either way — WS never dispatched a
mouse event to any app's event queue during the whole test matrix.

The same verdict came back from the standalone probe (`SLSPostEvent` /
`SLSPostEventRecord` on the probe's own connection):

```text
probe: cid=225283 displays=1
probe: SLSPostEvent move -> rc=1002
probe: cursor-after-SLSmove=(600.00,500.00)   # unchanged — no dispatch
probe: SLS menubar click rc=1002/1002
```

### 4.6 New delivery witness (file-gated)

`MacWSAppInputApplicationSendEvent` now logs
`#### APP-INPUT WS-DISPATCH pid=%d type=%lu window=%ld loc=(x,y)` for any
mouse/scroll/gesture event that is **not** a bridge-local fabrication — i.e.,
can only have arrived from WindowServer's dispatch. Arm it with:

```sh
touch /var/mnt/rootfs/private/tmp/macws_ws_dispatch_witness
```

Throughout the session it fired **zero times** — the authoritative proof that
WS dispatched nothing to any app.

### 4.7 Root-cause hypothesis (THEORY — needs RE of SLXServer)

`com.apple.WindowServer.HIDEvents` lives inside the WindowServer binary /
`SLXServer` init. Under `-virtualonly` with no `loginwindow`, no real display
registration, and no audit-session binding, WS most likely skips the entire
CGX event-driver initialization: no `com.apple.WindowServer.HIDEvents`
registration, no event port handed to clients, event records received on
connection ports answered with policy rejection or swallowed.

**The evidence consistent with this**: HID port absent, `SLSGetEventPort`
invalid, all event dispatches absent (zero WS-DISPATCH), record posts
rejected with invalid-connection for mouse types while scroll-type posts from
app connections are accepted.

**What this does NOT explain yet**: why scroll specifically works. Possible
answers: scroll goes through a different WS message (it shares the funnel but
the payload differs — wheel events may be routed by a different handler that
doesn't require the event pipeline), or the scroll's `CGPostScrollWheelEvent`
dispatch doesn't need the missing subsystem at all. Verify by tracing which
WS-side demux handler processes the encoded event-record request for
type 0x16 (scroll) vs types 1-8 (mouse) and where each verdict is set.

### 4.8 Next concrete steps (in order)

1. **RE `SLXServer`'s init sequence** in SkyLight — find where
   `com.apple.WindowServer.HIDEvents` is registered (string xref on the
   service name inside the SkyLight/WindowServer binaries) and what
   condition gates it (likely a `loginwindow`/audit-session/display-
   ownership check). That gate is the
   upstream fix point — everything else is plumbing around its absence.
2. If the gate can't be satisfied cheaply, **look for the WS-side record
   demux** that answers the encoded event-record request and see what
   produces `0x3ea` for mouse types — there may be a session-context check
   that can be satisfied by registering the caller's session differently
   (e.g., `SLPSPostEventRecordTo` targeting WS's own connection).
3. If WS dispatch can't be revived, the **fallback design**: route menubar
   clicks as process-local NSEvents to the front app's `sendEvent:` with the
   menubar windowNumber — AppKit/HIToolbox has a private path for the
   menubar hit (`-[NSApplication _menuTrackingStart]`-family) that may not
   require WS dispatch at all. Investigate `HIToolbox` menu tracking entry
   points.

### 4.9 Things definitively dead (stop retrying)

- `CGEventPost(kCGHIDEventTap)` — port doesn't exist
- `CGEventPost(kCGSessionEventTap)` — posts accepted, zero dispatch
- `CGEventPostToPid` — same
- `SLSPostEvent` on a `CGEventRef` — ABI wrong: arg must be an SLS event
  whose +0x18 is the SLSEventRecord; use `SLEventGetEventRecord(ev,&rec,0xf8)`
  to extract, or `SLEventCreateMouseEvent` to construct
- Raw mouse `SLSPostEventRecord` — rejected 1002 even from authorized conns
- OSXvnc RFB PointerEvent — reaches handler, `CGPostMouseEvent` inside
  silently does nothing useful (down accepted, up rejected, no dispatch)

### 4.10 What the session-tap change in `mac_hooks.m` means

`macws_vnc_session_mouse_post` now posts via `kCGSessionEventTap` instead of
`kCGHIDEventTap`. Both are dead today; the session tap is the *less* wrong
choice because it doesn't depend on the unregistered HID port — if/when
`SLXServer`'s event pipeline is fixed, session tap is the standard client API
surface that should deliver. Treat as unverified until a WS-DISPATCH witness
fires.

---

## 5. Runtime inventory at end of session

```text
WindowServer            pid 87761  -virtualonly -daemon   (composites alive)
macwsdisplayd           pid 88080  state=ready, seq advancing (publisher)
Dock                    pid 88216  owns wallpaper layer=22 surf=169 +
                                   Dock strip layer=26; LS launch requests live
Terminal                pid 88250  layer=27 1140x742; WS-DISPATCH witness armed
OSXvnc-server           pid 83507  -rfbnoauth -swapbuttons -deferupdate 0
Finder                  pid 88150  layer=16 fullscreen surface
Control Center          pid 88156  menubar extras layers 17-21,24
libmachook              deployed thin arm64+arm64e slices, trustcache-registered
wallpaper               static payload rendered (was dynamic HEIC -> black)
```

The `workspace-graph-state` file at that moment:

```text
state=ready windowserver=87761 publisher=88080
wallpaper=22/88216/169/0,0,2732,2048
dock=26/88216/0/0,0,2732,2048
menubar=13/87761/112/0,0,2732,48
backing=2/87761/0/0,2732,2048
```

State at `macos_gui.sh stop` time: clean shutdown, no lingering daemons.

## 6. Open frontier (unchanged, ordered by leverage)

1. **WS event subsystem init** — the single remaining blocker between "a
   desktop you can see" and "a desktop you can use". §4.7/§4.8.
2. **GPU fence / pacing** — every swap is a no-work cancellation; Metal frame
   completion is synthetic. Visible as general sluggishness under load.
   `IOMFB GPU-FENCE unavailable` is the upstream gap (iOS kernel won't vend
   the fence macOS expects).
3. **Wallpaper build step** — bake the static-PNG substitution into
   `build-rootfs-13.4.1.sh` so fresh rootfs installs don't regress.
4. **Session/activation depth** — `MacWSRepairFrontUIApplication` completes
   all activation transactions but without WS dispatch the menubar tracker
   still can't run. Once §4.7 lands, re-test the full ActivateTarget path.

## Evidence file cross-refs

- `docs/porting/ventura-22F82-session-reproduction-20261013.md` — the
  cold-start reproduction runbook: device map, build/deploy recipes,
  launch sequence, verification ladder, tool index, landmine list
- `docs/evidence/ventura-22F82-desktop-chain-20261011.md` — LaunchServices/XPC
  chain + privileged-flag fix + earlier desktop chain witnesses
- `analysis/MacWSBootingGuide/docs/evidence/vnc-usability-stability-20260729/`
  — the 22F66-era proof that native CGPost paths once opened menus (different
  WS generation, not `-virtualonly` chroot)
- `analysis/MacWSBootingGuide/AGENTS.md` — patch/evidence discipline this
  document follows

---

## 7. 2026-10-14 session-layer findings (new, runtime + RE confirmed)

The §4.7 hypothesis was tested end-to-end. Result: the gate is NOT the
HIDEvents port — it is the **empty session registry** on the WS side.
Every piece below was measured on-device (`<MACWS_DEVICE>` jailbroken
iPad, WS=59329, stack started via `macos_gui.sh start coexist`).

### 7.1 The event port is real (correcting §4.1's read)

`SLSGetEventPort` is `kern_return_t fn(uint32_t cid, mach_port_t *out)`.
With the CGS connection established first it returns `rc=0 port=4111`,
and `mach_port_type(4111)` → `rc=0 type=0x30000` (send-once + receive).
The earlier "port invalid" line was the probe calling the API before a
connection existed; the `1000` seen for remote conns is the CGX-OK
convention (CGX returns 1000-family codes where `1000 == success`), not
an error. runtime-confirmed via ws_evprobeC output.

### 7.2 Connection session state vs WS session registry

```
SLSCopyCurrentSessionDictionary ->
  { kCGSSessionAuditIDKey=5068627, kCGSSessionGroupIDKey=88,
    kCGSSessionIDKey=257, kCGSSessionOnConsoleKey=1,
    kCGSSessionUserIDKey=88,   // _windowserver
    kCGSSessionUserNameKey="unknown", kCGSessionLoginDoneKey=0 }
SLSCopySessionList -> CFArray count=0     // EMPTY
SLSGetSessionUID -> 0x10000003
```

Our connection reports the synthetic pre-login session 257 owned by
`_windowserver` (uid 88) that never finished login — and WS's session
list contains **zero sessions**. This is the first structural anomaly
with a clean causal path to the post rejections.

### 7.3 Session creation is accepted but produces nothing

```
SLSSetLoginwindowConnection(cid) -> rc=1000   // CGX-OK (or default)
SLSCreateLoginSession -> rc=1000 sid=0        // accepted, sid empty
SessionList stays count=0 after both
```

`SLSCreateLoginSession` → MIG `CreateSession` (msgid `0x7148`). The stub
(`RE: 0x1854473dc`) resolves the caller audit token, calls
`SessionGetInfo`, then needs the session-owner object at global
`0x1d8ba6460`. That global has exactly **one** writer site in all of
SkyLight (`str x22,[x19,#0x460]` at `0x1852850ac`, inside a vtable-heavy
installer at `0x185284d90`) — it was apparently never populated in this
environment, so the create path yields `sid=0`.

### 7.4 TCC post-event access is denied

`SLPreflightPostEventAccess` returns `0` — RE (`0x18520cee8`) shows it
reads a lazily built TCC-client object's `+0x19` byte = granted flag.
Adding `kTCCServicePostEvent` to `com.apple.private.tcc.allow` on the
probe did NOT flip it (still 0). The only tccd running is iPadOS's
(`mobile`, outside chroot) — the evaluation either can't reach a chroot
tccd or isn't entitlement-driven at this layer. Unresolved whether this
is *the* 1002 gate or a parallel one.

### 7.5 Every record post returns 1002 — uniformly

| call | result |
|---|---|
| `SLSPostEventRecord` mouseMoved/down/up | 1002 |
| `SLSPostEventRecord` canonical `SLEventGetEventRecord` record | 1002 |
| `SLSPostEventRecord` scroll-wheel record | 1002 |
| `CGPostScrollWheelEvent` issued | no WS-DISPATCH witness |
| `SLEventTapPostEvent(tap=0)` | garbage rc (needs real tap obj) |

Scroll on device works through **AppInputBridge's local `sendEvent`
injection**, proven now — `SLSPostEventRecord(scroll)` = 1002 and
`CGPostScrollWheelEvent` produced no dispatch witness. The earlier
"scroll posts reach WS" inference is retracted.

### 7.6 Where the 1002 is generated (RE-confirmed)

CGX msgid table decoded: base `0x7148`, 40-byte descriptors at
`0x1dd49fd80`, layout `{stub@0, size@8, ?, impl@0x18, argc@0x20}`,
routine for msgid `m` = `*(table + (m-0x7147)*0x28)`. Name table
decoded the whole family:

```
0x7148 CreateSession        stub 0x1854473dc (validates, SessionGetInfo)
0x7212 SetLoginwindowConnection  (stub=0 generic, impl 0x18546aa7c)
0x7287 PostEventRecordTo    impl 0x185461d14
0x7288 PostEventRecord      impl 0x1854618b4 (generic demarshal)
```

`PostEventRecordTo` impl `0x185461d14`, RE verbatim:

```
ldr w8,[x0,#0xc]            // target id from request
bl  0x1851ea104             // registry lookup
cbz x0 → mov w8,#0x3ea → [reply+0x20] = 1002
```

`0x1851ea104` = `std::map<uint32_t,obj*>::find` — a global
id→object registry (guard-initialized singleton). The 1002 is the
**registry lookup miss** — consistent with the empty session registry:
the target object can't be resolved because the session/conn tree WS
maintains is unpopulated.

`postCGSEventToProcess` log lines: **zero occurrences** in
WindowServer.err — rejection happens before event processing.

### 7.7 SLPSPostEventRecordTo ABI resolved

`_SLPSPostEventRecordTo(uint32_t target, const void *record, size, fl)`:
- resolves a cached WS port via `0x1853de23c` (global `0x1db438f50`,
  once-init; deallocated after use)
- encodes TLV request via `0x185227924`, callback `0x1853e006c`
- sends msgid `0x7287` (`PostEventRecordTo`), expects reply `0x72ec`
  (this subsystem uses reply = request + 0x65)
- earlier probe deaths (status 9) were child-process kills on malformed
  arg0 — the call shape `(targetID, rec, 0xf8, 0)` is correct; the
  server rejects on the registry miss above

### 7.8 What still needs the answer

1. **The conn-port dispatcher** — `SLSPostEventRecord` (0x7288) goes to
   the *connection's* send port; its impl mapping suggests a generic
   demarshal that may share the same registry lookup. The 1002 site for
   it specifically wasn't pinned yet (impl `0x1854618b4` shows no direct
   0x3ea in its first ~110 insns — the miss is one frame deeper).
2. **Who populates `0x1d8ba6460`** — the single writer's caller chain.
   `SLSSetLoginwindowConnection` returning 1000 but doing nothing
   suggests a feature-gate byte (`[0x1db438b00]`, tbz early-out at
   `0x185368408`) that is OFF here — find what turns it on.
3. **Audit session requirement** — `SLSessionOwnerCreateNewSessionWith-
   AuditSessionID` needs a real kernel audit session id. Whether
   `audit_session_join`/`session_create` works in the chroot is untested.
4. **22F66 vs 22F82** — upstream ran 22F66 where dispatch worked. Either
   a behavior delta between builds or a missing env piece (e.g. a
   session service the upstream stack had). No 22F66 SkyLight in repo;
   compare when one is available.

### 7.9 Things now definitively dead (do not retry)

- `CGEventPost` all taps — confirmed again today, no dispatch
- `CGPostScrollWheelEvent` as a WS-dispatch path — no witness
- `kTCCServicePostEvent` entitlement flip — Preflight still 0
- `SLSSetLoginwindowConnection` + `SLSCreateLoginSession` alone —
  accepted, produces no session

---

## 8. 2026-10-14 (evening): the instrumentation was never silent — it was filtered

The "CGXSYNTH-INSTALL never appears" mystery from earlier today is
solved, and it was not a missing callback at all.

### 8.1 `fprintf(stderr, …)` in mac_hooks.m is filtered (RE-confirmed in source)

`libmachook/mac_hooks.m:331` — every `fprintf` is macro'd to
`macws_filtered_fprintf`, which **drops stderr output entirely** unless
`MACWS_RUNTIME_DIAGNOSTICS` is in env or `/tmp/macws_runtime_diagnostics`
exists inside the chroot (`/var/mnt/rootfs/tmp/macws_runtime_diagnostics`
on the iOS side). The `#### MACWS-CTOR-CANARY` lines survive because they
use raw `write()`. Every `#### CGXSYNTH*` line used fprintf → invisible.

Consequence: the diagnostic hook was probably installing correctly in
earlier runs; we could not see it. To observe any `####` trace from
libmachook, `touch /var/mnt/rootfs/tmp/macws_runtime_diagnostics` first.

### 8.2 `MSFindSymbol` returns NULL for local `t` symbols here

`MSFindSymbol(header, "_CGXSenderCanSynthesizeEvents")` → `NULL`
(runtime: `CGXSYNTH-INSTALL gate=0x0` in the fname_probe run). On
shared-cache SkyLight, local symbols are not reachable through
Substrate's lookup. Resolved by image-relative offset instead, exactly
like the audit slot:

```
gate = header + (0x1853fd464 - 0x1850e5000)   // = +0x318464
```

RE-confirmed via capstone at that offset: `pacibsp; …; adrp x8,0x1d8ba6000;
ldr x8,[x8,#0x1b8]; cbz → return 0; ldr w9,[x8,#4]; cmp w9,#0x34` —
reads `gCurMsgAudit` (`0x1d8ba61b8`) and applies the ≥0x34 credential-size
gate. Identical structure to the earlier RE notes.

### 8.3 New deployment landmines hit and fixed (distinct from the inode bug)

| Landmine | Symptom | Fix |
|---|---|---|
| `ldid -S <space> ent.plist binary` on device | `Unknown header magic` — `-S` must be the **concatenated** form `-S/path`; the space form signs nothing and can leave a corrupt/adhoc signature → SIGKILL(137) / SIGTRAP(133) flapping | always `ldid -S/var/jb/usr/macOS/bin/entitlements.plist <bin>` |
| fresh cross-built dylib missing platform fix | dyld insert: `incompatible platform (have 'iOS', need 'macOS')` | `python3 misc/set_macos_version.py <dylib>` before signing, every rebuild |
| trustcache degradation mid-session | everything chroot → SIGKILL/SIGTRAP | `macws_boot_trust.py /var/mnt/rootfs` full restore re-added **1612** hashes |
| fat dylib at `/usr/local/lib/libmachook.dylib` | dyld rejects fat `DYLD_INSERT_LIBRARIES` in this chroot | ship thin slices: `libmachook.dylib`=arm64e, `libmachook_arm64.dylib`=arm64 |

### 8.4 Live witness: gate hook IS installed in real WindowServer

With the sentinel file present and the thin arm64 slice deployed:

```
WS pid 626: #### CGXSYNTH-INSTALL gate=0x1977e9464 slot=0x1eaf921b8
            hdr=0x1974d1000
```

`_dyld_register_func_for_add_image` replay works normally (verified: a
probe-side callback sees all 244 images; dyld-1286.10 source
`APIs::_dyld_register_func_for_add_image` replays unconditionally, even
from constructors).

### 8.5 The remaining anomaly: hooked gate never fires during real posts

Four `SLSPostEventRecord` posts from ws_evprobeG → four WS replies
(msgid `29421`, `bits=0x12`, successful sends — replies DID reach the
client with the 1002 payload). But **zero** `#### CGXSYNTH` gate lines.

`_XPostEventRecord` (22F82, `0x18546de64`) re-disassembled:

```
ldr w8,[x0+4]  request size must be in [0x38,0x4038]   else w0=-0x130
ldr w21,[x0+0x34] recordLen; (len+0x3b)&~3 must == size else -0x130
bl  0x1851ea104                  // CGXConnectionForPort → x22 (unchecked!)
bl  0x1853fd464                  // CGXSenderCanSynthesizeEvents(conn, cred)
cbz w0 → 0x18546df94: mov w0,#0x3ea → reply[0x20]=1002
```

The gate call is **unconditional** once request validation passes. So a
1002 reply provably transited `CGXSenderCanSynthesizeEvents` — yet the
hook did not log. Options remaining, in order of likelihood:

1. `MSHookFunction` silently failed on the gate in the WS process
   (INSTALL only proves the call was attempted; Substrate can no-op on
   unrelocatable prologues). Verify by reading bytes at gate — a hooked
   function starts with a `b` to the trampoline, not `pacibsp`.
2. The replies (`id=29421`) belong to a different routine than
   `PostEventRecord` — the msgid arithmetic for this subsystem was
   `reply = request + 0x65` (§7.7), so request `29320 = 0x7288` matches,
   but the msgid→impl table decode should be re-verified.
3. The demux routes to a second copy/alias of the impl.

**Next action**: extend the diag — hook `_XPostEventRecord` itself
(`header + 0x388e64`) for an entry counter, plus a readback of the
gate's first 8 bytes after `MSHookFunction` to prove/disprove the
trampoline.

### 8.6 Diagnostic code state in tree (all labeled DIAGNOSTIC)

- `macws_diag_sender_can_synth` + install block (mac_hooks.m ~5890-6040):
  logs `cred`, cred size, audit euid/pid, gate return.
- `#### IMGCB` print at top of `loadImageCallback`: fires for NULL
  `dli_fname`, SkyLight, and ImageIO image adds — the proof the add-image
  callback path works at all.
- Both are gated behind `macws_runtime_diagnostics_enabled` like every
  other `####` fprintf — sentinel file `/tmp/macws_runtime_diagnostics`
  in chroot `/tmp` is currently present on device.
- `ws_evprobeG` (device `/usr/local/bin/ws_evprobeG`) run with arg
  `postrecord` does the minimal SLS conn + 4× SLSPostEventRecord posts.

### 8.7 Process state at shutdown

macPad stack fully stopped (WindowServer bootout'd, macws* daemons +
MacWSHost killed, probes killed). Deployed artifacts on device:
`libmachook.dylib` (thin arm64e) + `libmachook_arm64.dylib` (thin arm64)
both = current tree + diagnostics, signed + trustcach'd. Trustcache
restored to 5690-hash closure this session.


## 9. 2026-10-15 corrections (R003/R004A) — read before trusting §4–§8 interpretations

The following statements in this document are **superseded**:

- §4.1's "event port invalid" — corrected already in §7.1; restated here
  for emphasis: `SLSGetEventPort(cid, &port)` returns `rc=0` with a real
  receive right.
- The blanket reading "Mach `1000` = CGX-OK" — per-routine codes differ;
  `_XSetLoginwindowConnection` returned `1000` on a *failed* connection
  lookup, and `_XPostEventRecord` denies with `1002`.
- "The `#### …-INSTALL` line proves a hook installed" — it only proves
  the attempt ran; the pre-R003 provider was a no-op scaffold.
- "The audit pointer is always NULL on the post path" — runtime telemetry
  shows `gCurMsgAudit` populated with a 52-byte credential on every
  canonical post (`#### CGXSYNTH cred=… size=52 … rc=0`).
- Any probe-process read of SkyLight globals (`rightsCache`,
  `lazyChecker`) describes only the probe's own image — it cannot be
  cited as WindowServer state.

Additional corrections and current facts:

- The `MSHookFunction` provider is now real: permit-gated trampoline,
  `MACWSHookPermitV2` capability, callable-original refusal contract;
  verified `VERDICT=INSTALLED` on device.
- The diagnostics sentinel lives in the **chroot** namespace:
  `/var/mnt/rootfs/tmp/macws_runtime_diagnostics` — the iOS `/tmp` path
  is a different file. Cleanup must check BOTH (an archived copy lives
  at `…/tmp/diag-archive/macws_runtime_diagnostics.r003-20261014`).
- Input is still unaccepted: `SLSPostEventRecord` → `1002`, zero
  `WS-DISPATCH` witnesses. All static dispatcher interpretations that
  predate an IDA session are provisional; the pinned RE input is
  `analysis/SkyLight_22F82` (UUID 96676A53-B1E0-3D7E-B98B-B73873CD1880,
  SHA256 13d7a0ca…906c61).
