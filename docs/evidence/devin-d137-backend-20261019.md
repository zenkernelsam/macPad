# D137 — reopen-freshness acceptance: STOPPED, target absent

**Task**: `coordination/_relay/devin-137-backend-task.md` — exercise the
existing `RequestApplicationReopen` → `WaitForWindowMetricsFlagsAfterGeneration`
(`generation > previous` AND `Visible`) path on the already-running System
Settings, exactly once, read-only otherwise.

**Outcome: STOP without action** — the required precondition failed:
**System Settings is not running** in the live chroot generation.

## Precondition evidence (all read-only)

### Target absence (the blocker)

- `ps aux` full inventory: running macOS GUI apps are exactly —
  WindowServer(22527), Terminal(22802), Dock(22769), ControlCenter(22707),
  SystemUIServer(22705), Finder(22701), CoreLocationAgent(22683). **No
  `System Settings` / `systempreferences` process exists.**
- App-input sockets `macws_app_input.<pid>.sock`: live pids are the six
  above; 22152, 22703, 24363 are DEAD sockets. No Settings socket.
- Metrics sidecars present: 22152, 22683, 22701, 22705, 22707, 22802 — no
  Settings pid. No entry in `macws_workspace_graph.state` matches Settings.
- `macws_control_probe status`: `system_settings_available=yes` — this is
  the launchable-bundle flag, **not** a running instance. The reopen path
  (`macwshostd/main.m:1678`) requires an existing `pid` with a live
  `macws_app_input.<pid>.sock`; there is none. Invoking it would require a
  fresh launch — explicitly forbidden ("already-running target", "not
  TERM/kill/relaunch", "do not substitute").

### Native/iOS health

- `windowing-status`: `ready=yes abi=1 pid=1796 capabilities=0x7f`.
- `macwshostd` pid 1821, etime 5h10m — stable.
- Host foreground: latest `scene-occlusion` record `foreground=YES
  backgrounded=NO suspended=NO` (session A2A3C0E7, unchanged from D135).
- `/tmp/macos_gui_start.state`: `phase=ready` (pid 21584 generation).
- `uptime`: 8:59, load 1.36 — iPadOS healthy.

### Producer/composite correlation

- `macws_final_composite.state`: `state=ready producer=22527 sequence=1053
  reason=validated-final-composite` — same WindowServer generation as
  D130/D135, still advancing.

### Metrics generation/Visible snapshot (decoded live)

| pid | owner | generation | entries (window / flags) |
|---|---|---|---|
| 22701 | Finder | 7 | w17 f=0xc00 |
| 22707 | ControlCenter | 3 | w18,w19,w20,w23,w24,w32 all f=0xc00 |
| 22802 | Terminal | 7 | w27 f=0x4d (Visible+Shadow+Resizable+Focused) |
| others | — | 1 | 0 entries |

No Settings sidecar exists; nothing to target.

## Actions NOT taken

- No `RequestApplicationReopen` was emitted (no running target exists).
- No launch/relaunch/kill of any process; no input injection substitute.
- No sentinels armed, no VNC, no display-mode or file changes.
- The runtime diagnostic/observation paths remain absent.

## Crash delta

CrashReporter new entries since last audit: `routined` ips ×2 (23:11,
23:13 — pre-existing iOS noise, unrelated) and **`macwsworkspacectl`
ips ×2 at 23:11 — caused by my own earlier read-only invocation**
(`/var/jb/usr/macOS/bin/macwsworkspacectl` is a macOS binary; invoking it
from iOS zsh dies in dyld "wrong platform" — the .ips records that abort).
Honest attribution: probe-caused, no effect on the stack; not repeated.

## What a future run needs

The acceptance remains unwitnessed. It requires a generation where a macOS
System Settings is already running (or an authorized launch), then one
`RequestApplicationReopen` → observe `generation > previous` AND a
`Visible`-flagged metrics entry.

Candidate remains **PENDING**.
