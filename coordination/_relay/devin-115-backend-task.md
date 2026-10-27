# D115 backend — close D114 admission/quiescence race in isolated prototype

Work only in `analysis/d113-trace-transport/` and task-local test artifacts.
Do not edit production sources, frontend candidate files, device state,
package/build/deploy/trustcache/restart state, or Git history.

D114's ordinary C0–C12 and D112 suites pass, but Codex reproduced a concrete
counterexample from a temporary instrumented copy: pause `d113_record()`
after its first `shutdown==0` load and before `in_flight++`; shutdown then
reports `d113_quiesced()==1`, `d113_free()==0`; resuming the producer triggers
ASan heap-use-after-free at the `in_flight` increment. Review report:
`coordination/reviews/D114-20261019.md`.

Repair the protocol without waiting under any caller lock. An in-object
atomic admission/closed transition must ensure a call that has already
started cannot be omitted from quiescence while teardown is admitted. Also
state the unavoidable external lifetime rule clearly: the owner must stop
and join/prevent all future API callers before freeing an object, because no
raw-pointer API can protect calls that begin after `free`.

Add deterministic barrier-controlled sanitizer tests for:

1. producer paused before admission while shutdown/close begins;
2. producer admitted and paused during record;
3. drainer paused at entry and during drain;
4. final drain cannot overlap a newly admitted drainer;
5. no free succeeds until admitted operations leave, and no wait/I/O occurs
   in lock context.

Preserve D113/D114 framing, timebase, errno, repeated-open, bounded-work and
single-drainer limits. Re-run D114 C0–C12 and all D112 reference/negative/
prototype controls, preserving true exits and failures. Keep production
candidate **PENDING**. Save evidence and stop with an independent final line:
`(handover to codex now)`.
