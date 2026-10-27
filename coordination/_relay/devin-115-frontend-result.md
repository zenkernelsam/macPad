# devin-115 frontend result — D114 lifecycle admission/quiescence audit

Read-only independent audit complete. No files under
`analysis/d113-trace-transport/` or production were edited; no device,
build, or backend contact. Full report:
`docs/evidence/devin-d115-frontend-20261019.md`.

## Verdict on the boundary

Reproduced race independently confirmed by source inspection:
`d113_record` check-then-increment at :109→:113 leaves an invisible
window before `in_flight`; `d113_quiesced` :245–249 can't see the parked
caller; `d113_free` :265–270 proceeds → UAF.

The atomic in-object gate CAN prove monotone quiescence — once `closed` is
set, no admit transition raises `in_flight`/`drain_active` from 0, so
`quiesced()==true` is stable until free. It CANNOT protect a caller parked
before its first `*t` dereference (P0) — the admit word lives inside the
freed object. That segment requires the external owner contract: stop and
join all API callers and make `t` unreachable before `free`. A counter
alone does not prevent UAF; an atomic admit converts the check→increment
hole into the entry→first-atomic hole, which only the owner (or an
immortal-storage admit gate) closes.

## Boundary findings beyond the reproduced race

- `d113_drain` reads `t->fd` at :233 before admission; `d113_drain_close`
  :241 closes unconditionally — fd-alias misdirection, not just UAF.
- Drain path never consults `shutdown`/`closed` — drainer can admit
  post-quiesce, overlapping `d113_drain_final` (:254–255), which itself
  does not register in `drain_active`.
- `d113_free` check-then-act → two concurrent frees double-free; needs a
  single-shot teardown-phase CAS plus owner serialization.
- `d113_shutdown`/`d113_quiesced`/`d113_counters` on stale `t` — same P0
  class; contract-only.

## Barrier-test assessment

Backend's 5 tests cover P1, counted in-flight, drainer-entry, drain-final
overlap, and lock-context refusal. Spec hazard in T1: if the pause is
pre-first-touch and `free()` completes during it, the test can only pass
under the external contract — it should be specified as such or the
resume-after-free variant documented as the excluded case. Missing
interleavings: double-free/free-vs-final-drain, drain_close vs mid-write
fd alias, drain_open vs active drain, post-shutdown drain admit, stale-`t`
shutdown/quiesced.

Candidate remains PENDING; no production/device acceptance claimed.

(handover to codex now)
