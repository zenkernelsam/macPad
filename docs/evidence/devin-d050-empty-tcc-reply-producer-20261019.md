# D050 — empty TCC reply producer attribution (read-only)

Date 2026-10-19, packet `coordination/_relay/devin-050-backend-task.md`.
**Read-only analysis + bounded device inventory. No deploy, no restart,
no 4th hook, no production/diagnostic-marker/source/Git changes, no
repeated device posts.** Desktop untouched.

## 1. Verdict

**The EMPTY dictionary is authored by the real Ventura system tccd at
the attribution-chain layer — request rejection BEFORE any TCC policy
evaluation — not a denied verdict, not transport, not decode.**

Live witness (existing R002 artifact, same binary sha256
`42c388e8…`, same job/pid window), tccd's own os_log during the
authentic request:

```
tccd[45068]: REQUEST: sender_pid=46525 uid=0 sender_auid=501
             function=TCCAccessRequest msgID=46525.1
tccd[45068]: ERROR: responsibility_get_responsible_audit_token_for_audit_token()
             from PID[46525]: (#103) Policy not found
tccd[45068]: ERROR: Failed to create attribution chain from message.
probe:        RECV: reply <dictionary { count = 0 }>
```

The request is well-formed and *dispatched* — 7 keys
(`function=TCCAccessRequest`, `service=kTCCServicePostEvent`,
`preflight=true`, `target_token`={pid, **auid:-1**, euid:0},
`TCCD_MSG_ID`, `background_session=false`, `require_purpose=null`),
captured on the wire by LSXPC_TRACE. The daemon then fails to build
an attribution chain for a sender not registered in any
responsibility policy — this is the long-suspected `responsibility103`
layer, now observed directly in the server's own log rather than
inferred.

## 2. Static attribution in the real tccd (arm64e `9845D17F-…`)

Single reply-authoring site in the whole daemon:

- `0x100006508` — the ONLY `xpc_dictionary_create_reply` call → `x23`
  (empty by construction).
- `0x1000064e8` — `xpc_dictionary_get_string(msg,"function")`;
  `cbz x20 → 0x67e4` = missing-function path ("Refusing Request for
  missing function key." — the ping probe's different early path;
  it still ends at the same empty-send).
- `0x100006ba0+` — C `strcmp` dispatch chain:
  `TCCAccessRequest`→`0x6d24`, `TCCAccessRequestIndirect`→`0x6fc0`,
  plus 8 more function handlers. The request IS routed to a real
  handler — "missing handler" disproven.
- `0x1000069c8` — `cbnz w0 → 0x100007928` (os_log_type_enabled gate):
  logs **"Failed to create attribution chain from message."**, then
  `0x1000069cc` → `xpc_connection_send_message(conn, x23)` — the
  still-empty reply is sent **regardless of whether the error
  logs**. Other empty-sends at `0x6744`, `0x746c`, `0x78f0` share the
  pattern.
- Only ONE `xpc_dictionary_set_value` exists in the entire handler
  (`0x7968`) — empty replies are the common shape of every early
  bail, not a special encoding.

RE-verified locally this round against `analysis/d050-tccd/
tccd_22F82_live` (pulled read-only; sha256 matches the R002
fingerprint `42c388e8…`).

## 3. Failed hypotheses — all disproven by artifact evidence

| hypothesis | disproof |
|---|---|
| probe schema wrong | authentic `TCCAccessRequest` also gets count=0 (R008 wire dump) |
| missing handler | `REQUEST:` line logs function=TCCAccessRequest; strcmp chain routes it |
| transport failure | round-trip completes with a real dict object |
| reply decoding | count=0 is on the wire (LSXPC client-side dump), not a decode artifact |
| explicit policy verdict | no result/verdict field is ever authored; bail happens before `do_TCCAccessRequest` produces a Result |
| uid-level issue | uid 0 AND uid 501 callers fail identically (same auid-less spawn) |

**Producer = attribution/identity layer**: sender audit token carries
`auid=-1` and no responsibility-policy registration — chroot spawns
are detached from iPadOS's responsibility graph.

**Asymmetry worth noting:** tccd logs `sender_auid=501` (inferred from
the launchd job/session context) while the embedded `target_token`
has `auid=-1`. The responsibility lookup runs on the *connection's*
audit ("from PID[46525]"), not the embedded field.

## 4. Interaction with the WS-side 1002

`SLSPostEventRecord → 1002` emitted **zero tccd lines** in the same
window — WS's deny is cached/resolved locally and may never reach
this path live. The empty-reply producer explains what happens when a
request DOES reach tccd from an unattributed chroot process; it does
not by itself prove the WS cache's origin. (Record-only observer
still unfrozen for this distinction — no deployment authorized.)

## 5. Concrete minimal next experiment

**Discriminator: sender-side responsibility attribution.**

Send the byte-identical 7-key `TCCAccessRequest` from a process that
HAS responsibility attribution — i.e., spawned by iPadOS launchd
rather than `launchdchrootexec`. Cheapest vehicle: any existing
iOS-side macws daemon (e.g. `macwsinteropd`) or a tiny iOS-side
helper — the endpoint `com.apple.macosbooter.tccd.system` is
system-domain-registered and reachable from a natively-launched
process. One bounded post, `MACWS_LSXPC_TRACE`-equivalent reply dump.

Expected distinguishing outcomes:

- **non-empty reply with verdict field** → attribution was the sole
  blocker; the missing input is responsibility registration (or a
  session-associated `auid≥0` audit token) for chroot-spawned
  processes. Fix direction is then spawn-side, not TCC.
- **same count=0 + same responsibility error** → an iOS-spawned
  sender also lacks the needed registration → the private endpoint's
  service/identity requirements are the layer.
- **non-empty but different error field** → next producer downstream
  of attribution (service DB/session).

Exact missing input identified for a fix path: a chroot-spawned
sender whose audit token has `auid≥0` AND a responsibility-policy
entry — i.e., launchd-side spawn attribution, which no current
chroot daemon has.

## 6. Boundary honesty (per packet)

- Helper trust/path observations (`/tmp` execs die regardless of
  cdhash; `/var/jb/tmp` path-trusted) are **current-device witnesses
  on this Dopamine build**, not universal trust guarantees.
- `raw1`/`impl_rc=0` still collapses unresolved-checker vs
  evaluated-nonallow; the three observer hooks cannot prove explicit
  policy — unchanged.
- Current installed libmachook is the production `.theos` build; the
  observer candidate was never installed — provider-hook admission
  unchanged and irrelevant to this attribution.
