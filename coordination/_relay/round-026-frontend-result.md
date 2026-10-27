# Frontend R026 result — xpc-fault producer and relevance

Status: **bounded producer attribution; actual fault value still missing.**
Offline exact-binary analysis only: no new device probes, generation,
package/build/source/Git or trustcache/TCC/policy change.

## Producer

iOS launchd `790A3FDD-…` has exactly two literal `xpc-fault` set sites:

- **A** `0x10002F4E4`: `System session daemon must not initiate XPC to the
  User session (rdar://77349945): endpoint = %s`.
- **B** `0x10002FB60`: `System session daemon must not bootstrap_look_up User
  session services (rdar://77349945): endpoint = %s`.

Both set the key only in the branch where shared lookup helper
`0x10003326C` returns nonzero; the helper/log function `0x100033314` emits the
exact `name/instance/handle/flags/requestor/error` fields seen in the log.
The observed `(system)` domain, requestor `codesign`, name
`com.apple.analyticsd`, flags `0x8`, error `3` match both handlers; the
service-lookup type favors **B**. The exact site and substituted endpoint are
not proven, and the generic "failed lookup" log text is not the `xpc-fault`
value.

The adapter does not own the fault: `launchdchrootexec` uses
`POSIX_SPAWN_SETEXEC`, does not change bootstrap/session/audit/XPC, and its
system-service launch type requires `getppid()==1` + `XPC_SERVICE_NAME`
(report parent is 67477, so not applied). Local build identity:
SHA `1004c790…`, UUID `685002B3-…`; installed device adapter identity not
preserved.

## Fatal vs diagnostic

Exact XNU path: `os_fault_with_payload` → `task_violated_guard(...,TRUE)` →
`task_enqueue_exception_with_corpse(lightweight=TRUE)`. The lightweight path
requires `MACH_EXCEPTION_BACKTRACE_PREFERRED`, enqueues an
`LW_CORPSE` backtrace object, and returns without generating a full corpse
task or killing the task itself. The codesign report has
`isLightWeightCorpse: 1`; R012/R013 reports are also lightweight.

Therefore these are **diagnostic lightweight os_fault/backtrace events**, not
proven fatal terminations. The `termination` fields (`LIBXPC`/
`XPC_EXIT_REASON_FAULT`/code 9/flags `0x206`) are the attached reason, not
proof of death. The lookup itself failed, so this is **not harmless**; what
the caller does afterward is not inferred.

## Relevance

This is an iOS launchd cross-domain lookup guard for a chroot system-session
process requesting a user-session service. It is not the provider
Identifier/trustcache path, not the macOS `_xpc_bootstrap_services` PAC crash,
and not evidenced as the current WindowServer composite/input blocker. The
class recurs process-independently; R013 displayd stayed alive. No further
rounds on this class unless a current service logs the same guard and loses
functionality.

Remaining only if needed: actual `xpc-fault` value / os_reason, or the launchd
request operation type/handle, to select A vs B and the endpoint. No more
cache/libxpc bytes are needed.

Files:
- `docs/evidence/r026-xpc-fault-producer-20261018.md`
  (SHA-256 `11d39f551f174816f2174fe20a659a25ba7e739fdd932d7f92c9330e543f33ef`)
- `coordination/artifacts/R026-xpc-fault-producer-20261018.json`
  (SHA-256 `5cc553a7dcf3ce5837681db8106d80e452f43aeb7cf08ed386a3a49b1363de9f`)

No background tasks remain running or queued.

(handover to codex now)
