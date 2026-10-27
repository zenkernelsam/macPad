# D052 — sender-attribution control: empty reply traced one stage deeper

2026-10-19, packet `coordination/_relay/devin-052-backend-task.md`.
One bounded sender-side control. No WS restart, no deploy of production
code, no identity/auid setters, no TCC policy writes, no marker/source/
Git changes. Desktop observed read-only before/after.

## Headline result

An **iOS-launchd-spawned sender** (temporary disposable job
`com.macws.d052sender`, user/501 domain — same domain that publishes
`com.apple.macosbooter.tccd.system`) sent ONE byte-schema-identical
7-key `TCCAccessRequest` (preflight=true, `kTCCServicePostEvent`,
`target_token`=own token) to the genuine Ventura `tccd system`
(pid 10300, sha256 `42c388e8…`).

**Reply: `type=dictionary count=0` — empty again**, BUT the server-side
failure signature MOVED one stage deeper:

| sender | tccd os_log | stage |
|---|---|---|
| chroot probe 46525 (R002) | `responsibility_get_responsible_audit_token_for_audit_token() (#103) Policy not found` | stage 1 — responsibility |
| **iOS launchd helper 27108 (D052)** | `proc_pidpath_audittoken() (#2) No such file or directory` | **stage 2 — executable-path attribution** |

Both end `Failed to create attribution chain from message.` → the
pre-created empty `x23` reply (`0x1000069cc → send_message`,
D050). No `#103` was logged for the launchd-spawned sender —
responsibility registration passed; the chain then failed resolving
the sender's executable path.

## Raw evidence (device capture, ANSI stripped)

```
tccd[10300]: REQUEST: tccd_uid=0, sender_pid=27108, sender_uid=0,
             sender_auid=501, function=TCCAccessRequest, msgID=27108.1
tccd[10300]: REQUEST_MSG: msgID=27108.1, msg={
    require_purpose=<xpc_null>  service="kTCCServicePostEvent"
    function="TCCAccessRequest" preflight=true
    target_token={pid:27108, auid:-1, euid:0}
    background_session=false    TCCD_MSG_ID="27108.1" }
tccd[10300]: ERROR: proc_pidpath_audittoken() failed from PID[27108]:
             (#2) No such file or directory
tccd[10300]: ERROR: Failed to create attribution chain from message.
helper:      D052 REPLY type=dictionary count=0
```

Same-window healthy native reference (tccd[883], `UserEventAgent[864]`
→ `com.apple.tccd`, Microphone): attribution chain builds
(`requesting={TCCDProcess: identifier=com.apple.UserEventAgent …
binary_path=/usr/libexec/UserEventAgent}`), identity refusal still
produces a **populated** 6-key reply (`auth_value=0`,
`auth_error_code=2`, `result=false`, `auth_version=1`,
`auth_reason=1`, `auth_error_string`). A functioning path never emits
an empty dict — emptiness itself marks early bail.

## Helper identity facts (FACT, self-reported + server-confirmed)

- pid 27108, `euid=0 auid=-1` token — `launchd user/501` spawn does
  **not** confer `auid≥0`; auid is NOT a differing axis (correction to
  the D050 §5 sketch).
- `responsibility_*` getters SYMBOL_MISSING via dlsym on iOS
  (consistent with R004 — native getter unreachable to non-platform
  clients). **Gap**: no client-side proof of the helper's
  responsibility registration; the *server-side* absence of a #103
  error is the indirect evidence that stage passed.
- `sender_auid=501` in tccd's REQUEST line is connection-inferred from
  the domain, independent of the token's embedded `auid=-1`.

## Interpretation

FACT: empty reply producer = **attribution-chain construction in tccd**,
which needs BOTH (1) responsibility registration for the sender AND
(2) a kernel-resolvable executable path (`proc_pidpath_audittoken`).
Chroot senders fail (1); a `/var/jb`-resident iOS helper passes (1)
and fails (2).

THEORY (not proven): `proc_pidpath_audittoken` ENOENT is caused by the
executable living on the Dopamine rootless bind (`/var/jb/tmp →
/private/preboot/…/procursus/tmp`) — the job record shows the
preboot-resolved path — or by trust-admitted exec leaving no canonical
text-vnode path. Confirmed-vs- THEORY boundary is exactly here.

THEORY: this maps to why WS's own cached path never reaches tccd —
every chroot client (WS included) is `launchdchrootexec`-spawned and
fails stage 1 identically.

Confounders recorded: sender platform (iOS libxpc vs chroot), uid 0 on
both sides (helper ran root — launchd job had no UserName), request
schema is the Ventura 7-key legacy form (native senders use a richer
9-key SPI_VERSION=2 form — accepted by the Ventura daemon anyway per
REQUEST_MSG dump), no `pidversion` freshness concern (sync call while
alive).

## Packet corrections absorbed

- "absence of tccd logs ≠ WS cache hit" — stated in doc §WS-note.
- "auid/responsibility sufficiency not established" — confirmed:
  responsibility alone is insufficient; path attribution is the next
  gate. A non-empty reply would have supported, not proven, the
  attribution hypothesis; we got the different-error outcome instead.
- No `result=1` policy conclusion; preflight request is the read-only
  variant (verified it emits no consent/persist path — it is what
  `TCCAccessPreflightWithAuditToken` itself sends on the wire).

## Concrete next producing-layer experiment

Run the SAME helper binary from a canonical real-filesystem path —
e.g. `/private/var/mobile/macws_d052_sender` (data volume vnode, not
the rootless bind) — one bounded resend.

- `#2 ENOENT` gone, new error or populated `auth_*` reply → path
  resolution was the remaining stage; producing layer advances again.
- Still `#2 ENOENT` → path failure is not bind-path-specific (then
  inspect whether trust-admitted exec lacks a resolvable textvp at
  all — a jailbreak-trust artifact, fixable only launchd-side).
- Populated reply with `auth_error_code` → reaching identity/policy
  stage; only then does a TCC verdict field exist to reason about.

## Artifacts / hygiene

- `analysis/devin-d052/macws_d052_sender.c` sha256 `77dd7d67…`,
  unsigned binary `7216aaff…`, on-device signed `1cc8636e…`
- `analysis/devin-d052/d052-device-capture.txt`, `d052-oslog-segment.txt`
- 3 pre-send crash iterations recorded (dlsym PAC failure;
  xpc_copy_description null-node; `&_xpc_type_null` ≠ null object —
  real null object is `xpc_null_create()`). None sent a request.
- Cleanup verified: job bootout rc=0, binary/plist/tmp/crash files
  removed, no process, no job residue.
- Desktop: `ready producer=10447` seq 3619→3707 advancing, same WS
  generation; native tccd 883 + Ventura tccd 10300 + displayd 10540
  alive throughout.
