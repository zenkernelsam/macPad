# devin-038 frontend result — deploy/lifecycle preflight audit

Worker: macPad Devin D038 frontend 9589. Read-only local audit, no
device/backend contact. Deliverable:
docs/evidence/devin-d038-deploy-lifecycle-audit-20261018.md (fully
line-cited against layout/usr/macOS/bin/macos_gui.sh + misc scripts).

## Proven from source

- Watchdog is a launchd job (com.macwsguide.watchdog), not a bare
  process. The launcher-owned stop = `macos_gui.sh stop` → transaction
  lock → stop_all → stop_watchdogs which `launchctl unload`+`remove`s
  the job FIRST (true respawn prevention), then kills pidfile owner +
  legacy loops and waits bounded on exact PIDs. :3446-3510/:5516/:5986.
- Transaction lock = atomic mkdir + pidfile + owner-liveness + stale
  recovery; refuses concurrent transitions (rc=75). :417-447.
- Injection is LC_LOAD_DYLIB on macOS binaries — no env/plist edit in
  the deploy (keeps bundle one-variable). :1996.
- Trust = volatile dynamic trustcache: every deployed slice needs
  ldid -S + per-CDHash `jbctl trustcache add` (list subcommand is
  unreliable; `trustcache info` authoritative). sign_installed.sh cited.
- Slice contract: libmachook.dylib=arm64e / _arm64.dylib=arm64 to
  separate chroot paths; provider fat → one framework path.
  iOS-side twins stay untouched = no smuggled second variable.

## Corrections delivered to the plan's deploy step

1. scp-overwrite is the documented inode landmine — replaced with
   stage→(vtool only-if-needed)→ldid→jbctl→atomic `mv` rename recipe;
   post-rename shasum+arch+trust verification before start.
2. Four-way exit proof required before staging (launchctl empty + no
   watchdog procs + pidfiles gone + generation exited).
3. Rollback = launcher stop → atomic mv of preserved copies → shasum;
   old CDHashes persist in the additive cache.

## Blockers vs risks

No witnessed failure blocks the A/B. Risks all have bounded handling
(live IOMFB uuid gate, transaction-lock respect, exit-proof gate,
trust admission). Backend D037 device results still pending — not
polled, per packet.

(handover to codex now)
