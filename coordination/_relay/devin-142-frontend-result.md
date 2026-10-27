# D142 frontend result — 2026-10-19

Adversarial audit of D140 timing design + D107/D109 candidate complete.
Frozen inputs only; `devin-141-backend-*.md` files exist but were NOT read
(moving-files rule) — required follow-up once backend freezes them.

Report: `docs/evidence/devin-d142-frontend-20261019.md`.

## Verdict

D140 feasibility stands with four amendments; no boundary is unhookable.
All six pinned prologues decode word-by-word clean against the actual
`macws_insn_pc_relative`/`macws_insn_terminal` refuse masks (16-byte
verbatim window matches the 16-byte permit pins exactly).

## Confirmed deficiency (code level)

D107/D109 pace emit path has **zero errno handling** — `write(2)` can
clobber caller errno (EINTR retry loops read it). Needs the SENDEROBS
errno envelope on both legs.

## Design amendments

1. Combined send+rcv trap: send-edge ts = entry, recv-edge ts = exit;
   reply sent at pass-N entry may be for pass N−1's request — join by
   msgid, never pass order.
2. Reply join key = `(msgh_id−100, msgh_local_port)`; `_current_message`
   demoted to crosscheck (single global, mispairs on early-clear/nesting).
3. Per-message dispatch edge = rendezvousHandler entry; `_post_port_data`
   entry is batch-begin (N messages per dequeue).
4. Umbrella `MACWS_MACH_MSG_ALL` write(2) trace must be mutually excluded
   with a timing arm (self-distortion).

## Other bound hazards

- Identical 16-byte prologues at 0x33C3A4/0x33C4FC — safe under offset-
  keyed permits; assert offset uniqueness at registration.
- Mid-window inward branch target after verbatim steal — low risk;
  post-install smoke call through thunk before arm (matrix T18).
- Interpose coverage of SkyLight's own mach_msg_overwrite depends on
  binding order — needs one verification record, not assumed.
- Causality ceiling: positive result proves wait→dequeue-delay on
  services tid only; clustering needs the wait-free-pass control (§7).

## Test matrix

20 rows in report §8: prologue/permit refusal (T1–T3), errno/return
integrity (T4–T5), combined-trap edges (T6), timeout tagging (T7),
reply join/orphan (T8), ring wrap/arm flip (T9–T10), filtering/nesting/
batching (T11–T14), tid distribution (T15), trace mutual exclusion (T16),
record-path recursion (T17), thunk smoke (T18), no-syscall-in-window (T19),
overhead budget (T20).

(handover to codex now)
