# D154 — D153 corrections incorporated into the backend task packet

`coordination/_relay/devin-152-backend-task.md` updated in place —
now carries all five D153 corrections. This file remains the final,
self-contained D150b spec.

## Applied corrections

1. **D1 seq disambiguation** (I6): `record.seq` defined as *attempt
   identifier* allocated pre-claim, explicitly distinct from the
   slot-internal seq (claim index+1) — "ring bookkeeping never exposed
   to the decoder". Gap semantics reworded: "records that never reached
   the ring (bounded-drop or dead-mid-emit attempts) — never
   committed-record loss".
2. **D9 full enumeration** (I6): now lists armed acquire, errno save,
   `g_seq` fetch_add, clock_gettime, pthread_threadid_np, 2 relaxed
   loads, `g_head` acquire, bounded claim loop (tail acquire + weak
   CAS), TEST_HOOKS-only callback, fixed 64-byte **structure copy**
   (inline or memcpy, either nonblocking), release commit, `g_emitted`
   (+ `g_dropped` on refused path), errno restore.
3. **D5 lease contract** (I6, new): process-lifetime lease never
   released — dead consumer permanently disables draining (bounded, by
   design); no `d141_drain` from signal/async context even on owner
   tid (same-tid reentry unserialized).
4. **D10 split scope** (I6): in-candidate clause (no mach*/wrapped
   calls on the path + debug reentry-counter assert) vs integration
   constraint (same property in production hooks).
5. **A3 invariant line** (I6, new): only the head-CAS winner writes
   `s->rec`/`s->seq`.
6. **D7 per-leg errno** (I5 tests): assert errno unchanged across both
   `d141_mach_call` and `d141_mach_ret`, not just the shared emit
   envelope.

## Acceptance table updates

- R5 gains "per-leg errno tests pass".
- R6 now requires seven contract lines (D1/C3/A3/D5/D8/D9/D10) with
  the D1 disambiguation, D9 full enumeration, and D10 split-scope
  qualifiers named.

## Verification

`git diff` of the packet reviewed — only the I5 test line, the I6
contract block, and the two R-rows changed; all earlier consolidated
content (I1–I4, CAP totals, overlap schedules, forbidden scope,
commit-gate clause) untouched. No candidate/source edits, no tests
run, no device, no backend contact.
