# devin-140 frontend result — SkyLight timing instrumentation feasibility

Offline design; report at
`docs/evidence/devin-d140-frontend-20261019.md`.

## Result

- **Complete chain feasible.** All six needed entry boundaries verified
  as function starts with `pacibsp` prologues on exact UUID
  `96676A53…` (prologue constants extracted): `CGXRunOneServicesPass`
  `0x33C63C`, `_post_port_data` `0x338E48`, `rendezvousHandler`
  `0x1064A8`, `_CGXHandleMessage` `0x1064E0`, `CGXPostReplyMessage`
  `0x33C3A4`, `_push_out_message` `0x33C4FC`.
- **Three sites cannot be safely hooked** under the current
  permit/trampoline contract (mid-function, no branch-instrumentation
  contract): recv-call inside the pass, demux `blraaz`, in-push
  `mach_msg`. All have verified proxies — the recv/send edges ride the
  **already-installed** `mach_msg`/`mach_msg_overwrite`/`mach_msg2`
  interposes (zero new hooks) and impl-invoke is approximated by
  `_CGXHandleMessage` entry + `_current_message` `0x1d8ba5b88` read.
- **No existing permit covers these offsets** — six new
  (uuid,offset,prologue) registrations required; provider fail-closed
  otherwise.
- **Contract check passes**: monotonic ts (must pin `CLOCK_MONOTONIC`
  shared with pace records), measured services tid, msgid correlation,
  generation=pid+UUID; trampoline wrappers never touch return registers,
  ring slot released before orig — no locks across waits.
- **Minimum paired experiment**: two cold starts of one build
  (no-wait baseline vs one bounded interval; arming is install-time —
  isWS+sentinel discipline like SENDEROBS), ≥30 `0x74a7` sends/arm,
  join on tid+ts, score per max(8ms, 0.5×interval) rule.
- Net-new code = the service-half record implementation only; pace half
  already exists as the D107/D109 candidate.

(handover to codex now)
