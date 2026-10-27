# D185 — independent audit: D184 once-only publication repair

Verdict: **CHANGES_REQUESTED** — the headline D183 fix is missing;
U6 tests the easy case, not the defect.

## F1 — the once-check is NOT implemented (defect remains live)

`d141_set_generation` (:57-77) after D184: CAS → hook → `memcpy
g_uuid` → pid/gen → `uuid_valid=1` → rollback UNINIT. **No
`g_uuid_valid` latch check exists anywhere in the window.**

Replay of the D183 F1 schedule, still reachable:

1. set#1 completes → `state=UNINIT`, `uuid_valid=1`.
2. set#2 arrives before `gate_init` decides → CAS UNINIT→INITIALIZING
   **succeeds** (nothing refuses it).
3. set#2's `memcpy(g_uuid, uuid2, 16)` runs while `uuid_valid == 1` →
   a concurrent `d141_get_uuid` sees valid=1 and copies mid-write →
   **torn UUID (uuid1/uuid2 mix) returned as valid**.

The flag protects publication of the first set only — it does nothing
for overwrite, which is exactly what the latch was for.

Required placement (task's own wording): inside the CAS-won window —
**after acquiring INITIALIZING, before `d141_hook_gen_init` and before
any non-atomic write**:

```c
if (atomic_load_explicit(&g_uuid_valid, memory_order_acquire)) {
    atomic_store_explicit(&g_state, UNINIT, memory_order_release);
    return -1;
}
```

Every exit must release `g_state` — including this reject path, else
`gate_init` spins on a stuck INITIALIZING until bounded -1.

## F2 — U6 tests the CAS-loser case, not the pre-gate repeat

T19's U6 (:769) calls `set_generation` **after `arm()`** — state is
ARMED, so the CAS fails trivially. That is the post-gate refusal —
not the sequential re-set between rollback and gate decision that
F1 describes. The task's required distinction ("pre-gate repeat vs
concurrent CAS loser") is not exercised:

- **Missing test A**: sequential second `set_generation` with gate
  still UNINIT → must return -1 (post-fix), uuid unchanged.
- **Missing test B** (the U9 folded case): concurrent second set
  during a `hook_gen_init`-parked first window → CAS-loser -1 —
  distinguishable from A only if the latch exists.

## Verified correct in D184

- Retention: `g_uuid[16]` + `g_uuid_valid` globals (:33-38);
  `memcpy` BEFORE pid/gen stores; flag stored last inside the
  window — publication ordering correct for the first set.
- Accessor (:81-88): acquire flag → verbatim 16B + 0 / flag 0 →
  zeroed out + -1. No syscalls/locks; errno-free by construction.
- Reset (:253): clears `g_uuid_valid` before `memset(g_uuid)` —
  readers see -1 before bytes zeroed (test-only path; a concurrent
  reader mid-reset could still read torn pre-clear bytes — the
  reset contract already assumes quiescence; noted, not defect).
- U5/T20 (:789-813): deterministic parked-window -1 via
  `hook_gen_init` before any store — correct.
- U3 record↔accessor lo64 binding (:754-764) exercises the exact
  D175 decoder relation.
- README semantics: provenance wording correct ("hi64
  producer-declared, never record-verifiable"); check counts updated.

## Required for acceptance

1. The latch check inside the window (exact placement above).
2. Test A: sequential pre-gate second set → -1 (deterministic, no
   gate arm needed).
3. Test B or folded U9: concurrent second set during parked window
   → -1 — distinguishes latch-refusal from CAS-loss.
