# D126 — attribute current GUI `exit 1` from saved evidence (read-only)

Task: `coordination/_relay/devin-126-frontend-task.md`. No device/SSH, no
GUI start/restart, no source/build/deploy/trustcache/TCC/KRW/Git changes.
Inputs: `coordination/artifacts/current-ipad-macpad-{logs,startup-logs,
readonly,status}-20261019.json`, deployed `macos_gui.sh` (installed cksum
`2532344555` == local `layout/usr/macOS/bin/macos_gui.sh` per D125), and
`macwshostd/main.m` HEAD.

## 1. Live Oct19 records vs Oct12 archives — separated

**Live (current generation, hostd pid 1821, started epoch 1792404680 ≈
Oct19 18:11, etime 47:28 at the 18:58 capture):**

```
1792407330.610 rootfs-probe ready=YES mask=0xf bash-errno=0 ws-errno=0
               gui-errno=0 exec-errno=0
1792407330.650 rotated WindowServer log to /var/jb/var/mobile/WindowServer.err.previous
1792407330.650 state busy=YES phase=检查并修复启动环境… error=
1792407330.657 spawned pid=4032 executable=/var/jb/usr/bin/bash wait=YES
1792407342.918 state busy=NO phase=操作失败 error=GUI 启动脚本失败（退出码 1）   ← 12.26 s
1792407350.003 state busy=YES phase=检查并修复启动环境… error=
1792407350.005 spawned pid=4291 executable=/var/jb/usr/bin/bash wait=YES
1792407360.688 state busy=NO phase=操作失败 error=GUI 启动脚本失败（退出码 1）   ← 10.68 s
```
(`current-ipad-macpad-readonly-20261019.json` "recent host log" tail;
lines before `macwshostd starting pid=1821` belong to the previous hostd
generation's ring buffer.) `status` artifact at 19:00:
`windowserver=no busy=no startup-retry=yes startup-log-bytes=1282
phase=操作失败 error=GUI 启动脚本失败（退出码 1）`.

**Archived (NOT the current failure):** `guistart.log` Oct-12 23:55 =
complete successful start (WS pid 61467, "Started in coexist mode");
`macos_gui_restart.log` Oct-12 20:25 = successful restart;
`WindowServer.err.bak` Oct-12 03:37 and `.prev2` Oct-11 16:16 = old WS
stderr; `WindowServer.out` Oct-07 empty. The live-side files are
`macos_gui_watchdog.log` (311 B, Oct-19 18:55) and
`macos_gui_thermal_snapshot` (157 B, Oct-19 18:55) — both recreated
inside the failing window.

**Evidence-destruction side effect (FACT, source-verified):**
`StartGUI` (`main.m:1297-1302`) unconditionally `unlink`s
`WindowServer.err.previous` then renames `WindowServer.err`. Attempt 2's
rotation deleted the `.previous` produced by attempt 1 while no new
`.err` had been created — which is exactly why the 18:58 `find` shows no
`WindowServer.err`/`.previous` at all. Each failed retry permanently
erases one generation of WS stderr. The current failure never reached
WS launch.

## 2. Producer and branch bounds (source + verbatim lines)

- Producer: `macwshostd` `StartGUI` (`main.m:1306-1353`) →
  `RunCommandToLog({kBash, kGUI, "start","coexist","--no-terminal",
  "--no-vnc"})`, stdout+stderr dup2'd into `kStartupLogPath =
  /var/mobile/Library/Logs/MacWSStartup.log` (`main.m:70-71,610-611,
  1319-1326`). `rc=1` → the verbatim error string at `main.m:1326`.
- `RootFSReady()` passed: `rootfs-probe ready=YES mask=0xf`, all four
  errnos 0 (`main.m:870-924`). Not a mount/exec-bit failure.
- Reached-and-passed `start_watchdog` (script `safety` phase): the
  watchdog child is the only writer of `macos_gui_thermal_snapshot`
  (`record_thermal_snapshot` is called only inside the watchdog loop,
  `macos_gui.sh:1576-1596`) and `start_watchdog` `rm -f`s both files
  before arming (`:5885-5887`) — fresh 18:55 mtimes prove ≥1 attempt
  armed the watchdog through `WD_READY` (`:1584`). A watchdog-stage
  failure mode (`WD_ARM_TIMEOUT=30` loop, `:5904`) cannot fit inside a
  10.7–12.3 s total.
- Timing budget excludes the expensive branches inside
  `ensure_chroot_works`: the full `macws_boot_trust.py` walk (~72 s+ by
  its own comment, `:2080-2081`) and the `postinst.sh` retry (~1 min,
  `:2388-2395`) each alone exceed the observed span. So either the run
  failed **before** the sentinels — `ensure_jb_usr_bind.sh` (instant),
  `restore_cold_boot_trust` fast paths (missing `macws_boot_trust.py`;
  `trust_closure_already_live` probe fail → `application_trust_-
  thermally_safe` → **THERMAL-PAUSE**, all bounded at seconds), 
  `ensure_autosignd_ready`, `mountdevfs`, `METAL2METAL_COMPAT_PROVISIONER`
  — or it **passed** `ensure_chroot_works` and died inside early
  `start_macos`.
- **The saved records do not name the branch.** The two artifacts
  captured only `/var/jb/var/mobile` files; the authoritative records
  are elsewhere and were never fetched. No TCC/responsibility link is
  claimed or implied.

## 3. Minimum next read-only evidence (all already on device)

1. `cat /tmp/macos_gui_start.state` — the script's own last
   `phase=`/`detail=` (`write_gui_start_state`, `:449-463`). This alone
   pins the failing stage.
2. `tail -c 8192 /var/mobile/Library/Logs/MacWSStartup.log` — the
   1282 bytes `macws_control_probe status` already counts
   (`startup-log-bytes=1282`; the `startup_log` XPC field is populated
   but the probe prints only its length, `macws_control_probe.c:425-437`).
   Contains the script's verbatim ERROR line and every TIMING marker.
3. `cat /var/jb/var/mobile/macos_gui_watchdog.log` +
   `/tmp/macos_gui_watchdog.ready` + `/tmp/macws_safety_trip` — confirm
   arm/teardown and rule out a safety trip (`AddStatus` would surface a
   trip as `phase=安全保护已触发`; status shows `操作失败`, so trip is
   already unlikely).
4. **Only** the single sub-log the startup ERROR names, e.g.
   `mountdevfs.log` / `restart-autosignd.log` / `quartzcore-compat.log`
   / `postinst.log` / `macos_gui_thermal_snapshot` — do not pull the
   whole set.

## 4. Bounded post-install start acceptance sequence

Given one authorized start:
- **Pre-start snapshot (before the attempt):** `ls -l` + `cat` the state
  file, `tail` MacWSStartup.log, `ls -l WindowServer.err*` — records the
  boundary and preserves the current `.err` before rotation can erase it.
- **Start once.** While it runs, the state file phase is the only
  progress witness needed.
- **On failure:** read exactly (a) new tail of MacWSStartup.log — last
  `TIMING stage=` marks the entered stage and the final `ERROR:` names
  the branch; (b) `macos_gui_start.state`; (c) the one sub-log the ERROR
  references. No broad log sweeps, no retry loops, no timeout changes.
- **On success:** readiness = existing contract only —
  `WaitForGUIComponents` (WS pid + input socket + DisplayStream), then
  per-app `WaitForWindowMetricsFlagsAfterGeneration` (`main.m:1444`,
  generation>previous + `Visible`) for the first launched app; VNC/panel
  freshness rules unchanged.

(handover to codex now)
