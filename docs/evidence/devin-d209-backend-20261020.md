# D209 — human-requested macPad shutdown and pause

Human request via `coordination/_relay/devin-209-backend-task.md`:
shut down macPad now, then pause. Supersedes all development work.

## Device identity / session (re-verified pre-action)

- `devssh` session live, password auth (credentials never persisted or
  printed)
- Device: iPad13,11 (M1), iPadOS 16.3 / 20D47, Dopamine rootless —
  matches the reproduction runbook identity
- Kernel: `RELEASE_ARM64_T8103 xnu-8792.82.2~1`

## Pre-stop inventory (project-owned, running)

WindowServer (pid 22527, SkyLight.framework), Dock, Finder, Terminal,
SystemUIServer, ControlCenter, macwsdisplayd(22644), macwsinputd(22502),
macwsinteropd(22664), macwsaudiooutd(22109), macwslocationd(22666),
macws-cfprefsd (×3), autosignd(13890) — plus baseline launchd jobs:
macwshostd(1821), macwskeychaind(1828), macwsallocd(1840),
MacWSHost.app(17584).

## Stop action

`zsh /var/jb/usr/macOS/bin/macos_gui.sh stop` → **exit 0**.

Script output (verbatim tail):
```
[macos_gui] Cleaning up previous macOS GUI services...
[macos_gui] Cleanup done.
[macos_gui] Restoring iOS (SpringBoard / backboardd)...
[macos_gui] Stopped. The iPad is back on iOS.
```
Non-fatal script noise observed: `clear_diagnostic_state` had `rm:
command not found` (PATH inside script); verified below that no
diagnostic flags remain. No reboot, uninstall, or WIP reset performed.

## Post-stop verification (exact)

`macos_gui.sh status`:
```
-- processes --
(none running)
-- launchd jobs --
1840 com.macwsguide.alloc | 1828 com.macwsguide.keychain |
1821 com.macwsguide.hostd  | 17584 UIKitApplication:com.macwsguide.host
VNC: not running
```

- **Gone**: WindowServer, Dock, Finder, Terminal, SystemUIServer,
  ControlCenter, displayd, inputd, interopd, audiooutd, locationd,
  autosignd, cfprefsd — entire macOS GUI stack + project workers.
- **Remaining** (by design — baseline iOS-side lifecycle, not
  diagnostics): macwshostd, macwskeychaind, macwsallocd, MacWSHost.app.
  The stop script intentionally leaves these; they are launchd jobs,
  not the GUI stack. No project-owned diagnostic process needed
  retiring.
- **Native iOS healthy** (untouched): launchd(1), SpringBoard(1796),
  tccd(1807), backboardd(1829), CommCenter — all alive.
- **Diagnostic sentinels**: none (`macws_runtime_diagnostics`,
  `macws_svctime*`, `macws_sender*`, `macws_diag*` all absent in both
  chroot and iOS tmp namespaces).
- **Leftover run artifacts** under `/var/mnt/rootfs/private/tmp/`
  (stale `.sock`/`.state`/`.info` files from the run, e.g.
  `macws_final_composite.state`, `macws_workspace_sleeping`). These are
  per-run chroot tmp files; the next `start`'s `cleanup_macos` removes
  them. Left in place — stop semantics, not a reset.

## WIP / host-side state

- D206/D207 accepted and committed as `b9395f0` (7 files).
- All working-tree WIP (DevinWatchdog, coordination state, other
  agents' files) preserved untouched.
- No rebuild, deploy, restart, or device start performed.

## Pause declaration

macPad is shut down and verified stopped. Per human request: **paused —
no new development, experiments, device start, or restart until explicit
human resume.**
