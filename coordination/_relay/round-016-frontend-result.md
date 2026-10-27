# Frontend R016 result — reopen freshness audit and bounded witness

Date: 2026-10-18. Read-only offline audit. No device/SSH, no source/test/build-
input edit, no build, no dependency install, no Git write. Added only this
result and the dated evidence
`docs/evidence/r016-application-reopen-freshness-audit-20261018.md`.

Candidate HEAD: `c97e219`.

## Source-confirmed behavior

`RequestApplicationReopen` (`macwshostd/main.m:1655-1689`) reads
`previousGeneration = ReadWindowMetricsGeneration(pid)`, sends one
`MacWSInputKindReopenApplication` record to the exact PID endpoint, then waits
for a valid metrics header with `generation > previousGeneration` (unless the
previous value is 0) and at least one entry carrying
`MacWSStreamWindowVisible`. The app handler
(`libmachook/AppInputBridge.m:10137-10342`) performs the real AppKit
`kAEReopenApplication` lifecycle; after the nested 150 ms + 250 ms dispatch it
**unlinks the sidecar, republishes metrics, and increments the generation even
if the window entries are unchanged** (`:10330-10342`, producer `:13808-13852`).
That forced generation is the liveness/response handshake; no reply datagram
exists.

Logs: `/var/mobile/Library/Logs/MacWSHostd.log` (timestamped; `.previous`
rotation) contains `application-reopen pid=… sent=… errno=…` and
`result=… previous-generation=G0 current-generation=G1 exit-status=…`. The
sidecar is `/var/mnt/rootfs/private/tmp/macws_window_metrics.<pid>.bin`; decode
with `misc/macws_window_metrics_dump.py --pid`. Optional app-side logs
(`#### APP-INPUT REOPEN…`) require the narrow, cached marker
`/var/mnt/rootfs/tmp/macws_app_input_diagnostics`; Host/metrics remain primary.

## Finding: generic visible-window reuse is not a freshness witness

For an existing non-document app, `LaunchRootExecutable` first accepts a
currently visible metrics entry after 0.15 s and logs `launch-app reuse`
without sending any reopen request (`main.m:3880-3882`). Only if that wait
fails does it call `RequestApplicationReopen(existingPID, 1.0)` (`:3887-3893`).
`reopenLifecycle` apps (System Settings, Maps) always call the 8.0 s reopen
path (`:3863-3882`), so the strict generation check applies there. Therefore:

- a generic app with an old visible sidecar is **not** evidence of the new
  reopen contract; no request was sent;
- use a genuinely windowless generic process (valid zero-entry G0 > 0) or a
  `reopenLifecycle` app for the strict witness;
- failure of a non-document windowless reopen leads to
  `TerminateWindowlessRootExecutable` (TERM, then KILL after 400 ms) and a new
  process (`:3894-3904`, `:2648-2695`), which is not success.

## Document-bearing failure rule

`documentOpenPending == YES` never calls `RequestApplicationReopen` and never
calls the windowless retire helper (`main.m:3841-3860`). If the AppInput
endpoint is not ready it logs `launch-document reuse ... result=no-appinput-
endpoint` and leaves the process open. For a newly spawned document process,
endpoint failure clears active state and starts a child reaper but does not
kill the just-spawned process (`:4022-4042`). A document-bearing process must
remain open on failure; no `windowless-retire`/KILL for its PID. Do not use
document-open mode to test reopen freshness.

## Test / runtime status

- Test-covered: metrics V2/V3 ABI/validity (`misc/macws_protocol_test.c:353-
  397`, `misc/test_window_metrics_dump.py`), initial launch admission event
  race (`misc/test_app_launch_admission.py`, which stubs
  `RequestApplicationReopen`), route contract.
- Not test-covered: the `previousGeneration`/`currentGeneration` comparison,
  previous=0 behavior, stale-visible/missing-response failure, document
  preservation.
- Not device-runtime-accepted: no raw `application-reopen`/`previous-
  generation`/`window-ready` Host or app logs for the current source were found
  in `docs/evidence`, `coordination/artifacts`, or `DEVIN_STATUS.md`. Source
  comments and the evening handoff state the requirement; neither is current
  acceptance.

## Bounded next-window witness

**Primary — Terminal, valid zero-entry generation:**
1. Record exact PID + executable
   `/System/Applications/Utilities/Terminal.app/Contents/MacOS/Terminal` +
   start time. Before the action read metrics:
   `generation = G0 > 0`, `entry_count = 0`/no visible entry, raw sidecar SHA.
2. Record the byte offset of `MacWSHostd.log`; parse only new lines.
3. Trigger one ordinary Host launch/reuse of Terminal. Require the request
   line `application-reopen pid=<PID> sent=YES errno=0`.
4. Success requires: Host result `result=window-ready previous-generation=G0
   current-generation=G1 exit-status=-1` with `G1 > G0`; same PID/executable/
   start time (no duplicate); a new sidecar read with `G1 > G0`, at least one
   visible entry (nonzero `window_id`, owner PID from filename); no
   `windowless-retire`/KILL for that PID.
5. Optional app-side correlation: narrow AppInput diagnostics marker created
   before the app's first diagnostics call; expect `#### APP-INPUT REOPEN`,
   `REOPEN-COMPLETE`, `REOPEN-WINDOW`. If the app was already running with
   diagnostics off, do not restart it just for logs.

**Cross-check — System Settings:** for an existing process,
`reopenLifecycle=YES` always uses the 8.0 s reopen path; the new-process path
also calls reopen with its launch timeout. Same G0/G1 + visible predicate plus
`system-settings content result=settings-extension-ready`. Do not use as the
first pixel/input acceptance.

**Document failure control:** on a natural document-open endpoint failure,
require `launch-document reuse ... result=no-appinput-endpoint` and PID still
open; no termination/duplicate. If no failure occurs naturally, report the
source rule as unexercised; do not induce it.

## Failure rule (not success)

`sent=NO` / `result=no-appinput-endpoint` / `result=no-visible-window` /
`exit-status>=0` / `G1 <= G0` / no visible entry / duplicate or replaced PID
are failures. A stale visible sidecar, an old window, PID uptime, app-side log
alone, or `launch-app reuse` on a generic visible app is not success. A
document-bearing process must remain open on failure; no name-based kill or
bundle-ID exception.

## Limits

No request ID/ack: correlation is PID + Host log order + G0→G1 + timing +
optional app-side logs; `sampleSequence` is not in the sidecar. A previous
generation of 0 disables the strict comparison in source, so require G0 > 0 or
app-side REOPEN evidence. Diagnostics caching, the 1.0 s generic timeout
(including 150+250 ms dispatches), and the 0.15 s visible shortcut are
recorded in the evidence note. Visible metrics do not prove pixel/input.

Full detail:
`docs/evidence/r016-application-reopen-freshness-audit-20261018.md`.

(handover to codex now)
