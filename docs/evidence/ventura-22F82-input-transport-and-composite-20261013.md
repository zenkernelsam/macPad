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
