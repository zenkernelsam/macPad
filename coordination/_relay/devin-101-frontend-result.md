# D101 frontend result — deferred send, loop/pacing relation, label fixes

Same frozen image, offline static only. Evidence:
`docs/evidence/devin-d101-frontend-20261019.md` (raw disasm + offsets).

## Deferred small reply — actual send traced

`_gOutMsg` leaves the server at **3 possible first-hit points**, earliest
applicable wins:

1. pass-end flush: `CGXRunOneServicesPass+0x678` `bl push_out_message` @
   `0x185421cb4` → `mach_msg(MACH_SEND_MSG)` @ `0x185421554`
2. next pass head: `_gOutMsgPending` → `mach_msg_overwrite` combined
   **send+receive in one trap** @ `0x185421974` (option `0x3000903`),
   pending cleared at `0x18542197c`
3. displacement flush: a subsequent `CGXPostReplyMessage` calls
   `push_out_message` first @ `0x1854213d0` before overwriting the slot

`_push_out_message` has exactly 2 callers (raw opcode scan, capstone
missed one due to embedded data); `CGXPostReplyMessage` has 7 (the two
rendezvous reply blocks, connectionDeallocate, 3 postponeMessage
callbacks, connectionHandler block_3).

## Complete chain for a `0x74a7` query

`server_loop` `0x185422638` → `CGXRunOneServicesPass(0, 3600.0)` loop →
`mach_msg[_overwrite]` receive on port set `[0x1d8ba61c0]` (R1) →
`_post_port_data` `0x185421e48` keyed on `msgh_local_port` via
`_gPortDataNotes` CFDictionary → `CGSessionControlSelectSessionWithID`
gate → `blraaz` handler `0x185421f5c` (R2) → `rendezvousHandler` →
`CGXHandleMessage` → work block → demux `blraaz` `0x1851eb584` → impl
(R3/R4) → `CGXPostReplyMessage` enqueue `_gOutMsg` (R5) → flush at R6
sites above. Unhandled → `0x3e8` → `mach_msg_destroy`.

## Timing-label corrections (D100 errata)

- R3→R4 (rendezvous entry → impl `blraaz`) = **handler prelude**, not
  pre-entry queue wait. Queue residence is R0→R1 (kernel delivery →
  receive-trap return) — **not observable inside SkyLight**; the first
  SkyLight-visible receipt is `0x185421978`/`0x1854219a8`.
- `_CGXPostReplyMessage` on the small-reply path = **enqueue**, not
  send; send = `0x185421554` or `0x185421974`.

## Flush ↔ event loop ↔ pacing

Static: everything serialized on one services pass — request dispatch,
event-tap delivery (`post_event_tap_data`/`release_event_buffer` at
`0x1852d4024/0x1852d4570`), process reaper, session timers,
`CGXRunOneEventPass` (nonzero → nonblocking next pass, `fcsel` at
`0x185421744`). **No call edge** from the services/event-pass chain to
any CARender/IOSurface/IOMFB display or composite function — no shared
queue with the compositor. The only relationship: a long/busy pass
delays the next receive dequeue and the pending-reply flush alike
(single-thread serialization). No runtime causal inference drawn.

## Remaining gaps (named, not guessed)

- R0→R1 queue residence requires kernel/libdispatch-side visibility
  (outside image).
- `_count_vm_regions` `0x185421d80` used as pass elapsed/budget clock —
  name misleading; internals not needed for the boundary.
- `w20` bit `0x1c` timing-flag mode (`[0x1d6a4b408]`) toggles the
  timing-bracketed path at `0x185421b4c`/`0x185421bd0` — semantics not
  resolved; flagged only.

No device, no probes, no source edits, no builds/restarts/Git.

(handover to codex now)
