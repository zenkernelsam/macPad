# D038 — independent deploy/watchdog/rollback preflight (source-cited)

Date: 2026-10-18 · Devin frontend 9589 · read-only local audit; no device,
no backend contact. Task: `coordination/_relay/devin-038-frontend-task.md`.
Sources: `layout/usr/macOS/bin/macos_gui.sh` (6142 lines),
`misc/sign_installed.sh`, `misc/vtool_and_sign.sh`.

## 1. Watchdog lifecycle — what actually exits the owner and blocks respawn

The live generation is owned by a **launchd job**
(`WATCHDOG_LABEL=com.macwsguide.watchdog`, plist `$WATCHDOG_PLIST`), not a
bare process — `run_watchdog` writes `$$` to `WD_PIDFILE`
(:1543-1556) and launchd restarts it on abnormal death.

- `stop` → `acquire_gui_transaction stop` (:5986-5992) → `stop_all`
  (:5516) → `stop_watchdogs` (:3446).
- `stop_watchdogs` for non-watchdog callers does
  `launchctl unload "$WATCHDOG_PLIST"` + `launchctl remove
  "$WATCHDOG_LABEL"` **first** (:3453-3455) — this is the respawn
  prevention: no loaded job = no relaunch. Then it kills the pidfile
  owner and any legacy `bash $0 watchdog ` loops (:3457-3481), then
  waits on the **exact PIDs** with bounded deadlines (5s TERM-wait +
  2s KILL-escalation, :3484-3510). This is the correct launcher-owned
  stop path — `macos_gui.sh stop`, not `kill`.
- `start_watchdog` (:5883) calls `stop_watchdogs` first, rewrites the
  plist with the live session intent flags, `launchctl load`s, then
  waits for `WD_READY` owner == the launchd-assigned child pid.
- `acquire_gui_transaction` (:417-447): atomic `mkdir` lock +
  pidfile; refuses if the recorded owner pid is still alive
  (`ERROR ... refused; pid=$owner is already changing...`, rc=75);
  recovers a stale lock only when the owner is dead. **Deploy must not
  run while a transition holds this lock** — check
  `$GUI_TRANSACTION_LOCK` absence (or simply let `stop` itself acquire
  it, which serializes correctly).

Ownership/exit proof points for the A/B: `launchctl list |
grep macwsguide.watchdog` empty + `ps` shows no `bash ...
watchdog` + `WD_PIDFILE`/`WD_READY` gone + WS label PID gone —
all four, not just process absence.

## 2. Deploy mechanics — what the binaries actually require

- **Injection model**: no `DYLD_INSERT_LIBRARIES` env anywhere in the
  script — macOS binaries carry `LC_LOAD_DYLIB` on libmachook
  (:1996-1997 comment: "whole /bin+/sbin CLI surface and a curated
  /usr/bin set carry LC_LOAD_DYLIB"). Deploying = replacing the dylib
  FILES; no plist/env edit needed, and none must be made (one-variable).
- **Deploy paths** (enumerated in the trust set, :1900-1910):
  - `$ROOTFS/usr/local/lib/libmachook.dylib` — arm64e (chroot consumers)
  - `$ROOTFS/usr/local/lib/libmachook_arm64.dylib` — arm64
  - `$ROOTFS/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` — fat provider
  - iOS-side twins exist (`/var/jb/usr/macOS/lib/libmachook*.dylib`,
    `/var/jb/Library/Frameworks/CydiaSubstrate.framework/...`) — the A/B
    should leave these UNTOUCHED: they serve iOS-side consumers and
    changing them would smuggle a second variable. Flagged scope decision.
- **Trust model**: Dopamine dynamic trustcache is **volatile per boot**
  (:2026-2035 comment). Every deployed Mach-O must be `ldid -S` signed
  and its per-slice CDHash `jbctl trustcache add`ed
  (`sign_installed.sh`: LDID=/var/jb/usr/bin/ldid :15,
  JBCTL=/var/jb/usr/bin/jbctl :16; admission at :345-351;
  `jbctl trustcache info` is the authoritative inventory :27-29 —
  `list` silently returns empty).
- **Platform patching**: `vtool_and_sign.sh` =
  `vtool -arch arm64 -set-build-version $PLATFORM 11.0 11.0 -replace`
  then `ldid -S` — needed only if the theos build carries wrong
  LC_BUILD_VERSION; VERIFY `otool -l | grep -A2 LC_BUILD_VERSION`
  first rather than blindly rewriting.
- **Slice identity**: `libmachook.dylib`=arm64e, `libmachook_arm64.dylib`=arm64
  — deploy each THIN artifact to its own path (never lipo them into one
  file; the LC_LOAD_DYLIB refs are per-arch). Provider is fat → one
  path. Verify `file`/`lipo -info` per artifact before copy.

## 3. The inode hazard (plan must fix step 3)

`scp` onto the target path truncate-writes the SAME inode. If any
process still maps the old image (e.g., deploy attempted before the
generation fully exits, or an iOS-side daemon maps the twin), the
mapped pages corrupt mid-write — the historical inode landmine. Even
after a verified stop, use the atomic sequence below: it also preserves
the rollback object on-device without a second fetch.

## 4. Corrected deploy recipe (all on device, placeholders for creds)

```
# 0. Gates (read-only)
launchctl list | grep -i macwsguide        # watchdog+WS jobs present
test ! -d "$GUI_TRANSACTION_LOCK"          # no transition in flight
cat /tmp/macos_gui_watchdog.pid            # record owner pid
# 1. Preserve (new backup dir, never reused)
B=/var/mobile/macws_d037_backup_$DATE; mkdir -p $B
for f in usr/local/lib/libmachook.dylib usr/local/lib/libmachook_arm64.dylib \
         System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate; do
  cp -p /var/mnt/rootfs/$f $B/$(basename $f); shasum -a256 $B/$(basename $f)
done
# 2. Stop through the launcher (unloads watchdog job = no respawn)
macos_gui.sh stop
# verify all four: launchctl list empty of macwsguide+WindowServer jobs,
# no bash ... watchdog processes, pidfiles gone, WS/displayd/Host exited.
# 3. Stage + patch + sign NEW inode, not the live path
scp artifacts -> /var/mobile/stage_d037/
vtool -set-build-version (only if otool -l shows wrong platform/minos)
ldid -S each staged file  (entitlements per sign_installed.sh if needed)
ldid -arch <slice> -h file -> CDHash; jbctl trustcache add <hash> per slice
# 4. Atomic rename into place (same filesystem as $ROOTFS)
mv staged -> /var/mnt/rootfs/usr/local/lib/libmachook_arm64.dylib.new
mv .new -> final path        # rename(2) = new inode, atomic
# repeat for libmachook.dylib + CydiaSubstrate
# 5. Post-rename verification before any start
shasum -a256 final paths == staged hashes; file/lipo arch check;
jbctl trustcache info | grep new CDHash
# 6. Start one bounded owned generation via launcher (same argv as the
#    old watchdog's — copied verbatim; no diagnostics flags added)
#    Bound is watchdog-enforced: add --runtime-cap=300 (min 60) —
#    WD_MAX_RUNTIME trips the watchdog -> stop_all automatically
#    (:1682-1684; flag propagates through start_watchdog :5890-5900).
#    This is a lifecycle bound, not a behavior variable; backend packet
#    requires it (max 300s).
```

## 5. Rollback order (on any ladder failure)

1. `macos_gui.sh stop` (acquires transaction, unloads watchdog).
2. `mv` preserved `$B/*` back over the deployed paths (atomic, same fs).
3. Re-`jbctl trustcache add` the OLD CDHashes if evicted; they should
   still be present (additive cache — nothing removes them this boot).
4. shasum the restored paths vs recorded pre-deploy hashes.
5. Leave stopped (or restart via launcher per coordinator direction);
   no blind restart loop.

## 6. Residual risks vs witnessed failures

- POSSIBLE RISK: live IOMFB LC_UUID arm64-vs-arm64e slice mismatch →
  provider -2 refusal (bounded, named line, no patch). Mitigation: plan
  step 2's live uuid read BEFORE deploy.
- POSSIBLE RISK: a concurrent transition holds the transaction lock →
  deploy/start must wait, not force. Script refuses correctly (rc=75).
- POSSIBLE RISK: watchdog relaunched between stop and rename window →
  generation respawns mid-deploy. `launchctl unload` removes the job
  first, so this needs an EXTERNAL re-`load` to happen — verify the
  four-way exit proof before staging.
- POSSIBLE RISK: iOS-side provider twin stays old — if any iOS process
  injecting the chroot framework path exists (unlikely — the paths are
  chroot-scoped), behavior stays pre-fix there by design.
- NOT A WITNESSED FAILURE: nothing in this audit blocks the A/B; all
  gaps are mechanics the deployer can satisfy from the cited scripts.

Pending backend D037 inputs unchanged (live probe transcript, IOMFB
uuid, changed-bytes, counters, state, pixels) — none exist yet; not
polled per task.
