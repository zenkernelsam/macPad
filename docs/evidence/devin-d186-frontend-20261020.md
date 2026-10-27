# D186 — re-review: final D184 latch implementation

Verdict: **ACCEPT** — both D185 findings resolved; latch placement,
exit paths, and test distinction all verified against working tree.

## F1 resolution — latch check present, correctly placed

`d141_svctime.c:70-73`: acquire-load of `g_uuid_valid` sits inside the
CAS-won window — after the UNINIT→INITIALIZING CAS (:61-63), before
`d141_hook_gen_init` (:75), before `memcpy(g_uuid,…)` (:78), before the
relaxed pid/gen stores (:80-82). Refused path release-stores
`g_state=UNINIT` (:71) then returns -1 — no stuck INITIALIZING.

The D183 schedule is now closed: second pre-gate caller re-wins the
window but hits the latch before touching `g_uuid`; the live flag can
never coexist with a writer inside the array.

## Exit-path audit (all four)

- CAS loser (:63): -1, state untouched — failure CAS does not mutate.
- Latch rejection (:71): release-store UNINIT — restores pre-window
  state; gate_init spin-observe-then-CAS unaffected.
- Success (:86-87): `g_uuid_valid=1` release BEFORE `g_state=UNINIT`
  release — order is load-bearing: any thread that CAS-wins on UNINIT
  necessarily observes flag=1 (same-thread release ordering). Reverse
  order would open a flag-visibility hole; this order is correct.
- Hook paths: latch precedes hook; CAS losers and latch refusals never
  reach `gen_pause` — no deadlock route in the test or the contract.

## Test distinction (D185 F2 resolution)

- U6a (:754-762): sequential second set BEFORE `arm()` → latch -1,
  uuid preserved — the actual D183 defect case.
- U6b (:776-780): post-gate refusal retained (ARMED CAS-loss).
- t21 (:835-868): deterministic — writer parked in `gen_pause` holds
  INITIALIZING; concurrent set → CAS loser -1 (:852); `get_uuid`
  still -1 mid-window (:855 — proves the loser published nothing);
  first tuple verbatim post-release (:860); post-publish pre-gate set
  → latch -1 (:863). CAS-loss vs latch-rejection distinguished and
  both exercised.

## Acquire/release and reset quiescence

- Reader `d141_get_uuid` (:91-98): acquire flag → verbatim copy /
  flag-0 → zeroed + -1. Paired with the release store inside the
  window — publication is correct; non-atomic `g_uuid` is written only
  while flag==0 and only by the sole window owner.
- Reset (:263): `g_uuid_valid=0` before `memset(g_uuid)` — a racing
  reader either sees flag=0 (safe -1) or flag=1 loaded just before the
  clear (torn stale bytes possible). Contract-excluded: header
  (:140-146) states reset "requires quiescent test threads and is NOT
  a production reset path". Wording is explicit — accepted.
- Header contract (:140-157) now documents first-set-wins, pre-gate
  AND post-gate -1, latch-release-only-via-reset, and the hi64
  provenance-only caveat (records carry gen/pid low64; hi64 is
  producer-declared, never record-verifiable — D175 wording).

Read-only review; no source edits, no build, no device, no commit.
