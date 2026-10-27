# D041 backend result — input boundary on the live generation (2026-10-19)

Packet `devin-041-backend-task.md` executed on the CURRENT session the
user is watching — no restart/teardown/watchdog change. Full evidence:
`docs/evidence/devin-d041-input-boundary-20261019.md` (+ PNGs, trace).

## Verdict

**Exact AppInput PID/window key path: works end-to-end — real AppKit
sendEvent ack + visible text consequence, on production-shaped records.**
Global route: both denial surfaces reproduced exactly as documented
(HIDEvents name 1102 dead; SLSPostEventRecord→1002). Desktop still
healthy post-test; generation untouched.

## Inventory (FACT)

Generation pids: WS 10447 / inputd 10436 / displayd 10540 / Dock 10668 /
Finder 10595 (frontmost, window 32) / Terminal 10929 (window 27, session
DEAD "[Process completed]") / ControlCenter 10601. Composite:
`ready producer=10447 sequence 695→1176` advancing throughout.
tccd-system pid 10300 + iOS tccd 883 both live. Deployed input binaries
(D042 reconcile: repo's own .theos/obj code — whole-file hash deltas were
signing/extraction artifacts, not a newer deployment): inputd `0bc473d1…`,
libmachook arm64e `aa041efd…` / arm64 `21f81b5c…`, provider `134b7eb9…`.

## Global route (bounded probes, once each — FACT)

`macos_ws_event_probe post` (launchdchrootexec):
- `com.apple.WindowServer.HIDEvents -> rc=1102 port=0` (dead name);
  windowserver/.active/CARenderServer rc=0.
- SkyLight uuid 96676A53… match=1; cid=155063; event port valid.
- `SLPreflightPostEventAccess -> 0`; `TCCAccessPreflightWithAuditToken
  (3 svcs, self) -> 1/1/1`; SecTask tcc.allow vended, loginwindow absent.
- `CGWarpMouseCursorPosition` WARP-OK (control channel alive).
- `CGEventPost(HID,moved)` → cursor unmoved → NO-DISPATCH.
- `SLSPostEventRecord(canonical 248B record) -> rc=1002`.

`macwstccdprobe`: ping → `reply-type=dictionary {count=0}` (system tccd
reachable, EMPTY reply dict); preflight → `kTCCServicePostEvent result=1`.

Upstream boundary (FACT): denial is WS-side gate
(`CGXSenderCanSynthesizeEvents`→0) — not transport (warp OK, event port
valid, tccd replies) and not payload (canonical record). The accessor-1 vs
WS-1002 asymmetry persists; empty reply dict matches the R007/R008 family
(no explicit denial field). `responsibility103` remains the leading THEORY
for why the WS permission-cache key misses.

## AppInput exact path — the one controlled action (FACT)

Ownership verified BEFORE injection, two independent sources: metrics
sidecar (Finder gen=8, window 32) + live WS winlist (win=32 pid=10595
223,149 920x436 onscreen). Terminal rejected as target (dead login
session — visible in capture).

Records → `host_key_probe.py --pid 10595 --window 32 --width 1366
--height 1024 --latency-diagnostic` → inputd → `macws_app_input.10595.sock`.
Narrow keyboard-only sentinel enabled for the test only (never the global
runtime marker; removed after). Per-record ack chain fired every time:
`app-socket (0.2–0.8ms) → app-queue post-event → app-dispatch
(sendEvent hook observed each down/up; 0.65–2.7ms producer→dispatch)`.

Action set (all reversible, same exact target):
1. right-arrow — delivered, no visible change (Recents empty).
2. 'd' — delivered, no visible change (no type-select target).
3. Cmd-F — **search bar opened with focused caret** (pixel diff).
4. 'd' — **'d' glyph rendered inside the live search field** (text).
5. Backspace + Escape — reverted; final frame == baseline.

Artifacts: `frame-before.png`, `frame-after-cmdf.png` (search bar),
`frame-after-text.png` ('d' visible), `frame-reverted.png` (== baseline),
`keyboard-latency.trace` (36 lines, all stages).

## Session health after test (FACT)

`state=ready sequence=1176`; all procs alive; watchdog 9845 untouched;
desktop pixels identical to pre-test; no teardown (user session kept).

## Scope/cleanup (FACT)

No production source/build/deploy/marker/TCC/kernel change. Transient
sentinel + trace removed from chroot tmp. Probe artifacts left only in
`/private/var/tmp/d041/` (inert); capture binary cdhash
`0b3dcc66…d2acf` was trustcache-added so it could run. `git diff --check`
clean; no commit; no new launchd jobs; iOS-side provider untouched.

## Next producing layer (FACT/THEORY)

Only dead rung = global pipeline (HIDEvents 1102 + gate 1002).
Pointer/IME/clipboard/activation untested — do not rebrand.
Next packet candidates: permit-instrument `CGXSenderCanSynthesizeEvents`
(SkyLight+0x318464) and `_XPostEventRecord` (+0x388e64) — clean prologue
windows already mapped — for entry counters + gate-byte readback; audit
the `responsibility103`/permission-cache key relation; check whether
`-virtualonly` suppresses the HIDEvents registration.

(handover to codex now)
