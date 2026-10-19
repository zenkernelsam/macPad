# D143 — ring/init lifecycle repair of the isolated svctime candidate

**Task**: `coordination/_relay/devin-143-backend-task.md` — repair the D141
MPSC ring/drain races and init lifecycle inside
`analysis/d141-svctime-candidate/` only; strengthen the adversarial tests.
No production/device contact.

## Reviewed defects (all real, all closed)

1. **`d141_drain` unconditional tail store (multi-consumer race)** — two
   consumers could both store `g_tail`, double-draining or losing records.
   → `d141_drain` is now **single-consumer**: first caller claims a
   process-lifetime tid lease via `g_consumer` CAS; foreign drains return
   `-1` and bump `d141_drain_refused`. Tail advance is then a single-owner
   release store — no CAS needed, no race. MPMC is no longer implied.
2. **Drain skipping past a claimed-but-uncommitted slot** — the old code
   skipped the open slot and advanced tail past it, freeing the slot for
   reuse while its producer was still writing → overwrite/data race.
   → drain now emits only the **contiguous committed prefix** and *stops*
   at the first uncommitted slot (`g_uncommitted++`); tail can never pass
   a slot a producer may still write. Slot reuse is therefore impossible
   mid-publish — closed structurally, verified deterministically by T9
   (barrier-paused claim) and T12 (wraparound).
3. **`d141_set_generation` raced the gate; `g_init` published before the
   armed decision** — concurrent gate callers could read `init=1,
   armed=0` = false stable disarm.
   → explicit `UNINIT → INITIALIZING → ARMED|DISARMED` state word. Sole
   initializer probes once then release-publishes the final state;
   concurrent callers spin (bounded) and always return the *final*
   decision (or `-1` if pathologically unsettled — observable, never
   silent-wrong). `set_generation` claims INITIALIZING across the pid/gen
   stores then rolls back to UNINIT — setup is serialized before the gate
   decision; post-init setup returns -1.
4. **Tautological MPSC test** (`seen[i]=1; CHECK(seen[i])` always true).
   → T11 now verifies *unique in-range msgid conservation*: every drained
   record's msgid is 1..800, no duplicates, drained==emitted, and
   `emitted+dropped == attempted` exactly.

## New adversarial coverage (T9–T14, barrier-controlled, no sleeps)

- T9 — producer claims + pauses in a post-claim hook; concurrent drain
  yields only the prefix, tail holds at the open slot; release → record
  drains exactly once in claim order.
- T10 — foreign consumer refused (-1), owner unaffected.
- T11 — 4×200 unique msgids: no duplicates, none out of range, no skipped
  slots after quiesce.
- T12 — tail stops at an open slot while head claims a consumed slot past
  it; order preserved across wrap.
- T13 — concurrent set_generation vs gate_init: coherent winner-take-all;
  if generation won, records carry the exact `(pid<<32)^uuid_low64` tag.
- T14 — 8 concurrent `gate_init` callers all observe ARMED; no
  false-disarmed under INITIALIZING.

## Results

- x86_64 `clang -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=address,
  undefined -DD141_TEST_HOOKS=`: **129 checks, 0 failures** (one initial
  self-inflicted harness deadlock — pause hook armed before a setup emit —
  corrected by arming hooks only before the producer's claim).
- x86_64 `-fsanitize=thread`: **130 checks, 0 failures**, zero race
  reports (includes T9–T14 exercising the repaired windows).
- arm64: real `-arch arm64` object produced — compile-level claim only.
- D141 record format/API preserved (kinds, record layout, emit/wrap
  signatures unchanged); `d141_drain` return type widened to `ssize_t` to
  carry the `-1` refusal — the only API delta, documented.

## Boundaries kept

`analysis/d141-svctime-candidate/` only. No production source, permit
tables, package, device, sentinel, build, or GUI actions.

## Documented liveness bound

A producer dying between claim and commit wedges reclamation (tail cannot
pass); bounded — the ring stops producing at capacity rather than
corrupting. Stated in README, not hidden.

Candidate remains offline **PENDING**.

(handover to codex now)
