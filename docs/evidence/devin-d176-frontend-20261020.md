# D176 — independent review: D175 full-UUID + suppression coverage

Verdict: **CHANGES_REQUESTED — single required gap**: `span_overflow`
has no fixture (explicit D174 requirement). Everything else audited
clean.

## Verified

**Full-UUID propagation (v3)**
- `d169_stream_hdr` + `d169_stream` carry `image_uuid[16]` +
  `uuid_valid` (:72-73, :109-110); file loader passes both verbatim
  and normalizes `uuid_valid` to 0/1 (:537-538); `D169_STREAM_VERSION`
  bumped 2→3 (:50) — v2 files reject at E_VERSION.
- Authoritative-vs-verified split is correctly labeled: comment at
  :124-127 states identity is the authoritative UUID, low64 is only
  the record-vs-UUID **consistency** check — exactly the honest
  model. `stream_identity` enforces: `rec.pid == hdr.pid` (:145),
  uniform gen (E_MULTIGEN), uniform per-record recovered lo64
  (:153), and `recovered lo64 == uuid_lo64(image_uuid)` (:160-161) —
  the upper 64 bits are producer-declared only, never pretended to
  be record-verified.
- Cross-stream 16-byte memcmp (:224-225); `image_uuid_set` filter
  requires BOTH streams uuid_valid AND equal (:229-232) —
  unverifiable stream under a set filter rejects, not passes.
- Unknown-UUID suppression incl. empty streams: `uuid_valid=0`
  → `image_unverified` (:227-228) → `scoring_suppressed` (:472);
  t20 covers same-lo64/different-hi64 reject, uuid/gen lo64
  mismatch, invalid ws uuid, and empty-probe-no-uuid.
- Consistency edge: empty stream with `uuid_valid=1` passes as
  producer-declared (no records to contradict) — acceptable and
  consistent with "authoritative producer field".

**Scoring suppression (D174 gate honored)**
- Order is right: all counters accumulate through the merge loop;
  suppression runs last (:472-477) — `probes_scored` moved to
  `suppressed_samples` (withheld, reported), stats block then sees
  `probes_scored==0` and produces no median. Honest accounting.
- Trigger set complete: `image_unverified || stats_unknown ||
  tids_capped || pend_overflow || span_overflow` — matches the D174
  gate "no affected sample scores after any truncation counter".
- t21 proves pend_overflow (>64 pending CALLs → 1 withheld sample)
  and tids_capped (>64 PASS_ENTER tids → suppressed argmax) with
  counters retained; t22 proves the call_absent/no_probe_call split.

**Allocation paths**
- `!mer || !spans` → `D169_E_ALLOC` (:284); seqidx alloc failure →
  `known=0` → `stats_unknown` → suppressed (soft-fail, deliberate).

**F2 fix retained**: deduped seqidx + footprint [min,max] vs
whole-stream index (t19 exercises the exact D172 construction).

## Gap (required before acceptance)

- **`span_overflow` has NO fixture** — D174 required it. It needs
  >1024 PM CALLs (`D169_MAX_PROBES`); trivially constructible in a
  loop (e.g., 1025 `CALL(PM)` records → assert `span_overflow>0`,
  `scoring_suppressed==1`, `probes_scored==0`, honest
  `spans_total==1024`). Untested code inside a fail-closed gate is a
  coverage hole, not a semantics dispute.

## Minors (non-blocking)

- Stats malloc failure at :481-488 leaves `median_ns==0` with
  `probes_scored>0` — inconsistent state not covered by
  `stats_unknown`; cosmetic.
- `reserved` header field is unchecked on load — forward-compat
  note only.

Once the span_overflow fixture lands and passes, this is an
ACCEPT-shaped candidate.
