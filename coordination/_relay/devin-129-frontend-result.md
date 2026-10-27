# devin-129 frontend result — causal timing design, frame-wait vs service messages

Read-only design audit; note at
`docs/evidence/devin-d129-frontend-20261019.md`. No source/device/Git
changes.

## Key determinations

- **Serialization is the loop, not a lock (FACT)**: `pace_lock` is
  libmachook-internal; the real blocking resource is `ws_main_thread`
  itself when a paced update runs inside `CGXRunOneServicesPass`
  (gen-50574 stacks). Per-generation tid recording required — three
  callsites differ (`:20055` unconditional vs `:27607/:27688`
  finish-depth-gated); which thread paces in gen-10447 is MISSING.
  Interaction wake socket only shortens the interval (VNC-activity
  source) — not a message wake.
- **Smallest record set (FACT-pinned VAs)**: pace
  enter/lock/start/exit/call (D107/D109 schema) + SkyLight
  `_post_port_data` `0x185421f5c`, `rendezvousHandler` `0x1851eb4a8`,
  demux `0x1851eb584`, `CGXPostReplyMessage` `0x1854213a4` (enqueue,
  not send), `push_out_message` `0x185421554` (send), msgid from
  `_current_message` `0x1d8ba5b88`. 32-B records, in-memory ring,
  `write(2)` flush outside measured region.
- **Probe**: `_XGetWindowServerTimebase` `0x74a7`, read-only; baseline
  + one bounded-wait arm, same generation/UUID/endpoint/thermal.
  Adaptation warranted only if exit-clustered dequeue AND median
  excess ≥ max(8 ms, 0.5×interval) AND replicated on services-tid;
  otherwise keep current behavior.
- **Honesty boundary**: kernel arrival unobservable; "arrived during
  wait" = send-phase + recv-edge clustering inference, labelled.
  Kernel-side arrival timestamp is the named MISSING artifact.
- **D113**: offline harness only, PENDING — never in-process.

(handover to codex now)
