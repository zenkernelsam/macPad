# D161 frontend result — 2026-10-20

T18 timeout-exclusion audit complete.
Report: `docs/evidence/devin-d161-frontend-20261020.md`.

## Findings

- `pair_for` has no success predicate — timeout exclusion exists only
  in comments (:667, :696).
- T18 *consumes* CALL(0x8000): the timeout RET mechanically pairs
  (:694) and marks it matched (:695); availability never asserted.
  Correct vs buggy decoders produce identical outcomes — vacuous D4
  coverage.
- `0x10000003` is `MACH_SEND_INVALID_DEST`
  (`osfmk/mach/message.h:1184`), not `MACH_RCV_TIMED_OUT`
  (`0x10004003`, :1231) — wrong error class under an "-ish" label.

## Minimal corrections (test-only)

1. `pair_for`: early `-1` when `recs[ret_idx].aux != MACH_MSG_SUCCESS`
   (models the decoder rule inside the helper).
2. Assert `pair_for(out,3) == -1` + `matched[2]==0`; add delayed
   `d141_mach_ret(0x8F00,0)` proving the CALL stays pairable.
3. `MACH_RCV_TIMED_OUT` via `<mach/message.h>` or `#define 0x10004003`
   with the message.h:1231 citation; drop "-ish".

No source/test edits, no builds, no device, no backend contact.
No commit.

(handover to codex now)
