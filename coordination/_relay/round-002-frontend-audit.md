# Round 002 — AppInput route audit (read-only)

Date: 2026-10-18. Role: frontend. Scope: independent review of the exact
PID/window route contract; no source, test, build-input or device change was
made. The only file written by this audit is this report.

Candidate HEAD: `d3c4ecf` (“docs: record final frontend provider/route test
results”). The audited files (`include/macws_host_protocol.h`,
`include/macws_app_input_route.h`, `libmachook/AppInputBridge.m`,
`misc/test_app_input_route_contract.py`, `macwsinputd/main.c`) are unchanged
in the working tree; the unrelated backend modifications (`layout/*`,
`misc/test_tccd_lifecycle_contract.py`, TCC docs, `MTLCompilerBypassOSCheck/`)
were not read as audit input and were not touched.

## 1. Commands and exits

| # | Command | Exit | Result |
| --- | --- | --- | --- |
| 1 | `git log --oneline -3; git status --short` | 0 | HEAD `d3c4ecf`; audited files clean |
| 2 | `python3 -m unittest -v misc.test_app_input_route_contract` | 0 | 3 tests OK |
| 3 | `cc -std=c11 -Wall -Wextra -Werror -Iinclude /tmp/macws_route_audit.c -o /tmp/macws_route_audit` | 0 | out-of-tree probe built |
| 4 | `/tmp/macws_route_audit` | 0 | output in §2 |

No package build, signing, deployment, device command or network operation was
run. The probe source was written to `/tmp`, not the repository.

Probe output (verbatim), `kind win flags -> disposition (name) windowOut`:

```text
kind=15 win=0 flags=0x40 -> 2 (Global) windowOut=0
kind=16 win=0 flags=0x40 -> 2 (Global) windowOut=0
kind=15 win=0 flags=0 -> -6 (RejectWindowRequired) windowOut=0
kind=16 win=0 flags=0 -> -6 (RejectWindowRequired) windowOut=0
kind=27 win=0 flags=0x40 -> -7 (RejectGlobalOwner) windowOut=0
kind=25 win=0 flags=0x40 -> 2 (Global) windowOut=0
kind=17 win=77 flags=0 -> 1 (ExactWindow) windowOut=77
kind=24 win=3735928559 flags=0 -> 1 (ExactWindow) windowOut=3735928559
kind=7 win=0 flags=0 -> 0 (ProcessScoped) windowOut=0
kind=26 win=0 flags=0 -> 0 (ProcessScoped) windowOut=0
```

## 2. Contract under audit

`include/macws_app_input_route.h`:

- 97–102: `RelativePointer` — rejects global surface, requires nonzero window.
- 104–109: `SystemGesture` — requires global surface AND Dock endpoint.
- 111: generic `globalSurface -> GlobalSystemSurface` (checked **before** the
  exact-window requirement).
- 112–115: `KindRequiresExactWindow` (ConfigureWindow, CloseWindow,
  RelativePointer) rejects a zero window only after line 111.
- 68–72: exact-window kind set.

`libmachook/AppInputBridge.m`:

- 5107–5113: `MacWSInputRecordIsValid` calls the contract first.
- 5114–5122: ConfigureWindow payload check no longer repeats the window-ID
  test; CloseWindow returns YES after the contract.
- 5174–5179: generic tail requires `kind <= PerformQuit` (25).

## 3. Per-kind matrix

Route classification is for the kind’s normal production shape; payload and
consumer citations are source facts. “Exact/Process/Global” are the three
accepted dispositions from the contract.

| Kind | Value | Route (normal) | Payload validation | Actual consumer / dispatch | Verdict |
| --- | ---: | --- | --- | --- | --- |
| TouchDown | 1 | Exact (window) / Global (window 0 + flag) / Process (RFB window 0) | generic tail: coords in frame, kind≤25 | main-thread pointer path; `MacWSNSEventType` 4673; event build 11888–11906 | preserved |
| TouchMove | 2 | same | generic tail | pointer path; type 6 (4674) | preserved |
| TouchUp | 3 | same | generic tail | pointer path; type 2 (4675) | preserved |
| TouchCancel | 4 | same | generic tail | pointer path; type 2 (4676) | preserved |
| Hover | 5 | same | generic tail | pointer/menu path (type 5, 4677); Dock leave record 2267–2278 | preserved |
| Tap | 6 | same | generic tail | atomic down/up path 11922+; 7DTD expansion 9843–9868 | preserved |
| TargetProbe | 7 | Process (record form) | generic tail accepts (kind≤25) | **no record consumer in AppInputBridge**; broker consumes it at `macwsinputd/main.c:1737–1745` before sending | pre-existing gap, see F2 |
| ActivateTarget | 8 | Process (window 0) / Exact if windowed | generic tail | `AppInputBridge.m:9921`; hostd sender 3571–3588 | preserved |
| DeactivateApplication | 9 | Process | generic tail | `AppInputBridge.m:9875`; broker control 1044 | preserved |
| MenuHover | 10 | Exact/Global/Process | generic tail | menu/tracking pointer path 12213+ | preserved |
| KeyDown | 11 | Exact or Process (global keyboard proxy) | generic tail (no keycode range check) | `AppInputBridge.m:10680`; `MacWSPostKeyRecord` 7438 | preserved (pre-existing payload leniency) |
| KeyUp | 12 | same | generic tail | same | preserved |
| SecondaryTap | 13 | Exact/Global/Process | generic tail | atomic right-click path 11922+ | preserved |
| Scroll | 14 | Exact/Global/Process | finite pressure/horizontal, ≤16384 | `MacWSPostSystemScrollEvent` 11583; App scroll 11607+ | preserved |
| ConfigureWindow | 15 | Exact | geometry/scale (5114–5120) | `AppInputBridge.m:10431`; `MacWSWindowWithNumber` nil for 0 (8341–8342) → CONFIGURE-DROP | **flag bypass F1** |
| CloseWindow | 16 | Exact | returns YES (5121) | `AppInputBridge.m:10348`; nil window → no-op | **flag bypass F1** |
| CreateInitialWindow | 17 | Process; **Exact if sceneID nonzero** | returns YES | 10089; performs enabled Command-N menu item | taxonomy note F3 |
| ReopenApplication | 18 | Process | returns YES | 10137; real delegate reopen | preserved |
| Magnify | 19 | Exact/Process | finite pressure, phase | 11700+ `_reallySendEvent:` | preserved |
| DesktopCommand | 20 | Process | contactID∈[3,4] | 10634; CGS key post | preserved |
| SystemGesture | 21 | Global + Dock only | axis/contact/pressure/altitude/phase (5160–5173) | Dock RX 6844 → `MacWSPostDockSystemGesture` 6568; non-Dock rejected at route 104–109 | preserved |
| Rotate | 22 | Exact/Process | finite pressure, phase | 11700+ | preserved |
| PerformPaste | 23 | Process or Exact | generic tail | 10639; exact window if present, else real keyWindow | preserved |
| OpenDocuments | 24 | Process; **sceneID is a nonce** | `sceneID != 0` (5125) | 9870 → `MacWSHandleOpenDocuments` 9480; sidecar nonce 9368+ | taxonomy note F3 |
| PerformQuit | 25 | Process | returns YES | 9788 → `MacWSHandlePerformQuit` 9622; socket lifecycle branch | preserved |
| ModifierSnapshot | 26 | Process (route) | **rejected by generic tail** (`kind≤25`, 5175–5176) | never sent to AppInput: broker forwards to VNC proxy (`macwsinputd/main.c:1667–1672`); `mac_hooks.m:8968/9335` consumes | correct fail-closed, note F4 |
| RelativePointer | 27 | Exact only | source finger/indirect, deltas, coords | 11345; process-local relative event, no global cursor | preserved |

## 4. Findings

### F1 — FACT (defect): `GlobalSystemSurface` bypasses the exact-window requirement

The generic global branch (route header 111) runs before the exact-window
requirement (112–115). Therefore:

```text
ConfigureWindow, window=0, flag=GlobalSystemSurface -> accepted (Global)
CloseWindow,     window=0, flag=GlobalSystemSurface -> accepted (Global)
```

whereas the same records without the flag are rejected as
`RejectWindowRequired`. This is directly reproduced by the probe in §1
(kind 15/16 rows). It is a behavior change from `3c4d581`: the pre-refactor
`MacWSInputRecordIsValid` tested the window ID unconditionally, and
`macwsinputd` still requires a nonzero window for both kinds
(`macwsinputd/main.c:267–277`).

Current impact is fail-safe, not destructive:

- `MacWSWindowWithNumber` returns nil for 0 (`AppInputBridge.m:8341–8342`), so
  ConfigureWindow logs CONFIGURE-DROP (10431+) and CloseWindow performs no
  action (10348+).
- For non-Dock endpoints the flag has no other effect because both kinds
  return before the pointer/global branch. At Dock, `MacWSPostDockSystemInput`
  rejects non-pointer kinds via `default: return NO`
  (`AppInputBridge.m:6840–6858`), then the record is queued
  (13226–13229) and the same nil-window path runs.
- Reachability of a crafted record requires the endpoint’s socket path; it is
  bound with `chmod 0600` (`AppInputBridge.m:14102–14127`), so the writer is
  same-UID. Direct senders exist (`macwshostd`, `macwsworkspacectl`,
  `mac_hooks.m:14833`), but none of the inspected senders sets
  `GlobalSystemSurface` on ConfigureWindow/CloseWindow. **THEORY:** no
  production path currently emits the invalid combination.

The stated invariant (“an arbitrary GlobalSystemSurface flag cannot widen an
unrelated kind”; “exact-window kinds reject missing IDs”) is nevertheless
false at the contract boundary.

Minimal proposed fix (not applied): move the exact-window requirement ahead of
the generic global branch and reject the flag there, e.g.

```c
if (MacWSAppInputRouteKindRequiresExactWindow(record->kind)) {
    if (globalSurface) return MacWSAppInputRouteRejectGlobalOwner;
    if (windowID == 0) return MacWSAppInputRouteRejectWindowRequired;
    return MacWSAppInputRouteExactWindow;
}
if (globalSurface) return MacWSAppInputRouteGlobalSystemSurface;
```

Optionally also gate `GlobalSystemSurface` to the kinds that actually produce
it (`IsSystemPointerKind` set in `macwsinputd/main.c:214–228`, plus Scroll and
SystemGesture), rejecting it for all control/process-scoped kinds. That would
also cover `PerformQuit` (probe row kind=25 → Global), which currently only
changes taxonomy, not observable dispatch.

Test gap (FACT): `misc/test_app_input_route_contract.py` covers
ConfigureWindow/CloseWindow **without** the flag (lines 109–117), relative
pointer + flag (119–132), and SystemGesture scoping (135–143), but has no
ConfigureWindow/CloseWindow + flag + window 0 case. Adding those two assertions
would have caught F1.

### F2 — FACT (pre-existing, not from this packet): record-form TargetProbe is accepted but has no AppInput consumer

`MacWSInputRecordIsValid` accepts kind 7 through the generic tail
(5175–5179), and `MacWSNSEventType` returns 0 for it (4681–4699). If an
84-byte record of kind 7 reaches an AppInput endpoint, the main-thread tail
builds a mouse event with type 0 (11888, 11903–11906) or drops it if creation
fails. In the production broker, TargetProbe is intercepted and handled at
`macwsinputd/main.c:1737–1745` before any `SendToAppInputBridge`, and the
32-byte probe path uses `MACWS_TARGET_PROBE_MAGIC` (AppInputBridge 13260+),
not this kind. **THEORY:** reachable only by a same-UID direct sender.
Minimal hardening: reject kind 7 in `MacWSInputRecordIsValid` (or handle it
explicitly) so acceptance and consumer sets match.

### F3 — FACT (taxonomy, no behavior change): non-window kinds can classify as ExactWindow

`CreateInitialWindow` with a nonzero `sceneID` classifies `ExactWindow` (probe
kind 17), although its consumer (10089) ignores the window and performs the
native Command-N action. `OpenDocuments` carries a hostd nonce in `sceneID`
(`macwshostd/main.m:4566–4583`; consumer 9368–9383), not a CGWindowID; the
probe shows an arbitrary nonce can classify `ExactWindow` (kind 24).
Neither consumer invents window ownership or resolves the decoded number, so
the current behavior is unchanged. **THEORY:** if a future consumer trusts
`outWindowID` for these kinds, it would act on a non-window value. Minimal
fix: classify inherently process-scoped kinds (`CreateInitialWindow`,
`ReopenApplication`, `OpenDocuments`, `PerformQuit`, `DesktopCommand`,
`ModifierSnapshot`, `TargetProbe`, `ActivateTarget`/`DeactivateApplication`
when they carry no window) by kind before decoding `sceneID`.

### F4 — FACT (correct fail-closed): ModifierSnapshot

The route stage accepts kind 26 as ProcessScoped/Exact, but
`MacWSInputRecordIsValid` rejects it via `kind <= PerformQuit` (5175–5176).
The broker never sends it to AppInput; it forwards to the VNC/session proxy
(`macwsinputd/main.c:1667–1672`), and `mac_hooks.m:8968/9335` consumes it.
The two-stage result is correct; only the contract header, if used alone,
would over-accept. Not a defect in the current call path.

## 5. Required checks

| Check | Result | Evidence |
| --- | --- | --- |
| Arbitrary `GlobalSystemSurface` cannot widen an unrelated kind | **FAIL** | F1; route header 111 vs 112–115; probe kind 15/16 rows |
| Exact-window kinds reject missing IDs | **PASS without flag / FAIL with flag** | route header 112–115; probe kind 15/16 rows |
| Process-scoped kinds do not invent window ownership | **PASS** | consumers 10089, 10137, 10348, 10431, 10639, 11345 use real AppKit objects; `MacWSWindowWithNumber(0)=nil` 8341–8342; no NSWindow fabrication found in these paths |
| Dock-only gestures remain scoped | **PASS** | route header 104–109 rejects non-Dock; payload 5160–5173; broker validation `macwsinputd/main.c:322–337`; consumer 6844/6568 |
| Existing per-kind semantics preserved | **PASS except F1** | matrix §3; pre/post `MacWSInputRecordIsValid` comparison 5107–5179 |

## 6. Limits

- No device, WindowServer, Dock or AppKit run; all conclusions are source
  facts plus a host-only pure-C route probe.
- Reachability statements are static; no socket was opened and no record was
  injected.
- The focused test run recompiles its embedded C into a temporary directory
  only; no package/build input was touched.
- **THEORY** items are labeled; they are not runtime-confirmed.
- The backend’s uncommitted files were outside this audit and were not read as
  evidence.

(handover to codex now)
