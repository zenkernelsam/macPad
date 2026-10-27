# D041 — current-generation input acceptance + upstream denial boundary (2026-10-19)

Packet: `coordination/_relay/devin-041-backend-task.md`. Scope honored:
**no stop/restart/rollback/new generation, no watchdog interference, no
production source/build/deploy/marker/TCC/kernel change.** One bounded
input test on the LIVE user session (the desktop the user was looking at —
Finder frontmost, Dock visible). At most one controlled reversible
exact-app action; global route probed once with existing bounded probes.
The only transient artifacts were `/var/mnt/rootfs/tmp/macws_keyboard_latency_diagnostics`
(keyboard-only sentinel, present ~10 min for the test then **removed**) and
its trace file (extracted, then removed). Sentinel is deliberately scoped:
`MacWSKeyboardLatencyDiagnosticsEnabled()` reads only that marker and never
touches the global runtime-diagnostics umbrella or the display producer.

## 1. Inventory (all FACT, live generation ~00:16 start)

| process | pid | role |
| --- | --- | --- |
| WindowServer `-virtualonly -daemon` | 10447 | producer |
| macwsinputd | 10436 | input broker |
| macwsdisplayd | 10540 | display presenter |
| Dock | 10668 | system surface owner |
| Finder | 10595 | **frontmost app** (menubar title = Finder), Recents window 32 |
| Terminal | 10929 | window 27 but session DEAD — "[Process completed]" (login timed out) |
| ControlCenter / SystemUIServer / CoreLocationAgent | 10601 / 10599 / 10565 | menubar agents |
| watchdog | 9845 | `macos_gui.sh watchdog coexist --no-vnc --no-terminal --experimental` — left untouched |
| MacWSHost (iOS) | 8318 | presentation |

Composite state (private/tmp/macws_final_composite.state):
`state=ready producer=10447 sequence=695 reason=validated-final-composite`
before the test → `sequence=1176` after (advancing continuously).

Deployed hashes (current generation, recorded as-is; D042 provenance
reconciliation later showed these are the repository's own build outputs —
per-slice UUID/__text identity, not a separate newer deployment):

- macwsinputd `0bc473d185693e410988898f25857361d36549cbfa1b5a6d73b63c0b6ea2ab18`
- libmachook.dylib (arm64e) `aa041efda7a33099347c5d51865be82c1dd9004b782bea57252df4679e7d1113`
- libmachook_arm64.dylib `21f81b5cbc8c19fcc3e531d78f6bf220286f5545477baccb9225b89ad9b9b39b`
- CydiaSubstrate `134b7eb954ddfb65ca746c471fcad6d831c79745c48b9701ef74ef03dbcc85e3`
- probes (pre-existing, untouched): `macos_ws_event_probe` `0f4eac6e…838ef`,
  `macwstccdprobe` `6802512f…760da`, `winlist_probe` `cf1cefd0…32320`, `cgwl` `fcaa3c94…c3e9`
- launchd: `com.apple.tccd` (iOS user tccd, pid 883) and
  `com.macwsguide.tccd-system` (pid 10300) both running; chroot probes run
  directly via `launchdchrootexec 0 0 /var/mnt/rootfs <bin>`.

Window list (FACT, `winlist_probe`, on-screen): Terminal win=27 pid=10929
(0,25 570x371), **Finder win=32 pid=10595 (223,149 920x436)**, Dock win=26
fullscreen layer 20, menubar win=12, wallpaper win=23. AppInput endpoints
exist for all live app pids (`/private/tmp/macws_app_input.<pid>.sock`).

## 2. Global route — one bounded post + one TCC probe (FACT)

`launchdchrootexec 0 0 /var/mnt/rootfs /usr/local/bin/macos_ws_event_probe post`:

```
bootstrap_look_up com.apple.windowserver            -> rc=0
bootstrap_look_up com.apple.windowserver.active     -> rc=0
bootstrap_look_up com.apple.WindowServer.HIDEvents  -> rc=1102 port=0   # DEAD NAME
bootstrap_look_up com.apple.windowserver.session    -> rc=1102
bootstrap_look_up com.apple.CARenderServer          -> rc=0
SkyLight loaded uuid 96676A53B1E03D7EB98BB73873CD1880 match=1
SLSMainConnectionID = 155063
SLSGetEventPort(cid=155063) -> rc=0 port=5655 (mach_port_type ok)
SLPreflightPostEventAccess() -> 0
TCCAccessPreflightWithAuditToken(PostEvent/ListenEvent/SystemEvents,self)->1/1/1
SecTask.tcc.allow -> 0x15200dd10 (vended)   SkyLight.loginwindow -> 0x0
cursor before=(0,0) after-warp=(600,500) WARP-OK
CGEventPost(HID,moved 700,600) -> cursor=(600,500) NO-DISPATCH
canonical record: v=0 len=248 type=5
SLSPostEventRecord(canonical) -> rc=1002
SLSCopyCurrentSessionDictionary / SLSGetSessionUID=268435459 present
```

`launchdchrootexec 0 0 /var/mnt/rootfs /usr/local/bin/macwstccdprobe` (ping):

```
TCCPING reply-type=dictionary service=com.apple.tccd.system
TCCPING reply=<dictionary> { count = 0, transaction: 0, voucher = 0x0 }
```

`macwstccdprobe preflight`:
`audit=<token> kTCCServicePostEvent result=1`

### Global boundary (FACT unless marked THEORY)

Two independent denial surfaces, both upstream of any payload content:

1. **`com.apple.WindowServer.HIDEvents` is an unregistered name** (rc=1102).
   `CGEventPost(kCGHIDEventTap,…)` therefore dispatches to nothing —
   `NO-DISPATCH` even though the CGS control channel is alive (WARP-OK)
   and our own event port is valid.
2. **`SLSPostEventRecord` -> 1002** — WindowServer's `_XPostEventRecord`
   gate (`CGXSenderCanSynthesizeEvents` at SkyLight+0x318464) returns 0
   for a byte-perfect canonical record produced by `SLEventGetEventRecord`.
   This is WS-side policy evaluation, not transport failure.

Comparison to preserved evidence: the 1002-vs-accessor asymmetry persists —
the *same process's* `TCCAccessPreflightWithAuditToken` returns 1 while WS's
gate returns 0. The tccd transport itself now replies a real
OS_xpc_dictionary but **empty (count=0)** — consistent with the R007/R008
family finding (no explicit denial field carried; accessor-zero was never
proven to be a policy field). THEORY: the gate's permission-cache lookup in
dispatcher 0x1854257ec keys off audit-derived identity that never matches
the grant cache — the earlier `responsibility103` (responsible-pid binding)
finding remains the leading suspect. Not re-instrumented this round
(no permit/marker change allowed).

`SLPreflightPostEventAccess -> 0` is a separate accessor result on the
probe's own process — consistent with the missing `SkyLight.loginwindow`
entitlement; recorded as data, not interpreted as the WS denial cause.

## 3. AppInput exact PID/window path — ACCEPTED at AppKit layer

Target selection: Terminal 10929 ruled out — its window shows
`[Process completed]` (login timed out; a fresh window would hit the same
login contract). Finder 10595 chosen: **ownership verified twice** before
any injection — app-published `macws_window_metrics.10595.bin` (gen=8,
window 32 present) AND the live WS window list (`win=32 pid=10595
rect=(223,149 920x436) onscreen`). Record: `kind=KeyDown/KeyUp,
source=HardwareKeyboard(4), targetPID=10595, sceneID=window32+modifiers,
frame=1366x1024, x=683 y=512, flags|=LatencyDiagnostic(0x8000),
sampleSequence=1,2` via `misc/host_key_probe.py` →
`/var/mnt/rootfs/private/tmp/macws_host_input.sock` (inputd) →
`macws_app_input.10595.sock` (Finder's AppInputBridge).

Action sequence (one bounded reversible action set; per-record evidence in
`keyboard-latency.trace`, sha256 `49c7c2b1…8506c`):

| # | keys | records | app-socket | app-queue | app-dispatch | pixels |
|---|------|---------|-----------|-----------|--------------|--------|
| 1 | right arrow (kc 124) | down+up | 0.51ms | post-event | sendEvent saw keyDown+keyUp (2.69/12.0ms) | none (empty Recents) |
| 2 | 'd' (kc 2) | down+up | 0.27ms | post-event | sendEvent saw both (0.65/20.0ms) | none (no type-select target) |
| 3 | Cmd-F (kc 3 +command) | down+up | 0.31ms | post-event | sendEvent saw both | **search bar opened, focused caret** |
| 4 | 'd' | down+up | 0.24ms | post-event | sendEvent saw both | **'d' glyph in the live search field** |
| 5 | backspace + escape | 2×down+up | ok | post-event | sendEvent saw both | search bar closed; desktop == before |

`stage=app-dispatch` lines are emitted by the real `-[NSApplication
sendEvent:]` hook *inside Finder* — this is the AppKit delivery/ack, not
enqueue success. The visible consequence is unambiguous: `Cmd-F` produced a
new focused Search field in the Finder toolbar (`frame-after-cmdf.png`,
diff bbox 1233,2–2720,384 vs before) and `'d'` rendered the glyph inside it
(`frame-after-text.png`). `frame-reverted.png` is visually identical to
`frame-before.png` (residual diff bbox 466,2–2720,552 is menubar/Host-overlay
repaint noise, e.g. clock/status items; Finder window content identical).

Chain proven end-to-end (all FACT):

```
probe → macws_host_input.sock → inputd validate+route (exactWindowRecord)
      → macws_app_input.10595.sock → AppInputBridge socket thread
      → postEvent:atStart: → -[NSApplication sendEvent:]
      → key-equivalent/menu action (Cmd-F) + insertText ('d')
      → WindowServer re-composite → Host Metal → iPadOS pixels
```

End-to-end producer→dispatch latency 0.24–2.7 ms on keyDown.

### What this does NOT claim

- Pointer/touch path NOT exercised (no TargetProbe→hit-test→pointer
  lifecycle run through inputd's WindowTargetAtPoint/activation machinery).
- IME marked-text, clipboard, drag — not exercised.
- Global (non-exact) input remains denied per §2.
- Acceptance claim: **exact-PID/window key delivery to a verified AppKit
  endpoint works, including a real menu action and real text insertion.**
  Do not rebrand as general "input accepted".

## 4. Session health after test (FACT)

`state=ready producer=10447 sequence=1176` (was 695 before — kept
advancing through the test). All five core processes unchanged; watchdog
9845 untouched; no session teardown performed — the user keeps their
desktop. Desktop pixels == pre-test baseline.

## 5. Owned-artifact cleanup

- Removed `/var/mnt/rootfs/tmp/macws_keyboard_latency_diagnostics` and
  `/var/mnt/rootfs/tmp/macws_keyboard_latency.trace` (trace preserved here).
- Probe scripts (`host_input_matrix.py`, `host_key_probe.py`) and capture
  binary `macws_ipados_capture` remain in `/private/var/tmp/d041/` with the
  on-device frames for coordinator reproduction; the capture binary's
  CDHash `0b3dcc6663f1f420c7b09385bc52e4372c0d2acf` was added to the
  dynamic trustcache (required for it to execute; inert otherwise).
- No production binaries, jobs, or policy state changed. One transient
  diagnostic marker (`/tmp/macws_keyboard_latency_diagnostics` in the
  chroot) was created for the key-delivery witness and removed
  immediately after; the global `macws_runtime_diagnostics` marker was
  never armed.

## 6. Next producing layer (FACT/THEORY)

FACT: the only dead rung is the global pipeline — `HIDEvents` name absent
(1102) AND `_XPostEventRecord`→`CGXSenderCanSynthesizeEvents`→0→1002.
AppInput exact records bypass both by design (process-local socket, no WS
hit-test or gate). THEORY for the next packet: instrument
`CGXSenderCanSynthesizeEvents` (permit `96676A53…@+0x318464`, clean
prologue already mapped in `devin-input-axis-prep-20261019.md`) and
`_XPostEventRecord` (`+0x388e64`) with entry counters + byte readback to
localize which audit-field comparison fails; separately determine whether
`-virtualonly` suppresses the `HIDEvents` bootstrap registration or the
name lives on a different domain.

(handover to codex now)
