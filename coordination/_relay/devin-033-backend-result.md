# Devin backend D033 result — read-only device regression inventory

Date: 2026-10-18 (Asia/Hong_Kong). Task:
`coordination/_relay/devin-033-backend-task.md`. **Read-only round** — no
start/stop/restart, no deploy/build/commit, no marker/trustcache/source
changes, no device writes. SSH used `sshpass -e` with transient env only;
no secret persisted, printed, or placed in process arguments.

## Outcome

**The desktop is not "stopped" — a watchdog-managed generation is RUNNING
and has produced zero frames since 22:27.** Root-cause evidence points at
the permit-gated provider refusing every SkyLight-internal
`MSHookFunction` target in WindowServer, which kills the swap-completion
→ publish chain; the same refusal regime existed in the R010-baseline
provider binary, so the defect is not new to today's 19:30 install.

Full evidence: `docs/evidence/devin-d033-device-regression-inventory-20261018.md`.

## Current device state (verified 22:43–22:49 HKT)

- iPad13,11 T8103, iPadOS 16.3 (20D47), uptime 2d9h56, thermal nominal
  33.2 °C. Native SpringBoard 872 / backboardd 905 / tccd 883 alive
  (all restarted 12:26:48 — midday userspace/respring, kernel not rebooted).
- Live generation: `macos_gui.sh watchdog coexist --no-vnc --no-terminal
  --experimental` (pid 92972, 22:26:42). WindowServer 93627 alive 21 min
  (`-virtualonly -daemon`), displayd 93724, inputd/interopd/locationd/
  OSXvnc 93880, Host 93835, tccd-system 93432, ~27 project launchd jobs.
- Composite state files: `state=missing producer=0 sequence=0`
  (written once at displayd start 22:28:03, never updated). displayd
  current segment: 0 frame-first / 0 received / 0 accepted / 0 rejected /
  0 surfaces.
- All six diagnostics markers absent (both namespaces).

## Regression evidence (verbatim counts)

- `WindowServer.err` (live gen): `REFUSED`=21 + `MSG-REFUSED bad args`×2;
  `SwapEnd hook installed`=0; `FINAL-COMPOSITE`=0; AGX probe
  `0x78→0xe00002c2 → legacy-0x70` (unchanged from working era).
- `WindowServer.err.previous` (R013 19:50): REFUSED=58, SwapEnd=0.
- `WindowServer.err.bak` (Oct 12, last good): REFUSED=0, SwapEnd
  installed ×10, `replay-receiver-ready port=120379`, replay-received
  ×6, full SkyLight/QC/MTL hook surface installed.
- Mechanism (source, RE-confirmed): provider `MSHookFunction` is
  fail-closed without a `(uuid,offset,prologue)` permit
  (`misc/cydiasubstrate_macos.m:459`); the only `MACWSHookPermitV2`
  registration is diag-marker-gated (`libmachook/mac_hooks.m:6077`) →
  zero permits in production → all refused → no SwapEnd hook → no
  `macws_publish_scanout_after_swap` → no publish → `state=missing`.
- Permit gate present in BOTH installed provider (`b2ae21da…`) and R010
  baseline (`7320a210…`) → restoring R010 baseline is NOT a known-good
  for this defect.

## Crash inventory (today, CrashReporter)

- `iconservicesagent`: 17 real crashes + still looping (latest 22:49:00,
  ~40–90 s/cycle). CHROOT arm64e (uuid `c0891064` match + libmachook
  loaded). `MPSLibrary::MPSKey_Compile → MTLReportFailure → __assert_rtn`.
- `MTLCompilerService`: 25 SIGABRT, cluster 22:30–22:31 then stopped.
  iOS-NATIVE binary (uuid `6d2cfe56` match), abort at
  `readModuleFromBinaryRequest → errorToErrorCodeAndEmitErrors →
  fatalDiagnosticHandler` = module decode failure (F11H class). Sender
  unattributed (iconservicesagent MPS path is a documented candidate —
  THEORY).
- `UIKitSystem`: 3 chroot SIGTRAPs (uuid `1e97e27c` chroot match),
  ~0.6 s lifetimes at 22:30.
- `macwsdisplayd/inputd/interopd/CoreLocationAgent/etc.`: ExcUserFault
  only (GUARD_TYPE_USER ns7 reason9 startup burst, per R022/R023 class),
  no real crashes — daemons alive.
- `ReportCrash`×24 (self-fault while reporting), `routined`×25
  (iOS-native; UNATTRIBUTED — flagged).
- WindowServer: zero crashes.

## Installed identities — all match R012/R013 records

package 0.3.4; provider `b2ae21da…` inode 250671913; libmachook
`47f78880…`(arm64e)/`6de9b688…`(arm64); `macos_gui.sh` mtime 18:57;
R010 baseline files all present with recorded hashes.

## Last known rendered baseline

- Documented: Oct-13 `state=ready`, continuous seq 278+
  (`docs/evidence/ventura-22F82-input-transport-and-composite-20261013.md`).
- Log-derived: last `state=ready` for WS 59329, bounded ≤ Oct 14 16:20
  (displayd.err line ~17912); sparse `frame-first` window=0 surfaces
  until gen ending Oct 18 01:18; **zero frames** on all three post-19:30
  generations (R012 19:33, R013 19:50, live 22:27).

## Candidate reversible next action (not executed)

Register an exact `MACWSHookPermitV2` permit for the IOMFB
`publicSwapEnd` target (offset already pinned per-build in
`kMacWSIOMFBABIs`) — and the other SkyLight composite-chain targets
visible in `.bak` — as a one-variable test in an owned generation.
Preserves the fail-closed contract; authorizes intended targets instead
of bypassing the gate. Success witness: `SwapEnd hook installed` +
`replay-receiver-ready` + `state=ready`/frames. Do NOT treat the R010
baseline as known-good for this defect; do NOT conflate with the F11H
decode failure (separate class; fix order: hook chain first, then
re-observe whether Metal decode becomes the next WS blocker).

## Ownership / cleanup status

- This session owned nothing: no processes started, no files written on
  device, no SSH session held open. All SSH commands returned exit 0
  (one benign glob miss re-run cleanly; `sysctl`/`shasum` absent —
  `uname`/`stat`/`sha256sum`+python3 used instead).
- The live generation belongs to the existing watchdog launcher — left
  untouched per read-only scope. iconservicesagent crash loop continues
  under it (expected watchdog respawn behavior).
- Parked per task: R031 compiler candidate (grammar/audit pass only,
  never compiler-tested). No private compiler replay run or scheduled.
- Frontend handover and SYNC.md are consistent with this inventory;
  `libmachook/mac_hooks.m` uncommitted R012 hunk remains uncommitted
  (not touched).

## Limits

- No `launchctl print`/lldb/`log show`/process maps — bounded reads only.
- `REFUSED` lines don't name targets; the SwapEnd refusal is proven by
  the missing install line + source contract, not a named-target log.
- Working-era provider binary is not among preserved backups; its swap
  date is THEORY bounded post-Oct-12 / pre-Oct-14.
- routined crashes and the 12:26 respring are facts on the timeline,
  unattributed.

(handover to codex now)
