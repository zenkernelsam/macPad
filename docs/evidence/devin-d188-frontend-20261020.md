# D188 — exact-binary audit: 0x74a7 standalone sender path

Image: `analysis/SkyLight_22F82`, arm64e, UUID
`96676A53-B1E0-3D7E-B98B-B73873CD1880` (LC_UUID — matches the audited
RE input). All offsets are vaddrs unless marked fileoff.

Verdict: **a safe standalone path exists, with one load-bearing
caveat — the exported entry caches once per process.**

## Client callsite (RE-confirmed)

`_SLSGetWindowServerTimebase` — exported T @ `0x185364890`:

- **Client-only guard**: `getsectbyname("__SLSERVER","__slserver")`
  (:4a20-4a38) feeds a `!CGSRunningInServer()` assert (:4a50-4a60,
  file "CGSDisplay.cc"). Aborts if invoked inside WindowServer —
  correct side for a chroot client probe.
- **Mutex + once**: `ldaprb` once-flag @`0x1db438bf8` (:48dc);
  `std::mutex` @`0x1db438bf0` held across the send (:48f0 lock,
  :49ec unlock) — in-process serialization is built in.
- **Cache — the key fact**: flag @`0x1db438c00` set after first
  success (:49c8); on later calls `tbnz` @:48fc skips the entire
  send and returns `d0` cached @`0x1db438c08` (:49e4). **At most one
  `0x74a7` message per process lifetime** — repeat calls produce no
  IPC.

## Wire shape (RE-confirmed, :4904-49bc)

Request — header-only 24B `mach_msg`:
`msgh_bits=0x1513` (remote COPY_SEND, local MAKE_SEND_ONCE);
remote=`_SLSServerPort()`; local=`mig_get_special_reply_port()`;
voucher=0; **msgh_id=0x74a7** — literal qword @fileoff `0x39c490` =
`…a774 0000` (:4930-4938). Option=`0x00304003` =
SEND|RCV|SYNC_OVERRIDE|PROPAGATE_QOS|SYNC_WAIT (no timeout, no
trailer bits). `send_size=0x18, rcv_size=0x34`.

Reply validation: id must == **0x750b** (=0x74a7+100, MIG reply
convention — :498c); `msgh_bits` bit31 clear (no descriptors —
:4998); `msgh_size==0x2c` (:49a0 — hdr24+NDR8+rc4+payload8);
`msgh_remote_port==0`; retcode==0; payload = u64 @reply+0x24.
Error path is the standard MIG epilogue (dealloc special reply port,
`mach_msg_destroy`).

## Callsite inventory & tools

- In-image callers: `bl` @`0x1852258a0`, `0x18533ef80` — internal
  timestamp-conversion helpers (sentinel-init pattern).
- No existing tree tool sends `0x74a7` (grep of `misc/` — D167
  reconfirmed; `macos_ws_event_probe` posts `_XPostEventRecord`).
- Server-side `_XGetWindowServerTimebase` lives in the WindowServer
  binary — **not in tree**, not auditable here.

## Operational safety (inference — labelled, not binary fact)

Read-only shape: header-only request, fixed scalar reply, no rights
moved either direction, Apple-designed as idempotent (the cache is
proof). "No WS state change" is inference from message shape +
getter semantics; the handler binary is absent, so it is not
RE-confirmed. Sending carries the same surface as any client RPC on
`_SLSServerPort`.

## Probe implications

- Per-process cache ⇒ the exported symbol yields **one send per
  process**. For ≥30 serialized sends: fresh process per send
  (serialization trivially satisfied by launch order — fits D166's
  one-in-flight rule), OR emit the raw shape directly
  (0x18/0x1513/special-reply-port/id 0x74a7, expect 0x750b/0x2c).
- Mutex means concurrent in-process callers serialize — no ordering
  hazard even if the wrapper were reused.
- The assert path means the probe must run client-side only — never
  inside WS.
