# D137 backend result — reopen-freshness: STOPPED (precondition unmet)

Status: **STOP without action** — mandated by the task contract.

## Blocker (exact)

System Settings is **not running** in the live D130/D135 chroot generation.
Full `ps` inventory: WindowServer, Terminal, Dock, ControlCenter,
SystemUIServer, Finder, CoreLocationAgent only. No `macws_app_input` socket,
no metrics sidecar, no workspace-graph layer for Settings.
`system_settings_available=yes` is a launchability flag, not a running
instance — `RequestApplicationReopen` (hostd `main.m:1678`) needs a live
target pid. Invoking it would require a launch/relaunch, which the task
forbids. No substitute injection was attempted.

## Recorded prechecks (read-only)

- `windowing ready=yes abi=1 pid=1796 caps=0x7f`; hostd 1821 alive 5h10m.
- Host foreground `foreground=YES suspended=NO` (same session as D135).
- `phase=ready`, producer=22527, composite sequence=1053 (still advancing).
- Metrics snapshot: Finder gen=7 (w17, f=0xc00), ControlCenter gen=3
  (6 windows, 0xc00), Terminal gen=7 (w27, f=0x4d incl. Visible).

## Delta / integrity

- Nothing written, armed, killed, or launched. Desktop left as found.
- Crash delta: `routined` ×2 (iOS noise, unrelated) and `macwsworkspacectl`
  ×2 at 23:11 — **caused by my own earlier read-only invocation** (macOS
  binary under iOS zsh dies in dyld); stack unaffected, not repeated.

## Forward note

The reopen-freshness witness still needs a generation where System Settings
(or an agreed target) is already running; then exactly one
`RequestApplicationReopen` observing `generation > previous` + `Visible`.

Evidence: `docs/evidence/devin-d137-backend-20261019.md`
Candidate: **PENDING**

(handover to codex now)
