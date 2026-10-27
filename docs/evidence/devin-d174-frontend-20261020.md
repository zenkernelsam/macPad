# D174 — D173 image-identity + cap fail-closed audit

Verdict: **CHANGES_REQUESTED** — F2's fix verified correct; two
fail-open surfaces remain plus one schema-bound identity limit that
must be labeled, not silently claimed.

## A. uuid_lo64 is a 64-bit identity — sufficient for lo64, NOT full UUID

- Encoding verified: `d141_svctime.c:66-69` `memcpy(&lo, uuid, 8)` →
  gen carries `uuid[0:8]` only. Recovered `image` (:125-128) =
  uuid_lo64 — correct arithmetic.
- The full 128-bit UUID is **schema-absent**: `d141_record` has no
  uuid field; `d169_stream_hdr` carries gen+pid only. The producer
  knows `uuid[16]` at `d141_set_generation` — the header could carry
  it at a format rev, but does not today.
- Consequence: two different SkyLight images sharing `uuid[0:8]`
  pass every check undetectably — collision is unlikely but
  the D165 requirement was "identical SkyLight image UUID" (128
  bits), and the code now *verifies* 64.
- **Label, don't invent**: report field/contract should read
  `image_lo64` / "uuid_lo64 verified; hi64 absent by schema".
  `image_unverified` (:217-218) currently means "unrecoverable" —
  it does not capture "verified-but-partial". Closure path for a
  later format rev: add `uuid[16]` to `d169_stream_hdr` (producer-
  side has it) → full-UUID compare; `image_id` filter then needs
  128 bits too.
- Verified correct in the meantime: `stream_identity` per-record
  image uniformity (:144-145), `rec.pid != hdr.pid` →
  E_IMAGE_MISMATCH (:136-137), empty-stream header recovery
  (:149-155), unrecoverable→flagged-not-claimed (:217-218),
  image_id fail-closed on unrecoverable streams (:219-222).

## B. `tids_capped` is fail-open — affected samples still score

- `resolve_svc_tid` :173-174: the 65th+ distinct PASS_ENTER tid is
  counted but skipped from argmax — that tid could BE the true
  services tid. `tid_state` still returns `D169_TID_OK`; every
  HANDLER then binds against a possibly-wrong `svc_tid` and samples
  **score normally**. Counter exists (:266) but never gates.
- Required: `tids_capped > 0` → degrade `tid_state` to a distinct
  `D169_TID_CAPPED`/flag that voids or marks scoring — argmax over a
  truncated candidate set is not evidence.

## C. `pend_overflow` / `span_overflow` corrupt pairing; samples still score

- `pend_overflow` (:334-335): overflowed CALL never enters `pend` —
  its later successful RET pairs the *previous* unmatched same-tid
  CALL (`:343-344` nearest-unmatched), closing a DIFFERENT span
  (scored on a mismatched close; latency value is handler−call so
  numerically unaffected, but pairing integrity is broken and the
  span's true RET then counts `orphan_ret`). Affected samples still
  score — no gate.
- `span_overflow` (:327-328): PM CALL beyond `D169_MAX_PROBES` opens
  no span and `spans_total` isn't counted — silent sample loss; its
  HANDLER(PM) then hits `open_span` → misclassified `handler_dup`/
  `orphan_handler` noise on an unrelated span.
- Required: any nonzero truncation counter → a `truncated` report
  flag that voids or marks affected samples (cleanest: refuse
  scoring for the decode, or at minimum mark all post-truncation
  samples suspect — the counters alone don't satisfy fail-closed).

## Verified correct

- **F2 fix is right**: `seqidx` dedupes (:83-87) so `present ≤ range`
  → `seqidx_missing` can't go negative; footprint [min,max] vs
  whole-stream index (:365-370) — records ts-sorted outside the span
  but present in-stream no longer void it; `UINT64_MAX` overflow
  guarded (:109); `known=0` → `stats_unknown` suppresses reconcile
  (:256-260).
- Reconcile direction still correct; `n` bound-checked pre-merge
  (:205-206); merge tie-break stable (:116-122).

## Fixture coverage check

Confirm a fixture exists for: capped-tid scoring blocked,
pend-overflowed CALL→RET mis-pair flagged not scored, span-overflow
PM unspanned counted, and (if lo64 label chosen) a same-lo64 fixture
documenting the residual. The audit's gate: **no affected sample may
score after any truncation counter increments.**
