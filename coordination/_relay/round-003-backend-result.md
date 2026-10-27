# Backend R003 result — fresh WS attribution and hosted-probe A/B

Date: 2026-10-18 (Asia/Hong_Kong). Scope: `round-003-to-backend.md`.
`coordination/reviews/R004-frontend-R002-backend-20261018.md` was read;
its caution that zero tccd lines do not prove a cached denial is
incorporated below. Read-only source/build/Git: no source, test, shared
build input or Git change; no package rebuild/install; no dependency
install; no grant, database, session, token or checker modification.
Device exclusive per the relay. Prior R002 logs and the
source/deployed mismatch are preserved; this run is **not** attributed
to merged HEAD.

## Inventory (FACT)

| Item | Value |
| --- | --- |
| Device / OS | iPad13,11 (T8103), iPadOS 16.3 / 20D47, Darwin 22.3.0 |
| Rootfs | macOS 13.4.1 / 22F82 |
| Repo HEAD | `7e5d771` (other workers' later commits); deployed package is the earlier `6e70353 + R008` build, not merged HEAD |
| Deployed scripts | `macos_gui.sh 471fc752…`, `postinst.sh ed42f478…` |
| Deployed libmachook | thin arm64e `9a511953…`, thin arm64 `151a2175…` |
| Deployed probe | `macwstccdprobe 430e3925…` (UUID 6B8EB001…) |
| Deployed tccd | `42c388e8…`, UUID `9845D17F-0409-3884-8A17-260C729636E5`, CDHash `1a32fd7e…` |
| Prior generation | tccd 45068 / WS 45211 (stopped after log preservation) |
| Fresh generation | tccd 48750 / WS 48891, `start coexist --no-vnc` exit 0 |
| Native health | tccd 883, SpringBoard 872, backboardd 905 |
| Thermal | 34.39 °C nominal before, 34.69 °C nominal after |
| Markers | both diagnostic namespaces absent throughout |

Prior-generation logs are in the device archive
`/var/mobile/macws_r003_evidence_20261018/prior_generation/`. The fresh
oslog is `r003-oslog.txt` (13 MB, 107,903 lines) in the same archive.

## Fresh generation and first real requests (FACT)

A full `oslog --info` stream was started **before** the controlled
`macos_gui.sh stop` / `start coexist --no-vnc`. The start exited 0.
`macws_final_composite.state` remained `state=missing` (producer=0,
sequence=0): this run has **process readiness only, no display/pixel
success**. Two finite postmin requests were sent; both returned
`SLSPostEventRecord -> rc=1002`.

The fresh WindowServer issued real TCC requests to tccd 48750 with
`msgID` 48891.16–21 and target tokens for several processes. The two
postmin senders were target PIDs 49186 and 49199:

```
WindowServer[48891] SEND msgID=48891.17 service=kTCCServicePostEvent
  target_token={pid:49186, auid:-1, euid:0}
tccd[48750] REQUEST sender_pid=48891 sender_uid=0 sender_auid=501
  msgID=48891.17
tccd[48750] ERROR responsibility_get_responsible_audit_token_for_audit_token()
  from PID[49186]: (#103) Policy not found
tccd[48750] ERROR Failed to create attribution chain from message.
WindowServer[48891] RECV dictionary count = 0
```

The same chain holds for `48891.19` (target 49199), and for the
ListenEvent checks of both targets. It also holds for `48891.20`
(ScreenCapture, target 49121) and `48891.21` (ListenEvent, target
48579 = `iconservicesagent runAsRoot`, a project launchd-hosted chroot
daemon). Every reply was `count = 0`.

**Critical distinction:** the XPC sender is WindowServer (48891), but
tccd's responsibility lookup runs on the **target token** (the process
being checked: the postmin injector, iconservicesagent, etc.). The
failing PID in the log is the target, not the WS sender. The failure is
a pre-policy attribution failure, not a WS-sender transport problem.

pidversion is **not** printed by the WS target-token descriptor
(`{pid, auid, euid}`); the 32-byte token is present in the request but
not logged. Correlation is msgID + target PID + tccd generation PID.
This is the exact capture limitation for pidversion.

## Hosted-probe A/B (FACT)

A project-owned temporary launchd job was loaded through the existing
launcher:

- label `com.macwsguide.r003-hosted-probe`;
- `launchctl print` resolves in both `system` and `user/501`;
- PID 49211, parent launchd via `/var/jb/usr/macOS/bin/launchdchrootexec`;
- request `msgID=49211.1`, service `kTCCServicePostEvent`,
  `target_token={pid:49211,...}`;
- tccd: `responsibility_get… from PID[49211]: (#103) Policy not found`,
  `Failed to create attribution chain`, reply `count = 0`,
  `TCCPREFLIGHT result=1`.

The same installed probe in the R002 SSH parent context also failed.
**Launchd hosting alone is insufficient**, and the project launchd-hosted
target `iconservicesagent` (48579) fails the same lookup when checked by
the WS. This is stronger than a probe-only observation.

## Upstream producing operation (FACT, exact disassembly provenance)

tccd arm64e (`42c388e8…`, UUID `9845D17F…`) attribution builder:

```
0x10004fb48  bl _responsibility_get_responsible_audit_token_for_audit_token
0x10004fb4c  cbz w0, 0x10004fbc4        ; success path
             ; nonzero: [x21,#0xa]=0, [x21,#0x18]=0, error log
0x100051a54  "ERROR: responsibility_get_responsible_audit_token_for_audit_token()
             from PID[%d]: (#%d) %{public}s"   (errno + strerror)
```

Failure then reaches the pre-created empty reply:

```
0x100006508  xpc_dictionary_create_reply -> x23
0x1000069ac  request-context-nil path
0x1000069c8  cbnz w0, 0x100007928 (logging gate)
0x100007928  bl 0x100052ca4   ; logs "Failed to create attribution chain from message."
0x100007930  b 0x1000069cc
0x1000069cc..0x1000069e0   send x23 unchanged
; no xpc_dictionary_set_* on this path -> count = 0
```

The runtime `(#103) Policy not found` is the responsibility subsystem's
errno/strerror result for the target audit token. The upstream producer
is the iOS responsibility policy lookup for the chroot target process;
the chroot processes in this environment (SSH-launched and
launchd-hosted) have no responsibility policy for tccd to resolve. This
is not a TCC grant decision and not an explicit `result=false`.

## Correction to R002

R002 inferred a "cached checker result" from zero tccd lines during a WS
post. That inference was not supported: zero logs alone cannot prove a
cache. R003 closes the gap with a fresh generation: the WS really does
send TCC requests for the postmin senders, and the same attribution
failure is captured on the real WS-sender path. The R002 server log,
hashes, UUID and disassembly provenance are retained above and in
`R002-tccd-oslog-window-20261018.txt`; the probe attribution finding is
confirmed, but it is now explicitly a property of the **target
process's** responsibility lookup, not a universal WS cause.

## Bounded next A/B proposal (not implemented here)

Because the failure is target-side responsibility, a grant or checker
change would be the wrong layer. Proposed next packet, one variable at
a time:

1. Establish a responsibility relationship for one chroot target PID
   (e.g. via `responsibility_set_pid_responsible_for_pid(target,
   responsible)`) from a helper with the exact required entitlement, or
   launch that target as a child of a RunningBoard-registered iOS app
   with an existing responsibility policy.
2. Re-run the same launchd-hosted probe and a fresh WS PostEvent check.
   Accept only a **populated reply** (log actual keys/types) and,
   separately, a visible input witness; process readiness alone is not
   acceptance.
3. If the lookup still fails, capture the responsibility subsystem's
   producer/policy database path with a bounded tool, not a fabricated
   token/session. Do not add blanket TCC overrides or edit grants/db.

No source/tests/build inputs were edited; no Git operation was
performed. The device is left in a healthy stopped state; the temporary
job was removed from both domains (SYS/USER prints exit 113), no
probe/oslog/debugger processes remain, and both markers are absent.

## Crash / final state

Run window 16:08–16:10 produced 40 new `.ips` reports: 37
`ExcUserFault_*` synthetic termination reports from the controlled
stop/start and client reconnects, plus real
`MTLCompilerService-161015`, `iconservicesagent-161016` and
`iconservicesagent-161058` reports. **No new WindowServer crash
report.** Thermal nominal 34.69 °C. Evidence archive:
`/var/mobile/macws_r003_evidence_20261018/` (prior generation, fresh
oslog, hosted-probe out/err/plist). Local artifacts:
`coordination/artifacts/R003-tccd-key-chains-20261018.txt` and
`coordination/artifacts/R003-tcc-fresh-attribution-20261018.json`.

(handover to codex now)
