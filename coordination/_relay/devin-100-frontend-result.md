# D100 frontend result — exact-SkyLight service-dispatch timing boundary audit

Offline static RE on `analysis/SkyLight_22F82` (UUID
`96676A53-B1E0-3D7E-B98B-B73873CD1880`, base `0x1850e5000`). No device,
no instrumentation, no source/build/Git writes. Full raw disasm +
evidence: `docs/evidence/devin-d100-frontend-20261019.md`.

## Request chosen

`_XGetWindowServerTimebase` — **msgid `0x74a7`**, impl `0x18543e148`,
`CGXWindowServer` subsystem (`0x1dd49f2c8`, ids `0x7468–0x74a8`, 65
routines, stride `0x28`, header `{server_routine, startID, endID,
maxsize 0x1034}`). Read-only query; peers noted (`0x74a8` GetDebugInfo,
`0x7480` GetSessionReadOnceData, `0x74a0` ConnectionsForPid).

## Boundaries established (all static, all verified)

- **receive**: `CGXEnableWindowServerPort` `0x1851eb470` →
  `_register_for_port_data_in_session` `0x185420f60` inserts
  `rendezvousHandler` `0x1851eb4a8` into `_gPortDataNotes`
  `0x1d8ba61e8`; handler → `_CGXHandleMessage` `0x1853beaa0`.
- **dispatch**: `_CGXHandleMessage` stamps `_current_message`
  `0x1d8ba5b88` (msgid) + `_current_connection_port` `0x1d8ba5b98`,
  allocates 32 KiB stack reply buffer, `blraa` work block
  `0x1851eb4e0` → `CGXRendezvous_server` pre-dispatch → reply header
  (`reply.msgh_id = req+0x64`, `reply.remote = req.remote_port`) →
  `blraaz` into table routine.
- **service**: impl validates `!COMPLEX` + `msgh_size==0x18`, calls
  `mach_timebase_info`, writes reply `+0x24`, `RetCode=0`,
  `msgh_size=0x2c`; error `-304` on bad size/complex.
- **reply**: RetCode gate at `0x1853beb58` (MIG no-reply convention) →
  `_CGXPostReplyMessage` `0x1854213a4` → small simple replies batched
  into `_gOutMsg`/`_gOutMsgPending` (`0x1d8ba61f0/1f8`), large/complex
  → direct `mach_msg(MACH_SEND_MSG)` `0x18542141c`.
- **thread/queue**: session-serial queue via the port-data mechanism;
  the dispatch_mig_server registrations in this image serve only the
  CGY notification subsystems (HMD `0x1854761a0`, UpdateInfo
  `0x1854482b0`, DisplayStream `0x1854479e8`), not CGXWindowServer.

## Future bounded timing observation

Bracket `rendezvousHandler` entry (T0) → `blraaz` impl dispatch
`0x1851eb584` (T1) → `_CGXPostReplyMessage` send/enqueue (T3): T0→T1 is
the closest measurable proxy for server-side queue+prelude wait, T1→T3
service+reply. **Client send→reply elapsed is NOT server queue delay** —
it additionally includes kernel delivery, source-wake scheduling,
reply-port wake, and `_gOutMsg` batch deferral.

## Named missing inputs (not guessed)

- libdispatch mach-receive source internals (arrival→wake latency) —
  not in `analysis/`.
- kernel `mach_msg` timing semantics — outside SkyLight image.
- `_gOutMsg` flush cadence (`_push_out_message` callers) — flagged as a
  bounded follow-up; does not affect the request-path boundaries.
- `CGXEnableWindowServerPort` has no direct `bl/b` callers (indirect
  session-init invocation) — enabling moment unattributed statically,
  request-path boundaries unaffected.

No source edits, no instrumentation, no device access performed.

(handover to codex now)
