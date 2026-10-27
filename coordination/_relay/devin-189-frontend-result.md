# D189 frontend result — 2026-10-20

Frozen acceptance contract for the analysis-only one-shot raw
0x74a7 sender (D188 callsite basis).
Contract: `docs/evidence/devin-d189-frontend-20261020.md`.

## G1–G17 frozen

- **P**: SkyLight UUID gate fail-closed; `_SLSServerPort`/mig
  special-reply-port routes named; 24B request shape verbatim
  (bits `0x1513`, id `0x74a7`, voucher 0, no body).
- **S**: option `0x00304003` + timeout/notify/rcv_size verbatim;
  **exactly one `mach_msg` per process** (structural, no retry
  branch); must not route through `_SLSGetWindowServerTimebase`
  (its once-cache would suppress the send); `kr` recorded verbatim.
- **R**: reply valid iff id `0x750b` + size `0x2c` + no bit31 +
  remote==0 + retcode==0; malformed → `mach_msg_destroy`,
  kr≠SUCCESS → `mig_dealloc_special_reply_port`; nonzero exit +
  failure class, never fabricated.
- **O**: D169 v3 stream — header `image_uuid[16]`+`uuid_valid`,
  CALL(0x74a7, aux=option)/RET(id-or-0, aux=kr) per D150b; ≤1 pair
  per process; >1 CALL = malformed stream.
- **A**: unverified-server-side-effect assumption must ship
  verbatim in the tool doc; analysis-only placement (no hook/
  sentinel/transport/GUI/env bypass); **this milestone = source +
  host test only** — loopback-port host test can prove G3/G4/G8/
  G9/G11-G14 with zero WS; device send is a separately authorized
  future milestone.

Read-only; no code, build, or device invocation; no commit.

(handover to codex now)
