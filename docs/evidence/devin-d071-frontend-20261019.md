# D071 frontend — read-only follow-up: install evidence, frame-wait audit, layered ledger

Local read-only. No device mutation/access, no probes, no
build/deploy/restart/trustcache/password/source/Git changes.

## 1. `SENDEROBS-INSTALL` in the current WS generation — evidence ABSENT (missing input)

Searched all locally saved artifacts (`coordination/artifacts/*.json`,
`docs/evidence/`, `analysis/`):

- `SENDEROBS-INSTALL` appears **zero times** in any saved capture —
  no witness of the disp/impl hook installation exists anywhere on
  disk.
- `CGXSYNTH-INSTALL` witnesses exist but are all **prior-generation**:
  `r004b-ws-log-delta.json` (2026-10-14) shows
  `#### CGXSYNTH-INSTALL gate=… isWS=1 diag=1 … permit=0` — the
  *gate* hook installed in an Oct-14 WS generation — plus probe-side
  `isWS=0 … permit=-9` lines (never attempted, probes aren't WS).
  These captures predate the disp/impl instrumentation (built
  D043/D044-era, Oct-19).
- The current WS generation (pid 10447, started 2026-10-19 00:17:23)
  has **no saved install lines** — the hooks arm only inside
  `loadImageCallback` at launch when the chroot sentinel
  (`/tmp/macws_sender_observation`) exists at that moment, and the
  generation's stderr has never been pulled into an artifact.

**Conclusion:** hook residency in the current generation is
**UNKNOWN** — the deciding read is `grep SENDEROBS-INSTALL
/var/jb/var/mobile/WindowServer.err` on device (a previously-approved
read-only log pattern), which requires device access not granted this
packet. Recorded as missing input #1 — exactly the D068 prerequisite.
No inference either way: absent-from-artifacts ≠ absent-from-log.

## 2. Artificial frame waits / service messages / reopen freshness — source audit

### Frame pacing (libmachook, WS-side)

- `macws_coexist_wait_for_completion_slot`
  (`mac_hooks.m:27489`): paces to **completion timestamps**, not fixed
  sleeps — `poll()` on an interaction wake socket shortens the wait
  mid-interval (interaction wake → recompute `activity_pace_us`,
  shrink `target_ns`); `usleep` fallback only when no wake fd or clock
  failure. One synchronous completion per swap is preserved (no
  callback FIFO accumulation) — the 2026-07-28 Chromium-148 55-ms
  rAF regression motivated exactly this design.
- Called pre-lock from `macws_iomfbserver_begin_skylight_update`
  (`:20055`) — moved *outside* the server mutexes after the runtime
  sample `/tmp/ws-magnify.sample` showed 232/266 display-timer
  samples parked in `vsync_callback+76` waiting on the lock while the
  updater held it in the old synchronous SwapEnd poll. Callers also
  at `:27607`, `:27688`.
- Pace selection (`:27354` region): mode ladder
  direct-drawable > interactive > render > idle with per-mode
  `pace_us` floors (`kInteractivePaceUS` etc.); a bounded **10-Hz
  producer wake** signals the activity fd so a relaunched producer is
  rediscovered without filesystem polling on active frames.
- Service-message availability IS handled — the wake socket +
  activity records + generation-gated descriptors are the
  event-driven path the share lesson asks about; the pacing layer is
  the residual artificial wait when no wake arrives.

### Reopen freshness (macwshostd)

- `WaitForWindowMetricsFlagsAfterGeneration` (`main.m:1444`):
  PID-scoped metrics sidecar
  `/var/mnt/rootfs/private/tmp/macws_window_metrics.<pid>.bin`,
  `minimumGenerationExclusive` freshness gate, and the lesson-5
  verdict in code: a launch is reusable **only if a real visible
  AppKit window exists** in the metrics (the Terminal-after-
  scene-discard case — sidecar entries persist while the validated
  catalog has no window). The host-side flow at `:1572` sends the
  native reopen as soon as the input endpoint is published; ~3.05 s
  wait precedent recorded with no visible metrics regression.
- Generation counters are monotonic per object (`:571` region —
  `now*1e9+previous+1`); child reaping is handled by
  `BeginApplicationChildReaper` (`:1502`) so a reused launch can't
  accumulate zombie state.

### Is a runtime wait measurement available? — **NO** (FACT)

Every wait/pace diagnostic emits only under
`macws_runtime_diagnostics_enabled()` — `#### COEXIST pre-lock
completion pace #N: interval=X slept=Y` (`:20056`) and `#### COEXIST
DIAGNOSTIC activity pace: mode=…` (`:27404`). The umbrella sentinel
also activates an alternate display producer — arming it makes any
measurement non-causal. There is **no dedicated non-confounding
sentinel** for frame-wait telemetry (the dedicated sender-obs
sentinel is input/TCC-scoped only; the keyboard-latency sentinel is
input-scoped). In the current generation no sentinel is armed, so no
wait data is being produced. A runtime wait measurement would require
a new dedicated toggle + redeploy — outside this round's scope. The
only wait measurement that ever existed was the historical
`/tmp/ws-magnify.sample` (prior generation).

## 3. Layered acceptance ledger — reconciled

| Layer | Witness status | Evidence |
|---|---|---|
| decode/asset | **witnessed** — wallpaper static-payload substitution (HEIC decodes black in chroot), visible in PNG | D037/D039 captures; `wallpaper=ready` |
| provider (hook/permit path) | **witnessed** — arm64e PAC relocation ALL PASS on live IOMFB/QuartzCore pins; callable-original replay | `devin-d037-gated-live-desktop-20261018.md` |
| producer/final composite | **witnessed, continuous** — cancel-completion → `publish_graphics_ready`/`scanout_after_swap`; state file `ready`/`validated-final-composite`, seq advancing (695→…→6055 same generation) | state file reads across D041–D069 |
| displayd validation | **witnessed** — pid 10540 writes state + `workspace-graph-state` `wallpaper/dock/menubar/backing=ready` | D037 §118, state file |
| Host presentation (MacWSHost 8318) | **witnessed historically** — PNG captures `frame-macos-desktop.png` (2732×2048, nonblack=0.984, visually verified Aqua desktop incl. host panel overlay) + negative-control iPadOS frame | D037/D039; D041 operator note "the desktop the user was looking at" |
| visible pixels — **current generation** | **partial** — operator sighting (D041-era) + state seq; **no fresh PNG/pixel artifact pulled after D041** for gen-10447 | missing witness |

**Only missing witnesses** (not redoing accepted work):

1. A current-generation pixel/frame artifact (PNG or Host readback) —
   last pixel proof predates the 10447 generation's later sequence
   range; state-file semantics are displayd-side claims, not pixels.
2. `SENDEROBS-INSTALL` status for gen-10447 (§1) — one grep on the
   on-device WS stderr log.
3. A causal wait-measurement path — none exists without the
   confounding umbrella; would need a dedicated sentinel + deploy
   (separate authorization).

Everything else in the chain (permits, provider, producer, displayd
validation, AppInput acceptance) is already witnessed — do not redo.
