# D183 — memory-model audit of the D180 UUID accessor contract

Verdict: **one real defect found in my own D180 spec** — the flag
ordering is correct; the immutability claim is not. `set_generation`
can legally run twice.

## Verified correct

- `g_uuid_valid` release-store after all data writes + acquire-load in
  `get_uuid` gives the required synchronizes-with edge — a reader
  seeing 1 sees the complete `uuid`/`pid`/`gen` triple. Plain
  16-byte `memcpy` is safe *because* publication goes through the
  flag. C11-legal, no torn read on the FIRST publication.
- `uuid_valid=1` observable before the `g_state=UNINIT` rollback is
  harmless — data is complete by the time the flag stores.
- Zeroed `out` on `-1`, errno untouched (no syscalls), accessor off
  the emit path — all correct as specified.
- U5 deterministic: `hook_gen_init` parks before ANY store →
  `get_uuid` returns -1 during the window, verbatim after — true by
  construction for the first set.

## F1 — re-set gap: uuid can mutate, and mutate under a live flag

`d141_set_generation` (:55-72) rolls `g_state` back to **UNINIT**
(:70) — the exact state its own CAS (:58-60) claims. A second call
arriving between rollback and gate decision **legally re-enters
INITIALIZING and overwrites `g_uuid`/`g_pid`/`g_gen`**. Refusal only
happens when the CAS loses (concurrent call) or the gate already
transitioned to ARMED/DISARMED.

Consequences:

1. My D180 "immutable: first set wins" is wrong about the current
   mechanism — re-set-while-UNINIT is legal today.
2. **Torn-read window**: during a second set's INITIALIZING window
   `g_uuid_valid` is *already* 1 → `get_uuid` reads `g_uuid` while
   its 16-byte `memcpy` is in flight → mixed bytes of two UUIDs,
   returned as valid. The flag protects publication, not overwrite.
3. U3's record↔uuid binding can break across a re-set (record minted
   under gen1 checked against uuid2) — self-defending downstream via
   E_MULTIGEN, but the accessor would mislabel which generation is
   "the" uuid.

## Minimal fix (recommended)

Forbid re-set using the same flag — inside the CAS-won window,
before the hook/stores:

```c
if (atomic_load_explicit(&g_uuid_valid, memory_order_acquire)) {
    atomic_store_explicit(&g_state, UNINIT, memory_order_release);
    return -1;                 /* generation already published */
}
```

This restores "first set wins" as an enforced property (not just a
documented-order convention), eliminates the torn window (no second
window ever exists), and needs zero new globals. It *does* change a
currently-legal-but-unused edge (re-set while UNINIT) — that is the
contract decision to make explicit in the packet: re-set was
self-defending downstream anyway (mixed gens → E_MULTIGEN).

Alternative (weaker): keep re-set legal — then every window must
clear `g_uuid_valid` before rewriting and republish (readers see -1
mid-window, never torn); but gen/uuid mismatches across the boundary
remain a lie about "the" generation. Not recommended.

## U6 spec correction

As written ("second set → -1, uuid unchanged") U6 is only true after
the gate decided — or after F1's once-check lands. With the fix, U6
runs correctly without arming the gate: second `set_generation` → -1
before touching any store, uuid byte-identical.

## Amended acceptance note for the milestone

- The flag design stands; add the once-check inside the window.
- U5 stays; add U9 (or fold into U6): concurrent second
  `set_generation` during the parked window → -1, flag and bytes
  unchanged — deterministically provable with `hook_gen_init`.
