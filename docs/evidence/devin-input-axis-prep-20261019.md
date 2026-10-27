# Input-axis prep note — feasibility checks for the next backend packet

2026-10-19, macPad D0xx backend 8057. Read-only source/file analysis — no
device contact, no code changes. Pre-positioning for the coordinator's
declared next milestone (input), preserving the restored display chain.

## Known input state (from
`docs/evidence/ventura-22F82-input-transport-and-composite-20261013.md`)

- `SLSPostEventRecord` replies `1002` — the denial lands at
  `CGXSenderCanSynthesizeEvents(conn, cred)` inside `_XPostEventRecord`
  (SkyLight 22F82 +0x388e64): gate returns 0 → `mov w0,#0x3ea` →
  reply[0x20]=1002. Credential audit shows `gCurMsgAudit` populated
  (52-byte creds) yet the gate denies.
- The earlier "hook installed but never fires" anomaly is explained by
  the no-op-provider era — under today's provider the observation hooks
  need explicit permits.
- `com.apple.WindowServer.HIDEvents` never registers under
  `-virtualonly` (dead-name hypothesis for the separate dispatch path).
- `ws_evprobeG postrecord` on-device does a minimal SLS conn + 4 posts.

## Relocator feasibility of the named diagnostic targets
(checked against `analysis/SkyLight_22F82`, uuid
96676A53-B1E0-3D7E-B98B-B73873CD1880, at the documented image offsets)

| target | offset | prologue | verdict under current relocator |
|---|---|---|---|
| `CGXSenderCanSynthesizeEvents` | +0x318464 | `d503237f d101c3ff a9067bfd 910183fd` (pacibsp; sub sp; stp x29,x30; add x29,sp) | clean verbatim window + branch-back — permit-installable |
| `_XPostEventRecord` | +0x388e64 | `d503237f d10203ff 6d0323e9 a9045ff8` (pacibsp; sub sp; stp x8,x9; stp x29,x30) | clean verbatim window — permit-installable |

Both are position-independent prologues — a diagnostic permit + hook can
install at these exact points with today's provider semantics (no
PC-relative or terminal words in the stolen window). This unblocks the
doc §8.5 next action ("hook `_XPostEventRecord` for an entry counter +
read back the gate's first bytes") without touching the relocator.

## inputd posting path (source-confirmed)

`macwsinputd/main.c:2514-2555` — pointer events go out via
`CGEventPostToPid(eventTarget.pid, event)` when a target pid is known,
else `CGEventPost(kCGHIDEventTap, …)`. Tap-drag-up pairs are synthesized
with `MacWSCGEventLeftMouseUp`/`RightMouseUp` and window-under-pointer
fields are stamped (`WindowUnderMousePointer`,
`ThatCanHandleThisEvent`). Both APIs land in SkyLight's post path →
`_XPostEventRecord` → the 1002 gate — so inputd shares the single
gate-blocked transport, no second channel exists on this side.

## Constraints carried forward

- Gate observation ≠ fix: `CGXSenderCanSynthesizeEvents` returning 0 is
  the denial point; the question stays *why* (credential content vs the
  ≥0x34 cred-size gate vs conn lookup) — hooking lets us dump args/ret.
- The `.m` fprintf diagnostic path needs
  `/var/mnt/rootfs/tmp/macws_runtime_diagnostics` (chroot namespace) —
  §8.1. Provider `####` lines are raw `write(2)`, unaffected.
- Input validation remains an independent axis from display — keep the
  restored producer chain unmodified.
