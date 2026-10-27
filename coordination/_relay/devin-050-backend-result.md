# D050 backend result — empty TCC reply producer traced (read-only)

2026-10-19. Executed `coordination/_relay/devin-050-backend-task.md`.
Read-only local analysis + existing artifacts only: no deploy, no WS
restart, no production/marker/source/Git changes, no new device posts.

## Producer attribution — RESOLVED

The EMPTY `count=0` reply is authored by the real Ventura system tccd
**at the attribution-chain layer, before any TCC policy evaluation**.

Runtime witness (R002 oslog artifact, same binary + same window):

```
tccd[45068]: ERROR: responsibility_get_responsible_audit_token_for_audit_token()
             from PID[46525]: (#103) Policy not found
tccd[45068]: ERROR: Failed to create attribution chain from message.
probe:        RECV: reply <dictionary { count = 0 }>
```

Static path independently re-verified this round on
`analysis/d050-tccd/tccd_22F82_live` (sha256 `42c388e8…` = R002 live
fingerprint, arm64e UUID `9845D17F-…`):

- `0x100006508` — sole `xpc_dictionary_create_reply` in the daemon →
  `x23`, created empty before request parsing completes.
- `0x1000064e8` — `get_string("function")`; missing → `0x67e4`
  "Refusing Request for missing function key" (the custom ping's
  early path, not a TCC request).
- `0x100006ba0+` — strcmp dispatch; `TCCAccessRequest` → `0x6d24`.
  The authentic request IS dispatched to a real handler.
- `0x1000069c8` — `cbnz w0 → 0x7928` (os_log gate): "Failed to create
  attribution chain from message." → `0x69cc` →
  `xpc_connection_send_message(conn, x23)` — empty x23 is sent
  unconditionally after the log attempt.
- Only one `xpc_dictionary_set_value` in the entire handler
  (`0x7968`) — most early bails emit the reply with zero keys.

## Failed hypotheses — all disproven

probe-schema (authentic 7-key request also empty), missing-handler
(REQUEST log + dispatch), transport failure (real dict returned),
reply decode (count=0 on the wire), explicit policy verdict (bail
precedes Result creation; no verdict field ever authored), uid-level
(uid 0 and 501 fail identically).

Producer = **attribution/identity**: sender audit token `auid=-1`,
no responsibility-policy registration — chroot spawns are detached
from the iPadOS responsibility graph. This is the `responsibility103`
layer, now observed in the server's own log, not inferred.

## Boundary notes

- The WS-side `SLSPostEventRecord → 1002` produced ZERO tccd lines —
  cached WS-side deny; the empty-reply path was never exercised live
  by WS in that window. Distinction preserved.
- Preflight `result=1` remains an API default (TCC `+0x93bc`: `w8,#1`
  before contact; conn-NULL `cbz` preserves it) — not a verdict.
- Helper trust/path facts (`/tmp` killed execs, `/var/jb/tmp`
  path-trusted) are this-device witnesses only.
- Observer stays uninstalled; installed libmachook = production
  `.theos` build; provider admission unchanged.
- D048 status wording preserved: isolated probe = helper-level only;
  real WS hook install still untested.

## Concrete next experiment (minimal, distinguishing)

Post the byte-identical 7-key `TCCAccessRequest` from a process WITH
responsibility attribution (iPadOS-launchd-spawned, e.g. an existing
iOS-side macws daemon or tiny helper; endpoint is system-domain).

- non-empty reply with verdict field → attribution sole blocker;
  fix = spawn-side responsibility/auid≥0 for chroot processes.
- same empty + same error → endpoint/identity requirements are the
  layer.
- non-empty error field → next producer downstream.

Exact missing input: a chroot sender whose audit token carries
`auid≥0` and a responsibility-policy entry — no current chroot spawn
has one.

Docs: `docs/evidence/devin-d050-empty-tcc-reply-producer-20261019.md`.
