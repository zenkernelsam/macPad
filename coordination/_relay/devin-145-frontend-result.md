# D145 frontend result — 2026-10-19

Reviewer acceptance checklist for the D143 repair is ready, built from
frozen D144 findings + current D141 candidate only. No D143 files read,
no edits.

Checklist: `docs/evidence/devin-d145-frontend-20261019.md`.

## Shape

- **Gate A (reclamation, 3 rows)**: stop-at-hole drain — `g_tail` may
  advance only past a contiguous committed prefix; `g_skipped` becomes
  an always-zero invariant; wedged-producer stall bound must be
  documented with a test; no slot write without a live outstanding claim.
- **Gate B (deterministic adversarial, 3 rows, ★)**: parked-producer
  wrap needs a real injection point (barrier, not sleep); drain must run
  concurrently with producers with per-record pattern validation;
  sanitizer-green never substitutes for the exercised interleaving.
- **Gate C (publication, 3 rows)**: once-only gate latch; generation
  either single-atomic (`pid = gen>>32`) or enforced sequential-init
  contract with a racing test; armed-records-before-gen window closed.
- **Gate D (record semantics, 7 rows)**: claim-order-vs-seq contract;
  real unique-payload check (T7's vacuous `seen[]` banned); post-call
  pass-aux path; combined-trap distinct send/recv ids + timeout
  exclusion; drain single-consumer enforcement; nested-leg pairing;
  errno envelope on both legs; umbrella-trace mutual exclusion;
  no-blocking emit; record-path recursion bound.
- **8 global rejection rules**: any deterministic-hook absence,
  retained skip-past-hole, serialized-only tests, vacuous asserts,
  conservation-only reclamation "evidence", gen tear under declared
  order, missing contract lines, or "no race found" without the D144
  interleaving exercised → reject even when sanitizers pass.

Ready for use when backend delivers the formal D143 handover.

(handover to codex now)
