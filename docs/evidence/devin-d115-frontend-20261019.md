# D115 — independent lifecycle admission/quiescence audit (frontend)

Task: `coordination/_relay/devin-115-frontend-task.md`. Read-only audit of the
frozen D114 boundary in `analysis/d113-trace-transport/`. No edits, no device,
no backend contact. Sources audited: `d113_transport.c` (rev as of review),
`d113_transport.h`, `d113_adapter.c`, `coordination/reviews/D114-20261019.md`,
`docs/evidence/devin-d114-backend-20261019.md`.

## 1. Race-window enumeration (independent of the review)

Teardown sequence (owner): `d113_shutdown()` :260–263 → `d113_quiesced()`
:245–249 observes `shutdown && in_flight==0 && drain_active==0` →
`d113_drain_final()` :252–258 → `d113_drain_close()` :238–243 →
`d113_free()` :265–270.

### Producer (`d113_record`, :107–168)

- **P0 — suspended before the first `*t` dereference (:109).** The `!t` test
  at :108 reads the pointer *value*, not the object; the first touch of `t`
  is the `shutdown` load at :109. If the owner completes teardown while a
  caller is parked between entry and :109, the resumed load is a UAF *read*.
  **No in-object gate can close this window** — the admission word lives
  inside the freed object. This is strictly earlier than the reproduced race
  (which paused between :109 and :113).
- **P1 — suspended between :109 (shutdown==0) and :113 (`in_flight++`).**
  The reviewer's ASAN counterexample. An atomic admit transition (single
  RMW on a packed `closed|in_flight` word, or CAS-admit) makes P1's
  *logical* invisibility impossible: the RMW lands either before the
  owner's quiescence read (caller is counted → free refuses) or after
  `closed` is set (caller backs out). **But** if `free(t)` ran during the
  pause, the admit RMW itself is the UAF write — the atomic gate converts
  P1 into P0, it does not eliminate it.

### Drainer (`d113_drain_fd` :223–229 / `d113_drain` :231–236)

- **D1 — same admission hole**: `drain_active++` at :225 has no `shutdown`
  consult at all. A drainer suspended between entry and :225 is invisible to
  quiescence; worse, a drainer can admit *after* `shutdown==1` (no closed-bit
  check on the drain path), so `drain_active` can go 0→positive post-quiesce
  — concurrent with `d113_drain_final` → two consumers on the deq CAS →
  non-atomic interleaved `write(fd)` (:210) → frame interleaving/corruption
  beyond the documented single-drainer non-claim. The admit gate must cover
  the drain path too, and drain admission must consult `closed`.
- **D2 — fd read outside admission**: `d113_drain` reads `t->fd` at :233
  *before* any counter. Concurrent `d113_drain_close` (:241 closes `t->fd`
  unconditionally) or `d113_drain_open` (:182 close + :190 reassign) can
  close/rebind that fd between the read and `write()` — **fd-alias data
  misdirection** (trace bytes into an unrelated reuse fd), not memory
  corruption but integrity loss. `drain_close` has no quiescence check.

### Final drain / close / free (owner-side)

- **DF1 — `d113_drain_final` is invisible to `drain_active`.** It calls
  `drain_loop` directly (:255) after quiesced (:254); it does not register
  itself. A second concurrent owner call (`d113_free`) still sees
  `drain_active==0` → frees → `drain_loop` CAS on `t->deq`/`t->slots` = UAF.
  Even a correctly gated drainer can admit between :254 and :255, since
  nothing marks final-drain-in-progress — gate must reject drain admits once
  teardown phase ≥ quiesced.
- **F1 — `d113_free` check-then-act (:267→:268).** Two concurrent
  `d113_free` calls both observe quiesced → **double-free**. A single-shot
  teardown-phase CAS inside `t` (quiesced→freeing, set before `free`) makes
  the second refuse deterministically — cheap in-object win — but the
  pre-first-touch caller (P0 class) still needs the external contract.
- **S0/C0 — `d113_shutdown` (:262) and `d113_drain_close` (:241) on a stale
  `t` are UAF writes.** Same P0 class: even the "harmless" flag store
  dereferences the object.

## 2. What an atomic in-object gate CAN prove

1. **Monotone quiescence**: once `closed` is set, no admit transition can
   raise `in_flight`/`drain_active` from 0 → a `quiesced()==true`
   observation is stable until free; `d113_free`/`drain_final` can rely on it
   *against already-started callers*.
2. **Bounded admission** (single RMW/CAS): satisfies the "no wait under the
   pacing lock" constraint.
3. **Frozen ring for final drain**: quiesced ⇒ no producer can publish and
   no drainer can admit (if drain admission is gated too) → `drain_final`
   sees a stable ring.

## 3. What STILL requires the external owner

- **P0/pre-first-atomic**: a caller parked before its first `*t` access is
  unobservable and unprotectable by any object-resident state. The owner
  must *stop and join* all API callers AND make the pointer unreachable for
  future calls before `free` — "no raw-pointer API can protect calls that
  begin after free", exactly as the backend task states.
- **Owner serialization**: `drain_open`/`drain`/`drain_final`/`drain_close`/
  `free` must be one-owner-thread, non-reentrant. Counters cannot prevent
  two quiesced observers (double-free) or fd-alias.
- The only in-code shape that shrinks P0 to zero: an admit word in
  *immortal* storage (e.g., a static gate, or the adapter's own gate word
  freed only after the same join) checked before `t` is touched — it moves
  the problem one level out and bottoms out only at .bss-immortal storage
  or a callsite lock. Worth stating as the alternative; the external rule
  is mandatory either way.

## 4. Proposed barrier tests vs failure modes

Backend task's 5 tests map as: T1→P1, T2→counted-in-flight, T3→D1,
T4→DF1/drainer-admit-post-close, T5→refuse+no-lock-wait.

**Coverage gap / specification hazard in T1**: if the producer's pause is
*before* `*t` first touch and the test lets `free()` complete while paused,
the resumed admit UAFs under ANY in-object fix — the test can only pass
under the external contract (callers joined before free). The suite should
therefore specify T1's barrier as "resumed after shutdown but before free
is attempted by an owner that joined callers", or explicitly classify the
"resume-after-free" variant as the documented excluded case — otherwise the
test demands what no in-object gate can deliver.

**Missing interleavings not in the 5 tests**:
- M1: concurrent `d113_free` ×2, and `drain_final` vs `d113_free` —
  double-free / UAF-in-drain_loop (needs teardown-phase CAS + owner contract).
- M2: `d113_drain_close` vs mid-drain `write(fd)` — fd alias (:210/:241).
- M3: `d113_drain_open` reopen vs active `d113_drain` — fd swap (:182/:233).
- M4: post-shutdown `d113_drain` admit (drain path never reads `closed`).
- M5: `d113_shutdown`/`d113_quiesced`/`d113_counters` on stale `t` — P0
  class, contract-only.

## 5. Explicit limits of this audit

- Static audit only; the reproduced race is trusted from the reviewer's
  verbatim ASAN output (temporary instrumented copy — repo source unchanged,
  verified: :109–118 still carries the check→increment pair).
- No claim that a packed-word or CAS-admit is THE correct repair — backend
  owns the choice; this audit bounds what any such choice can prove.
- No production/device acceptance implied; the candidate remains PENDING.
