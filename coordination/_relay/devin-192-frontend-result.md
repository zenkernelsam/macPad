# D192 frontend result — 2026-10-20

Special-reply-port ownership/cleanup audited against frozen source.
Report: `docs/evidence/devin-d192-frontend-20261020.md`.

## Corrections to D189 (both confirmed against xnu + 22F82 disasm)

**G2 wording inverted** — the SRP is NOT a send-once right:
`thread_get_special_reply_port` (ipc_tt.c:1451-1474) hands the task
**receive + send** rights on a thread-bound port;
`mig_get_special_reply_port` (mach_port.c:453-465) TSD-caches it.
MAKE_SEND_ONCE (message.h:328, "Must hold receive right") mints the
send-once right **into the message** — the local name must hold a
receive right, which the SRP is. Constraint restated: use the thread
SRP because the option's SYNC_WAIT/QoS handoff binds to it — never a
caller-allocated port.

**G9 two-class → four-class cleanup** (stub :495c-49e0):
1. SUCCESS + all validations → none (send-once consumed; SRP
   TSD-retained).
2. SUCCESS + any validation failure → `mach_msg_destroy(reply)`
   unconditionally; SRP retained.
3. `kr ∈ {SEND_INVALID_DATA, SEND_INVALID_DEST, SEND_INVALID_HEADER}`
   → nothing — send-side reject, no reply buffer, SRP retained.
4. All other `kr` (incl. RCV_TIMED_OUT/TOO_LARGE/INTERRUPTED/
   PORT_DIED) → `mig_dealloc_special_reply_port` = destruct ALL
   rights on thread SRP + clear TSD.

Class 2 keeps the SRP where G9 implied none; class 3 is a new row
G9 lacked (dealloc there would churn a pristine port).

Frozen-source audit only — no D190 files touched, no edits, no
device, no commit.

(handover to codex now)
