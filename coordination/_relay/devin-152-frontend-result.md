# D152 frontend result — 2026-10-20

Consolidated D150b backend execution packet delivered:
`coordination/_relay/devin-152-backend-task.md` (self-contained;
wins over earlier drafts). Summary:
`docs/evidence/devin-d152-frontend-20261020.md`.

## Resolutions

- CAP totals: `emitted==511, dropped==1, drained==511, attempts==512`
  (D150 replay; D149's `CAP` figure superseded)
- Gate-spin observability mandatory: `g_gate_spun` counter or cas-fail
  hook — otherwise the retry path can't be provably exercised
- Overlap: `drain_started` latch + parked-producer prefix proof
- PASS_EXIT: contract route preferred (handled-count = dispatch-edge
  count inside the pass bracket — no API needed); API alternative kept
- Mach legs: D151 source rules — send-id pre-call snapshot (shared
  buffer in mach_msg, register unpack in msg2), RET msgid = recv id iff
  `SUCCESS && RCV`, TOO_LARGE conservative-0, pairing for async
  reentrancy not trap nesting
- T13 flaky-by-design confirmed → fixed via I1; review's T12
  stale-flag claim annotated as refuted

## Packet contents

I1–I7 implementation items, acceptance rows R1–R8, forbidden scope
(isolated candidate only — no production/permit/package/device/
sentinel/GUI/contact), commit only after reviewer acceptance of the
implementation milestone.

No edits/tests/builds/device/backend contact.

(handover to codex now)
