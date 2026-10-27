# devin-126 frontend result — GUI exit-1 attribution from saved evidence

Read-only; evidence doc `docs/evidence/devin-d126-frontend-20261019.md`.
No device access, no source/build/Git changes.

## Verdict

- **Producer pinned**: `macwshostd` `StartGUI` →
  `bash macos_gui.sh start coexist --no-terminal --no-vnc`, two live
  attempts at epoch 1792407330.657→342.918 (12.26 s) and
  1792407350.005→360.688 (10.68 s), both rc=1 →
  `GUI 启动脚本失败（退出码 1）` (`main.m:1326`). `rootfs-probe
  ready=YES mask=0xf` — rootfs/exec-bit gate passed.
- **Branch bounded, not named**: fresh 18:55 `macos_gui_watchdog.log` +
  `macos_gui_thermal_snapshot` (watchdog-loop-only writers) prove ≥1
  attempt armed the watchdog → failure is inside `trust` phase
  (`ensure_jb_usr_bind.sh` / `ensure_chroot_works` fast-fail branches:
  missing boot-trust helper, closure-probe→THERMAL-PAUSE, autosignd,
  mountdevfs, metal provisioner) or early `start_macos`. The ~11 s budget
  excludes the ~72 s trust walk and ~1 min postinst retry.
- **Saved records insufficient for the exact branch** — the artifacts
  never fetched `/var/mobile/Library/Logs/MacWSStartup.log` (the 1282 B
  `startup-log-bytes` already counts) or `/tmp/macos_gui_start.state`.
  Minimum next read = those two files + watchdog log; then only the one
  sub-log the ERROR names.
- **Evidence-loss hazard found (FACT)**: each `StartGUI` retry
  unconditionally unlinks `WindowServer.err.previous` before rename —
  the second attempt destroyed the first's rotated log while no new
  `.err` existed; explains why no WS log exists now.
- Oct12 archives (`guistart.log`, `restart.log`, `.err.bak/.prev2`)
  show only successful starts; kept separate from live records.
- No TCC/responsibility inference made.

(handover to codex now)
