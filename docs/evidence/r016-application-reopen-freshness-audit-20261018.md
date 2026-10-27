# R016 — application reopen freshness audit and next-window witness

Date: 2026-10-18. Read-only audit of existing source/tests/evidence. No
device/SSH, no source/test/build-input edit, no build, no dependency install,
no Git write. This note and the R016 result are the only additions.

## 1. Files inspected

| File | Relevant lines |
| --- | --- |
| `macwshostd/main.m` | `1427-1442` generation read; `1444-1502` generation-gated wait; `1655-1689` `RequestApplicationReopen`; `3841-3860` document reuse branch; `3861-3904` existing-app reuse/terminate; `4022-4042` new document process; `2648-2695` windowless retirement |
| `libmachook/AppInputBridge.m` | `10137-10342` ReopenApplication handler and forced generation; `13665-13852` metrics producer; `945-960` diagnostics marker/caching; `14094-14096` sidecar path/unlink |
| `include/macws_stream_protocol.h` | `375-430` metrics header/entry/validity; `102` visible flag |
| `include/macws_host_protocol.h` | `182-186` ReopenApplication kind; `278-288` wire version |
| `include/macws_app_input_route.h` | `61-126` process-scoped route and exact PID/window rules |
| `misc/test_app_launch_admission.py` | initial-window loop test; `RequestApplicationReopen` stub at `:74`, not generation freshness |
| `misc/test_window_metrics_dump.py`, `misc/macws_protocol_test.c:353-397` | metrics ABI/validity, not reopen generation |
| `misc/macws_window_metrics_dump.py` | bounded read-only sidecar decoder |
| `docs/porting/ventura-22F82-evening-handoff-20261015.md:172,181` | P0 requirement: this reopen response + new generation; old visible metrics insufficient; retain document-bearing process on failure |
| `coordination/reviews/share-update-priorities-20261018.md:11` | reuse existing invariant; stale visible metrics insufficient |

## 2. Source-confirmed request/response chain

### Request

`RequestApplicationReopen(pid, timeout)` (`macwshostd/main.m:1655-1689`):

1. `WaitForAppInputEndpoint(pid, MIN(timeout,5))` checks only that
   `/var/mnt/rootfs/private/tmp/macws_app_input.<pid>.sock` exists
   (`:1555-1568`). Path presence is not a response; a stale socket is caught
   only by `sendto` failure.
2. Reads `previousGeneration = ReadWindowMetricsGeneration(pid)`
   (`:1427-1442`). A missing/invalid sidecar returns 0.
3. Sends one `MacWSInputRecord` with
   `kind = MacWSInputKindReopenApplication`, `targetPID = pid`,
   `frameWidth/frameHeight = 1`, source unknown, and a Host-local
   `sampleSequence` (`:1660-1673`). The wire version is the legacy version
   (`include/macws_host_protocol.h:278-288`); the route is process-scoped with
   exact target PID (`include/macws_app_input_route.h:118-126`).
4. Logs `application-reopen pid=… sent=%@ errno=%d` (`:1674-1676`). A
   datagram send is not a response.
5. Waits with `WaitForWindowMetricsFlagsAfterGeneration(pid, timeout,
   MacWSStreamWindowVisible, previousGeneration, &exitStatus)`
   (`:1678-1680`) and logs
   `application-reopen pid=… result=… previous-generation=… current-generation=…
   exit-status=…` (`:1681-1686`).

### Wait/freshness predicate

`ReadWindowMetricsGeneration` (`:1427-1442`) requires a regular sidecar whose
header passes `MacWSWindowMetricsAreValid` and returns
`header.generation`; otherwise 0.

`WaitForWindowMetricsFlagsAfterGeneration` (`:1444-1502`):

- `waitpid(pid, WNOHANG)` returns NO if the target exited;
- reads the sidecar header/entries and validates the V2/V3 layout
  (`include/macws_stream_protocol.h:416-430`);
- requires `minimumGenerationExclusive == 0 || header.generation >
  minimumGenerationExclusive`;
- requires at least one entry whose `flags` contains
  `MacWSStreamWindowVisible`;
- returns YES only when both hold; timeout returns NO.

The Host does **not** require an explicit request ID. `sampleSequence` is
Host-local and is not written into the metrics header or any app reply.

### Producer-side "response"

The app handler (`libmachook/AppInputBridge.m:10137-10342`):

- inspects visible windows; on the main queue constructs the standard
  `kCoreEventClass/kAEReopenApplication` `NSAppleEventDescriptor` and calls
  AppKit `_handleAEReopen:` (`:10173-10210`);
- after 150 ms calls `_doOpenUntitled` if no window is visible
  (`:10212-10245`), then after another 250 ms orders the real key/main
  level-0 window through `makeKeyAndOrderFront:`/`activateIgnoringOtherApps:`
  (`:10245-10328`);
- **forces a new metrics generation**: it unlinks
  `MacWSWindowMetricsPath`, calls `MacWSPublishWindowMetrics()`, and notifies
  the display catalog with `'r'` (`:10330-10342`). The producer writes a new
  header with `generation = ++MacWSWindowMetricsGeneration` even when the
  window entry bytes are unchanged, because the unlinked sidecar no longer
  exists (`:13808-13852`; generation at `:13848`).

Thus a fresh generation is the producer's liveness handshake for this exact
reopen request. If `_handleAEReopen:` is unsupported, the app main queue is
blocked, or the record never reaches the endpoint, no forced generation
occurs and the Host times out.

## 3. Source-level finding: generic visible-window reuse bypasses the strict path

For an existing process, `LaunchRootExecutable` (`:3839-3917`):

- `documentOpenPending == YES`: only waits for the AppInput endpoint; no
  reopen and no termination (`:3841-3860`).
- `documentOpenPending == NO`:
  - `reopenLifecycle` (System Settings, Maps) always calls
    `RequestApplicationReopen(existingPID, 8.0)` (`:3863-3882`), so the strict
    generation check applies.
  - generic apps call only `WaitForWindowMetrics(existingPID, 0.15)` first
    (`:3880-3882`). A currently visible metrics entry is accepted as
    "existing window" and logged as `launch-app reuse` without any reopen
    request or generation delta. Only when that 0.15 s wait fails does the
    generic path call `RequestApplicationReopen(existingPID, 1.0)`
    (`:3887-3893`).
  - if the generic reopen fails, `TerminateWindowlessRootExecutable` sends
    TERM, then KILL after a 400 ms grace (`:3894-3904`, `:2648-2695`), and
    launch continues in a new process.

Consequence for acceptance: **a generic app with a stale visible metrics
entry is not a reopen-freshness witness**; no request was sent. The strict
"this request + new generation" invariant is only exercised by
`reopenLifecycle` apps and by generic apps that are actually windowless at the
0.15 s probe. Do not call `launch-app reuse` on a visible generic app proof of
the new contract.

## 4. Document-bearing failure rule (source-confirmed)

- Existing document-bearing process: `documentOpenPending == YES` never calls
  `RequestApplicationReopen` and never calls
  `TerminateWindowlessRootExecutable` (`main.m:3841-3860`). If the endpoint is
  not ready it logs `launch-document reuse ... result=no-appinput-endpoint`
  and returns NO; the process stays open. If ready it logs
  `launch-document reuse ... endpoint=ready` and returns YES; document delivery
  is a separate `OpenDocuments` transaction, not this reopen freshness path.
- New document-bearing process: if the endpoint is not ready,
  `launch-document process-ready` is not reached; the active PID is cleared,
  a child reaper is installed, and the function returns NO without killing the
  just-spawned process (`main.m:4022-4042`).
- Non-document windowless failure is different: the existing PID is
  terminated and relaunched (`:3894-3904`). That is not the document-bearing
  rule and must not be applied to a document-bearing process.

Therefore the failure rule for this acceptance is: stale metrics, a missing
reopen response, or a timeout is **not success**; a document-bearing process
must remain open, and no `launch-app windowless-retire`/KILL may target it.
Do not add a bundle-ID exception or change lifecycle code.

## 5. Test and runtime evidence status

- **Source-confirmed:** generation read/wait, visible-flag requirement,
  forced unlink+publish after the AppKit reopen lifecycle, endpoint path,
  document branch, termination branch, and log strings above.
- **Test-confirmed:** metrics V2/V3 layout and validity
  (`misc/macws_protocol_test.c:353-397`, `misc/test_window_metrics_dump.py`);
  initial launch admission loop and its 0.15/1.0 s event race
  (`misc/test_app_launch_admission.py`, which stubs `RequestApplicationReopen`
  and does not test `previousGeneration`/`currentGeneration`); route contract
  (`misc/test_app_input_route_contract.py`).
- **Not test-covered:** `RequestApplicationReopen` generation comparison,
  previous-generation-zero behavior, missing-response/stale-visible failure,
  and document-bearing preservation have no focused test in the repo.
- **Not device-runtime-accepted:** a search of `docs/evidence`,
  `coordination/artifacts`, and `coordination/DEVIN_STATUS.md` for
  `application-reopen`, `previous-generation`, `current-generation`,
  `endpoint-driven-reopen`, `window-ready`, `no-visible-window`, or
  `no-appinput-endpoint` found no raw Host/app logs for the current source.
  Source comments cite historical runtime events, and the evening handoff
  states the requirement; neither is current-generation acceptance.

## 6. Bounded next-window witness

Choose one target state; do not rebuild or modify the mechanism.

### A. Primary: Terminal with a valid zero-entry generation

Why: Terminal is an allowed root app, has a simple exact executable, and after
closing its last macOS window can remain alive with a valid zero-entry metrics
header (`main.m:3895-3900` comment; producer publishes `entryCount=0`). This
forces the generic `RequestApplicationReopen` path instead of the visible
shortcut.

1. Identify exact process: PID + executable
   `/System/Applications/Utilities/Terminal.app/Contents/MacOS/Terminal` and
   process start time. Record `ps -p PID -o pid=,lstart=,comm=` (or equivalent
   proc identity) before/after; PID reuse is a failure.
2. Before the request, read the sidecar with
   `misc/macws_window_metrics_dump.py --pid PID`:
   require `generation = G0 > 0`, `entry_count = 0` (or no visible entry), and
   record the raw file SHA-256/mtime. This establishes a valid previous
   generation, not a missing file.
3. Record the byte offset/size of
   `/var/mobile/Library/Logs/MacWSHostd.log` before the action; only parse
   lines after that offset to avoid stale request/result lines. The Host log
   is timestamped epoch (`main.m:305-325`).
4. Trigger one ordinary Host launch/reuse of Terminal. Expected Host request
   line:
   `application-reopen pid=<PID> sent=YES errno=0`.
5. Success requires all of:
   - Host result line
     `application-reopen pid=<PID> result=window-ready previous-generation=G0
     current-generation=G1 exit-status=-1` with `G1 > G0`;
   - same PID and exact executable/start time (no duplicate Terminal PID);
   - a new sidecar read with `generation = G1 > G0`, at least one entry whose
     flags include `visible`, nonzero `window_id`, and owner PID from filename
     equal to the target PID; file written after the request;
   - no `launch-app windowless-retire`/KILL for that PID in the same interval.
6. Optional app-side correlation: create the narrow marker
   `/var/mnt/rootfs/tmp/macws_app_input_diagnostics` **before** the app first
   calls the diagnostics accessor (the code caches it, `AppInputBridge.m:945-
   960`) and read `/var/mobile/Library/Logs/Terminal.host.log` from a recorded
   offset. Expected `#### APP-INPUT REOPEN pid=… route=AppKit-AE
   supported=YES …`, `REOPEN-COMPLETE`, and `REOPEN-WINDOW` lines. If the app
   was already running with diagnostics off, do not restart it just for logs;
   Host log + metrics remain the required witness.
7. Failure: `sent=NO`, `result=no-appinput-endpoint`, `result=no-visible-window`,
   `exit-status>=0`, `G1 <= G0`, no visible entry, or a new/duplicate PID is
   **failure**. Terminal is non-document; its windowless retire/relaunch after
   failure is the documented behavior, not success.

### B. Cross-check: System Settings (always-reopen lifecycle)

For an already-running process, `reopenLifecycle=YES` always sends
`RequestApplicationReopen(pid, 8.0)`, even when a window exists
(`main.m:3863-3882`), and additionally requires a real Settings extension
(`WaitForSystemSettingsContent`, `:3883-3884`). The new-process path also
calls the reopen with its launch timeout (`:4052-4053`). Use an
already-running System Settings PID with a valid G0 sidecar; same success
predicate plus `system-settings content result=settings-extension-ready`.
Do not use this as the first pixel/input acceptance; it is a strict reopen
cross-check.

### C. Document-bearing failure preservation (separate check)

Use an allowed document-bearing app (e.g. Sublime Text or Word) with an
existing document process. The source rule is in §4: a document-open
endpoint failure must return `launch-document reuse ... result=no-appinput-
endpoint` and leave the PID open; it must not produce `launch-app
windowless-retire`/KILL for that PID. If no such failure occurs naturally,
report the source-confirmed rule as unexercised; do not induce it by killing
the endpoint or adding a bundle exception.

## 7. Correlation limits (must be recorded)

- There is no request ID/ack datagram. Correlation is PID + Host log order +
  generation delta + timing; `sampleSequence` is not in the sidecar.
- `previousGeneration == 0` disables the strict `>` check
  (`main.m:1474-1476`). A zero/missing sidecar is not acceptable freshness
  proof; require G0 > 0 (Terminal zero-entry state) or app-side REOPEN logs.
- A concurrent window change can publish its own generation. Use one quiet
  app, no menu/Dock/other-window action during the interval, log offsets and
  G0/G1, and optional app-side REOPEN logs.
- `MacWSRuntimeDiagnosticsEnabled` caches its answer; a marker created after
  the app's first call may not enable logs. Host/metrics remain primary.
- The generic 1.0 s reopen timeout includes a 150 ms + 250 ms nested dispatch
  chain; a timeout is failure, not a reason to accept an old window. System
  Settings has 8.0 s.
- Endpoint readiness is `access(F_OK)` on the socket path
  (`main.m:1555-1568`); only `sent=YES errno=0` plus a fresh generation/visible
  entry proves the request reached a live handler.
- Metrics generation is per-process and not a frame/input/pixel witness;
  visible metrics do not prove a rendered frame or user interaction.

## 8. Local commands / exits and limits

Read-only `rg`/`sed`/`grep`/`git log` over the listed files exited 0; HEAD is
`c97e219`. No tests, builds, package, device or Git operations were run.

Limits: source/test-confirmed behavior only; no current device runtime
acceptance exists in the repo. The generic visible-window shortcut is a
source-level gap relative to the strict "this reopen + new generation" rule
and must not be counted as acceptance. The document-bearing failure rule is
source-confirmed; a natural runtime failure was not observed.

(handover to codex now)
