# D161 — independent audit of D150b T18 timeout exclusion

Frozen inputs: `d141_svctime_test.c:647-713` (T18 + `pair_for`),
vendored xnu `analysis/xnu-xnu-8792.81.2/osfmk/mach/message.h`.

## Q1 — does `pair_for` exclude non-success RETs? NO

`pair_for` (:651-658) matches nearest unmatched same-tid
`D141K_MACH_CALL` — no predicate on the RET's `aux`/kr at all. The
timeout exclusion exists only in comments (:667 "is a leg, not a
pair", :696 "decoder rule excludes it"). No code enforces it.

## Q2 — does T18 assert exclusion? NO — it does the opposite

- :694 `pair_for(out, 3, matched)` mechanically returns out[2]
  (CALL 0x8000) — pairing *succeeds*.
- :695 `matched[p] = 1` — the timeout RET **consumes** CALL(0x8000).
  Availability of that CALL for a later reply is never asserted.
- :693 `CHECK(out[3].msgid == 0 && out[3].aux != 0)` asserts record
  contents only — true regardless of pairing behavior.

Result: a decoder that wrongly pairs timeout RETs and a decoder that
correctly excludes them produce identical T18 outcomes — the D145 D4
exclusion requirement has **vacuous coverage**.

## Q3 — `0x10000003` is the wrong constant

Vendored xnu `osfmk/mach/message.h`:
- `:1184 MACH_SEND_INVALID_DEST = 0x10000003` ← what T18 uses
- `:1231 MACH_RCV_TIMED_OUT   = 0x10004003` ← the real timeout
- `:1166 MACH_MSG_SUCCESS     = 0x00000000`

The literal isn't "TIMED_OUT-ish" — it's a *send-side* invalid-
destination error, a different failure class. `aux != 0` checks
still pass, so the test is green on a mislabeled value.

## Smallest deterministic test-only corrections

1. **Predicate in the decoder model** (preferred — `pair_for` is
   documented as "decoder-side pairing for the tests"): early-return
   `-1` when `recs[ret_idx].aux != MACH_MSG_SUCCESS`. Non-success RETs
   then never match; successful RETs (:666, :674-675) keep pairing.
   Alternative: gate at the call site (`out[3].aux == 0 ? pair_for : -1`)
   — weaker, leaves the helper modeling a wrong decoder.
2. **Assert exclusion + CALL availability**:
   - `CHECK(pair_for(out, 3, matched) == -1, "timeout RET unpaired")`
   - `CHECK(matched[2] == 0, "timeout does not consume its CALL")`
   - delayed-reply proof: emit `d141_mach_ret(0x8F00, 0)`, drain, assert
     it pairs to the still-free CALL(0x8000) slot — deterministic,
     barrier-free (records drained in order).
3. **Fix the constant**: either `#include <mach/message.h>` and use
   `MACH_RCV_TIMED_OUT` (host SDK guaranteed — `run_tests.sh` builds
   with the platform clang for macOS/arm64), or local
   `#define D141_TEST_MACH_RCV_TIMED_OUT 0x10004003` with a comment
   citing `osfmk/mach/message.h:1231`. Drop the "-ish" either way;
   update the aux-verbatim CHECK at :682 to the same symbol.

## Verdict

T18's timeout path is comment-asserted only; exclusion and
non-consumption are unenforced, and the kr literal is a different
Mach error than labeled. Two surgical additions (predicate + two
CHECKs + one delayed-reply emit) close the row without touching
`d141_svctime.c`.
