# D157 frontend result — 2026-10-20

T16-vs-A2 coverage audit complete (arithmetic/proposal only).
Report: `docs/evidence/devin-d157-frontend-20261020.md`.

## Verdict: partial coverage

- T16 proves the bound *trigger* — first refused `c+CAP` claim at
  c=511 with exact arithmetic; pre-claim refusal verified at
  `d141_svctime.c:103-107` (drops never consume a claim index).
- A2's *flood* property is unexercised: T16 refuses once then
  releases. A bug dropping the first overflow correctly but
  corrupting on the second would pass.

## Proposed amendment (for packet owner)

Insert step 5b before release: emit `M = CAP` more while P parked →
assert `dropped == 1+M`, `emitted == 510`, head unchanged, optional
second mid-park drain == 0. Amended totals `attempts == 512+M`,
`emitted == 511`, `dropped == 1+M`, `drained == 511` — conservation
exact. Plus one contract line: dead/stalled producer = permanent
CAP-bound block; drops counted; ring never corrupts.

No packet edits (proposal-only task), no source/tests/builds/device/
backend contact. No commit.

(handover to codex now)
