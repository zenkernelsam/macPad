# D159 — T16 global-hook sequencing audit + fix

Frozen inputs: `d141_svctime.c` hook mechanics, `_test.c` T9/T12
patterns, updated I2 (post-D158). Packet amended; docs only.

## 1. Defect found in D158 T16

`d141_hook_post_claim` is a global function pointer
(`d141_svctime.c:36`, invoked at `:113` after *every* successful
claim, gated only by `D141_TEST_HOOKS`). My D158 schedule armed it in
step 2 and never cleared it — step 4's 255 fill emits would each
invoke `pause_hook` and spin on `hook_gate`: the test deadlocks in
its own instrumentation, not just P.

Ordering subtlety that makes the fix safe: the pointer is loaded and
called at claim time; clearing it does NOT unblock a thread already
inside the callback (P spins on `hook_gate`, a separate atomic). The
clear only prevents *new* claimants from entering. T9/T12 use
exactly this pattern — arm, wait `hook_entered`, clear pointer at
`:198`/`:295`, then fill, then release `hook_gate`.

Refused attempts are hook-free by construction: the bound check at
`:103-107` returns before the claim — so the refusal + flood steps
could never deadlock regardless; only the step-4 fill was at risk.

## 2. Corrected order now in the packet

1. Emit 255 committed (hook NULL).
2. Arm hook → P claims 255, `hook_entered`, spins `hook_gate`.
   Constraint stated: exactly ONE claimant may emit while armed —
   a second emitter would also park and corrupt the schedule.
3. `hook_entered` observed → main clears
   `d141_hook_post_claim = NULL`; P stays blocked in its in-flight
   callback.
4. Drain 255 → tail=255.
5. Fill 255 → claims 256..510 land, hook NULL, no block.
6. First refused attempt → `dropped == 1`.
7. Flood M=CAP → `dropped == 1+M`, `emitted == 510`, `g_head == 511`,
   second mid-park drain == 0.
8. `hook_gate = 1` → P commits → `emitted == 511`.
9. Drain 256, `[DEADBEEF, 255 followers]` order; mid-window drain
   pre-release == 0.

D158 arithmetic unchanged (`attempts == 512+M`,
`dropped == 1+M`, `emitted == 511`, `drained == 511`). All earlier
gates preserved.

## 3. Residual note for implementers

`hook_entered`/`hook_gate` naming reused from the test file's
existing `pause_hook` harness (:166-170) — no new hook type needed;
the I1 `d141_hook_gen_init`/`g_gate_spun` observability additions are
unaffected.
