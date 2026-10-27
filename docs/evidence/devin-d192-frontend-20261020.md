# D192 — special reply port ownership & cleanup audit

Sources: vendored xnu `libsyscall/mach/mach_port.c:441-478`,
`osfmk/kern/ipc_tt.c:1439-1474`, `osfmk/mach/message.h:328,1182-
1243`; exact 22F82 stub epilogue `SkyLight_22F82 @0x18536495c-49e0`.

## G2 wording — WRONG, corrected

D189 G2 said the SRP is "a send-once right; never a caller-allocated
receive right." Both halves are inverted:

- `thread_get_special_reply_port` (ipc_tt.c:1451-1474) allocates a
  port with `IPC_PORT_INIT_MAKE_SEND_RIGHT | IPC_PORT_INIT_SPECIAL_
  REPLY` — the task receives **both a send right AND a receive
  right** on it, bound to the calling thread.
- `mig_get_special_reply_port` (mach_port.c:453-465) is a thin
  TSD cache (`__TSD_MACH_SPECIAL_REPLY`) over that trap — per-thread,
  reused across RPCs.
- `MACH_MSG_TYPE_MAKE_SEND_ONCE` (message.h:328: "Must hold receive
  right") mints the **send-once right into the outgoing message** —
  the `msgh_local_port` field must name a port the caller holds a
  receive right on. The SRP is exactly that: a caller-owned receive
  right (+ send right).
- Corrected G2 wording: local = `mig_get_special_reply_port()` —
  the *thread* special reply port (task holds receive+send rights,
  thread-bound, TSD-cached); MAKE_SEND_ONCE mints the send-once
  right into the message. Never substitute a caller-allocated port —
  the option's SYNC_WAIT + SYNC_OVERRIDE/PROPAGATE_QOS handoff
  (`0x00304003`) binds specifically to the thread SRP.

## Cleanup classes — four, not two (stub :495c-49e0)

The epilogue decodes `w8 = kr − 0x10000002`, range-checks
`≤ 0xe` (:4964-496c), mask-tests `w8 & 0x4003` (:4970-4980):

| class | kr / condition | action | cites |
|---|---|---|---|
| valid reply | SUCCESS + id `0x750b` + size `0x2c` + bit31 clear + remote 0 + rc 0 | **none** — send-once consumed by reply; SRP stays TSD-cached | :4984-49e4 |
| malformed reply | SUCCESS, any validation fails | `mach_msg_destroy(reply)` — unconditional on every failure branch (covers header-carried remote_port right + descriptors); SRP **retained** | :49dc-49e0 |
| send-side reject | `kr ∈ {0x10000002, 0x10000003, 0x10000010}` = SEND_INVALID_DATA/DEST/HEADER | **nothing** — no destroy, no dealloc; SRP retained (message never left) | :4970-4980 + message.h:1182-1208 |
| all other kr | any other non-success (RCV_TIMED_OUT, TOO_LARGE, INTERRUPTED, PORT_DIED, out-of-range) | `mig_dealloc_special_reply_port` → `thread_destruct_special_reply_port(ALL)` on the thread SRP + clear TSD (+dealloc arg if ≠ SRP) | :49d0 + mach_port.c:468-478 |

G9's two-class split (destroy on malformed / dealloc on kr≠SUCCESS)
is wrong: it would dealloc the SRP on harmless send-side rejects and
misses that malformed-received replies keep the SRP.

## Note

`migport != srp` branch (:473-475) exists for the case the message's
local_port field was corrupted to a different name — defensive; the
raw sender passes the SRP it stored, never reads it back from the
received (possibly overwritten) header.

XNU source = adjacent-reference semantics; the 22F82 runtime facts
are the disasm rows above (exact offsets cited).
