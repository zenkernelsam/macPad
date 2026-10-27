# D040 — restored-baseline ledger + next input acceptance plan

Date: 2026-10-19 · Devin frontend 9589 · read-only source/evidence audit.
No device/build/source/Git changes, no backend artifact edits.
Task: `coordination/_relay/devin-040-frontend-task.md`.
FACT = witnessed in saved artifacts or verified in current source;
THEORY = inference with the confirming evidence named.

## Part 1 — Restored-baseline ledger (consolidated)

### Deployed bundle (the accepted candidate)

| Layer | Source artifact sha256 | Deployed (signed) sha256 | Destination |
|---|---|---|---|
| libmachook arm64e slice | `f2143308…9146` | `aa041efda7a33099347c5d51865be82c1dd9004b782bea57252df4679e7d1113` | `/var/mnt/rootfs/usr/local/lib/libmachook.dylib` |
| libmachook arm64 slice | `bf3ae467…dc30` | `21f81b5cbc8c19fcc3e531d78f6bf220286f5545477baccb9225b89ad9b9b39b` | `/var/mnt/rootfs/usr/local/lib/libmachook_arm64.dylib` |
| CydiaSubstrate provider fat | `82e74519…e3a9` (arm64 `2a8da5bf…`, arm64e `3bde9239…`) | `134b7eb954ddfb65ca746c471fcad6d831c79745c48b9701ef74ef03dbcc85e3` | `/var/mnt/rootfs/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` |

Deployed≠source deltas are the `ldid -S<ents> -M` signature rewrite, not a
mismatch (D039 addendum). Deployment method `rm`+`cp` (fresh inode);
preserve set at `/var/mnt/rootfs/tmp/d037/preserve/` with recorded
sha256 (rollback capability intact). Deployment RETAINED per
keep-on-pass; iOS-side `/var/jb` provider twin deliberately unchanged.

### Live image identity (cache-extracted; extraction limit applies)

- IOMFB uuid `7AC531B7-05F5-38FE-92E5-DA6D86D22A14`, arm64e; bytes
  @0x11cc `b4000080 f9439401 b4000041 d61f083f` — pin match.
- QuartzCore uuid `CF853BBD-01B6-3F46-ADA1-EC70FD2DC9DC`; three pinned
  prologues match. Limit: `task_for_pid` denied (`kr=0x5`) — identity is
  from `dyld_shared_cache_arm64e` extraction (the image source the WS
  maps), not a read of the process's mapped memory.

### Launch flags (D037 generation)

`macos_gui.sh stop` → `start coexist --no-vnc --no-terminal
--experimental --runtime-cap=300` under a test lease; watchdog-enforced
cap (macos_gui.sh:1682) fired as designed.

### Static wallpaper identity

`/usr/local/share/macws/wallpapers/macws-forest-lake.png` —
sha256 `4931a89da65eccf2fb3b6085c21b09397a323a3cc0ba4a961de1a92d943d1806`
(2.79 MB, added 10-11). Applied per generation by
`apply_workspace_wallpaper` → `workspacectl set-wallpaper`
(macos_gui.sh:265, 4341-4366) via one-shot AppKit/NSWorkspace IPC under
`MACWS_UTILITY_PROCESS=1`. This is the forest/lake visible in
`frame-macos-desktop.png`. (The dynamic HEIC path decodes black in the
chroot — the static payload substitution is the established fix.)

### Runtime witnesses (D037)

- arm64e probe: `RESULT: ALL PASS` — real wrapper bytes, braaz
  authenticated IA+0 impl, x0 preserved, callable-original replay,
  named refusals.
- Counters: `replay-receiver-ready`, `replay-received 1→7`; displayd
  `final-composite-received producer=7763 surface=155 2732x2048`.
- State: `state=ready producer=7763 sequence 9→…→60`, ~190 s,
  `reason=validated-final-composite` (≈3.7 s/publish ≙ source 4 s
  keepalive, Metal_hooks.x:8427-8431).
- Pixels: `nonblack=0.984`; PNGs sha256 `1d8ff479…c558e2` (desktop —
  reviewer-inspected: menu bar, wallpaper, Dock) and `6bfd1827…bed0640`
  (pre-activation iPadOS negative control).
- Teardown: lease-gated stop, `state=fallback reason=layer-topology`
  (expected shutdown transition), iOS restored, 3 ExcUserFault (baseline
  class), probe leftovers removed.

### D039 wording corrections applied (this doc supersedes)

1. "first since Oct-12" → **restoration vs the 10-13 baseline**
   (commit `0614a5b` + runtime `0db21ae`, sequence 278+, framebuffer
   PNG — 10-13 was itself visible; corrected in D039 verdict).
2. Saved-PNG residual → CLOSED (artifacts inspected + sha256-verified).
3. Identity wording → cache-extracted/behavioral evidence with stated
   limits, not mapped-memory proof.
4. Causation scope → the A/B proves the deployed *bundle* (relocator +
   4 permits) sufficient; per-target necessity is NOT isolated (no
   per-permit ablation was run).
5. Confound claim → enumerated-channel verification (diagnostics
   sentinel, VNC path, iOS twin, teardown-fallback, stale-buffer), not
   an exhaustive zero-confound guarantee.

## Part 2 — Next input acceptance plan

### Axis separation (required)

Two independent input axes exist in the current design; conflating them
was the pre-D033 error. They must be accepted separately.

- **Axis A — exact AppInput delivery** (app-scoped injection; bypasses
  WindowServer event policy entirely): Host/UIKit or injector →
  datagram → macwsinputd → per-PID endpoint socket → AppInputBridge
  contract → `-[NSApplication sendEvent:]` → visible consequence in the
  app. **Fully implemented and instrumented; never end-to-end exercised
  in a witnessed run.** This is the primary axis.
- **Axis B — global `SLSPostEventRecord` policy path** (SkyLight → WS
  event dispatch): transport proven live (mach_msg succeeds, WS replies
  its own code); every record class probed returns **1002** — a WS-side
  verdict, not a dead channel (10-13 dead-end map §4.2-4.9). The gate
  chain `CGXSenderCanSynthesizeEvents`→impl returns genuine 0;
  `tccPostEventService()->requestAccess()` is also in the path. Do NOT
  attempt to force acceptance — no TCC override, no constant-return
  hook, no identity spoof (per patch discipline + this packet).

### Axis A ladder — exact AppInput (existing capability, no rebuild)

All hops already emit named witnesses; the acceptance run only needs to
*read* them in order plus one pixel consequence.

| Rung | Witness | Where |
|---|---|---|
| A0 precondition | visible desktop + Host scene active (`uiopen macwshost://` done), one macOS app with a text-capable window | D037 state; launch Terminal via existing macPad 控制中心「终端」button — the button press itself emits an ActivateTarget/ConfigureWindow **APP-CONTROL exact-PID record**, so a successful Terminal launch already exercises rungs A1-A3 |
| A1 record produced | Host `MacWSSendInputRecord` send result + perf monitor, OR injector `sent=84` line | `MacWSHost/main.m:8320-8325`; `misc/macws_inject_record.py` (one-shot, wire format `include/macws_host_protocol.h:446`, 84-byte packed) |
| A2 inputd recv + route | `POLL-TIMEOUT` cadence stops; `MACWS-INPUT` route/APP-CONTROL witness, or `REJECT` with full field dump | `macwsinputd/main.c:1555-1650` (recv), `:1634` (REJECT), `:1694`+ (APP-CONTROL) |
| A3 endpoint delivery | `SendToAppInputBridge` errno=0 → `/private/tmp/macws_app_input.<pid>.sock`; `AppInputBridgeEndpointExists` + TARGET-PROBE nonce answered | `macwsinputd/main.c:942-987`, `:1325` |
| A4 bridge accept | contract `MacWSAppInputRouteClassify` accepts (ProcessScoped/ExactWindow/GlobalSystemSurface), not a named Reject class | `include/macws_app_input_route.h`; `AppInputBridge.m` `MacWSInputRecordIsValid` |
| A5 in-app injection | `-[NSApplication sendEvent:]` swizzle fires (`MacWSAppInputApplicationSendEvent`, installed :3116-3126, "sendEvent: display-settle" log); `windowWithWindowNumber:` resolves non-nil for windowed kinds | `AppInputBridge.m:2655` |
| A6 visible consequence + sequence | InputLab `/tmp/macws_inputlab_events.jsonl` records the event with timestamp+latency, OR Terminal echoes the injected key → next CARenderServer capture (`macws_ipados_capture`) shows the character; displayd sequence advances | `misc/InputLab/main.m` (purpose-built receiver: logs every NSEvent with timestamp+delivery-latency); capture tool from D037 |
| A7 timestamp correlation | record.timestamp ↔ inputd `InputUptimeSeconds` ↔ InputLab event.timestamp ↔ displayd sequence ↔ capture wall-time — one table per test record | existing log fields |

Test selection (existing assets, pick ONE): **InputLab** is the
structured receiver (jsonl per-event record — strongest witness);
**Terminal** gives the visible text consequence in the capture PNG
(char echo). Recommended minimal run: launch Terminal via control-panel
button (A0-A3 free), `macws_inject_record.py keydown … --target <term
pid> --window <id>` ×2 (down+up for one char) → verify A4-A7. The
`tap`/`activate` kinds exist for pointer-side follow-up; keep the first
acceptance single-kind, single-target, single-variable.

Failure-mode map (all named, none new): REJECT (field dump names the
bad field), endpoint absent (errno path), contract reject class
(RejectMagic/…/RejectGlobalOwner), `windowWithWindowNumber:` nil,
sendEvent swizzle absent (means libmachook didn't load in the app —
check LC_LOAD_DYLIB delivery), no pixel consequence (event delivered
but not rendered — separates injection from presentation).

### Axis B ladder — global path (characterize the denial, don't force)

| Rung | Requirement | Tool/status |
|---|---|---|
| B1 | Raw explicit reply **dictionary** — the full MIG reply payload for each record class, not just `w19`=rc | `misc/macos_ws_event_probe.c` exists (committed 10-13; build/deploy recipe in header); extend to hexdump the complete reply record |
| B2 | Exact WS error semantics — which WS-side check authors 1002: `tccPostEventService()->requestAccess()` denial, `CGXSenderCanSynthesizeEvents` impl return, or connection-state gate | RE of the reply-construction site in SkyLight (`SLSPostEventRecord` @ `0x185156788` already mapped); `#### CGXSYNTH` hook telemetry shows the gate sees populated `gCurMsgAudit` (52-byte creds) |
| B3 | Functioning-control-channel baseline | `CGWarpMouseCursorPosition` proven working (600,500 moved+readback) — bounds the dead surface to the *event* path, not all control |
| B4 | Acceptance bar | global path may only be claimed accepted with a record that receives a non-1002 WS reply AND produces a WS-visible consequence; 1002 with identified authoring check = characterized denial (a valid negative result, not a fix prompt) |

### Axis B addendum — gate interior now RE-verified (this review, capstone on `analysis/SkyLight_22F82`, uuid `96676a53…d1880`)

Backend's prep-note claims verified byte-exact, with additional detail:

- `_XPostEventRecord` @ +0x388e64: `bl CGXSenderCanSynthesizeEvents` @
  +0x388f08 → `cbz w0,#0x388f94` → **`mov w0,#0x3ea`** → `str w0,
  [x19,#0x20]` — 1002 written into the MIG reply rc field. Authoring
  chain confirmed (rung B2's first half — the *site*).
- A **second error class** exists at +0x388ea0/+0x388f48:
  `mov w0,#-0x130` (−304) for structural failures — the msgh-bit check
  and the payload-size consistency check (+0x388ebc-0x388ed8, incl. a
  0x4000 max and `(size+0x3b)&~3` alignment equality). Every observed
  probe returned 1002, so records pass structure and fail the gate
  specifically.
- **Gate interior (new this review, symbol-resolved)** — three
  sequential decision points: (1) `ldr x8,[0x53ac11b8]` container
  (the per-message audit block), `cbz` → deny before creds examined;
  (2) `[x8+4] ≥ 0x34` size → deny; (3) copy creds `[x8+0x14..0x33]`
  + static ctx → `_WSAuditTokenHoldsCachedProcessRight` (+0x3407ec)
  with PAC-signed callback `_audit_token_check_tcc_access` (+0x340da0)
  → real verdict. The impl is a **cached-right + TCC-fallback**
  structure: audit-token→identity conversion + owner fast-path +
  capability bitmask tests (`[ctx+0x18]`/`[ctx+0x10]`); on miss it
  `blraaz`es the signed TCC callback and caches allowed rights into
  `[0x53ac1b40]`; a cache-miss arm builds a PAC-signed async
  continuation — the TCC request can be **asynchronous**.
- **Correction to my earlier hypothesis**: R002 telemetry showed the
  chain reaching `0x1854257ec` — file offset +0x3407ec = exactly
  `WSAuditTokenHoldsCachedProcessRight`. Decision points (1)+(2)
  therefore PASS at runtime (`gCurMsgAudit` populated = consistent),
  and the denial is produced by the *real verdict* — sender lacks the
  cached synthesis right AND the TCC PostEvent check denies. The 1002
  is a genuine authorization failure, not a structural dead-subsystem
  early-out. THEORIES updated accordingly: remaining unknowns are
  which right-name/service the ctx block encodes and whether a real
  user TCC grant (via the R007-R008 dictionary-reply path) could ever
  populate the cache for the sender identity in chroot.
- Corollary for B1: the reply rc is a plain int32 at reply+0x20
  (`str w0,[x19,#0x20]`) — the "raw explicit reply dictionary" is the
  MIG reply body; the existing probe already extracts w19=rc but a
  full hexdump confirms no additional denial-reason fields.

### Likely producing-layer next step

- **FACT**: Axis A's chain is complete in source and instrumented at
  every hop; no capability is missing. The next producing-layer step is
  a **bounded input A/B generation**: visible desktop + Terminal +
  `macws_inject_record.py` driving one exact key record, reading the
  A1-A7 witness ladder. Tool paths: `misc/macws_inject_record.py`
  (emit), `misc/InputLab` (structured sink), `macws_ipados_capture`
  (pixel consequence), inputd/displayd stderr (route witnesses).
- **THEORY** (ranked, with disconfirming evidence):
  1. First blocker most likely at A3/A4 — endpoint absent or contract
     reject if the deployed libmachook predates the contract in the
     target app (check endpoint socket existence first — one `ls`).
  2. POLL-TIMEOUT during the A/B run would mean records never reach
     inputd — then the producing layer is Host-side emission (MetalView
     emit only fires on real UIKit touches; synthetic tests must use
     the injector, not wait for touches).
  3. Axis B's 1002 most probably authors in the sender-synthesize gate
     chain (returns genuine 0 under populated audit creds — R002), not
     TCC; the B1 reply dump decides — THE THEORY, pending the extended
     probe.
- Explicitly NOT next: TCC overrides, constant-return hooks, identity
  spoofing, per-permit ablation of the display bundle (out of scope),
  or rebuilding the injector/bridge/endpoint stack (all exist).

## Constraints honored

No device/build/source/Git mutation this round; no backend artifact
edits; no TCC/validation bypass proposed anywhere; every claim cites
current source lines or saved evidence; rejected approaches named per
patch discipline.

(handover to codex now)
