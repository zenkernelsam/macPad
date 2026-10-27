# D135 — native pixel witness from the live D130 generation (read-only)

**Task**: `coordination/_relay/devin-135-backend-task.md` — one read-only
native pixel capture from the existing live generation; no restart, no VNC
changes, no sentinels, no writes to source/build/package/device state.

## Pre-capture state (all read-only reads)

- `/tmp/macos_gui_start.state`: `pid=21584 phase=ready detail=WindowServer and requested clients are ready` — unchanged from the D130 attempt.
- `/var/mnt/rootfs/tmp/macws_final_composite.state`:
  `state=ready producer=22527 sequence=904` at first read; `sequence=954` at
  capture; `sequence=966` after capture. Producer = live WindowServer
  (`/System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer -virtualonly -daemon`, pid 22527). Same generation as D130 — no restart occurred.
- Host foreground state (from `MacWSHost.log`, read-only): latest
  `scene-occlusion` records for session `A2A3C0E7-…` show
  `foreground=YES backgrounded=NO suspended=NO`, `scene-became-active`,
  `action=keep-live`, and a `runtime-confirmed native Metal present
  frame=2732x2048 source=IOSurface status=4`. MacWSHost (pid 17584, uid
  mobile) was foreground during capture.
- `macws_workspace_graph.state` (`updated=1792420300`): real workspace —
  wallpaper (Dock `Desktop Picture - DefaultDesktop.heic`, surf=171,
  snapdone=1), Menubar (surf=170), StatusIndicator (surf=166), Control
  Center status items, Finder, Terminal window 27 `dest=(0,50 1140x742)`.

## Capture mechanism

Existing on-device tool `dispcap_probe`
(`/var/mnt/rootfs/private/tmp/dispcap_probe`, Oct-12 vintage) — a chroot-side
helper using `CGGetActiveDisplayList` + `CGDisplayCreateImage` +
`CGImageDestination*` to write the composited display to PNG.

Command (exactly one invocation):

```
chroot /var/mnt/rootfs /bin/zsh -c "/private/tmp/dispcap_probe /private/tmp/d135cap"
```

Result: `/var/mnt/rootfs/private/tmp/d135cap_1.png`, 8,790,069 bytes,
written 23:14. Pulled to the Mac over scp, then the device copy was deleted
(screen data preserved locally only, per task).

## Pixel inspection (local file `/tmp/d135cap_1.png`)

- `sips`: 2732×2048 px — exact match to the workspace-graph canvas
  `canvas=2732x2048`.
- `shasum -a 256`: `c981cce82c2e973c8cf223d9846caebf91be65164fb87f016cfb685f0663f87c`
- Pillow: **343,142 unique RGBA colors** — a full photographic render, not a
  black/blank frame.
- Menubar band (0,0,2732,48): 2,262 unique colors — antialiased text glyphs.
- Wallpaper center sample mean RGB ≈ (118,128,115) — green/sage foliage.

### Visual content

The image shows a real Ventura Aqua desktop: Apple logo + menubar reading
`Terminal Shell Edit View Window Help` with status icons top-right, and the
full-canvas green lake/forest `DefaultDesktop.heic` wallpaper owned by Dock —
matching the workspace-graph layer identities. **This is direct pixel
evidence of a composited visible desktop**, closing the D130 layer-5 gap
within the no-VNC constraint.

Note: `CGDisplayCreateImage` returns WindowServer's composited display image —
the authoritative macOS desktop pixels that displayd publishes to Host
(`final-composite-received producer=22527 sequence=1 surface=157
size=2732x2048` in `macwsdisplayd.err`). The iPad framebuffer itself has no
installed CLI capture tool; this is the strongest native pixel witness
available without changing display mode.

## Producer/sequence correlation

Composite state read immediately before and after the single probe run:
`sequence=954 → 954` during the capture second, `producer=22527` constant,
advancing to `966` on the final read — same live WindowServer generation as
D130 throughout.

## Surf-ID lookup limitation (documented, not worked around)

`iosurf_probe2 <id>` and `surfdump <id> <out>` both returned `lookup N
failed` for live graph IDs (157, 166, 170, 171) from iOS-side root —
`IOSurfaceLookup` does not resolve these IDs from an unrelated root process
context. `winsurf <windowID>` (chroot-side, SkyLight `SLSGetSurfaceList`)
returned `owner 76883 surfaceCount=0` for window 27 — app-owner connections
do not own the front surfaces in this generation (they are retained by
WindowServer/displayd). No memory reads or pokes were attempted.

## D130 F1 audit vs `WaitForWindowMetricsFlagsAfterGeneration`

`macwshostd/main.m:1444` — reopen freshness gate requires BOTH:
`header.generation > previousGeneration` AND at least one metrics entry with
`MacWSStreamWindowVisible` (1<<0). It is only reached by the
`MacWSInputKindReopenApplication` path at `main.m:1678`.

D130 observed: window 27 metrics generation 6→7 and a nonzero `configureAck`
after an exact-window `ConfigureWindow` record; a `GlobalSystemSurface`(0x40)
variant was rejected (no ack, no gen bump). Window 27's `flags=0x4d` does
include `Visible` (0x1).

**Verdict: D130 did NOT exercise reopen freshness.** No `ReopenApplication`
record was sent; the 6→7 generation advance was the ConfigureWindow-ack
metrics republish, not a reopen; and the `generation > previous` + `Visible`
predicate of `WaitForWindowMetricsFlagsAfterGeneration` was never the
acceptance gate under test. D130's F1 evidence stands as *exact-window route
discipline* (ConfigureWindow accepted; global-surface flag rejected) — reopen
freshness remains unwitnessed. Per task, no reopen was issued (read-only).

## Commands/exits (representative)

- `dispcap_probe` run: rc=0, one file produced.
- `iosurf_probe2 157/166/170/171`: "lookup failed", rc=1.
- `surfdump 157`: "lookup failed", rc=2.
- `winsurf 27` (chroot): rc=0, `owner 76883 surfaceCount=0`.
- scp pull: rc=0; device copy removed (rc=0 path verified absent).

## Cleanup / final state

- Device PNG deleted; local copy at `/tmp/d135cap_1.png`.
- No sentinels armed, no VNC touched, no GUI state changed; stack left
  running as found (phase=ready, composite seq advancing).

## Residual gaps

- Host-side on-screen iPad framebuffer pixels remain unwitnessed (no
  installed mechanism; `native Metal present` log + this composite image are
  the two halves of the chain).
- Reopen-freshness F1 path untested (requires a bounded reopen in a future
  task).

Candidate remains **PENDING**.

## Preserved review artifact

The captured PNG was independently opened and visually inspected, then copied
from the temporary capture path into the repository evidence set at
`docs/evidence/devin-d135-live-desktop-20261019.png` without modification.
SHA-256 remains
`c981cce82c2e973c8cf223d9846caebf91be65164fb87f016cfb685f0663f87c`.
