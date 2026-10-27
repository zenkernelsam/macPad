# D158 — D157 flood coverage applied to final backend packet

Scope: `coordination/_relay/devin-152-backend-task.md` edits only —
implements the D157 proposal verbatim. No candidate source/tests, no
builds, no device, no backend contact.

## Changes

1. **I2 retitled** "CAP-boundary refusal + saturation flood (T16)".
   New step 6 inserted between the first refusal and P's release:
   emit `M = CAP = 256` while P parked (tail=255, head=511) — every
   attempt sees `511−255 = 256 ≥ CAP` → drop. Asserts:
   `dropped == 1+M`, `emitted == 510`, `g_head == 511` (refusals
   never consume a claim index), second mid-park drain still 0.
   Steps renumbered 1–8.
2. **Amended totals**: `emitted==511`, `dropped==1+M`,
   `drained==511`, `attempts==512+M` — conservation
   `511 + (1+M) == 512+M` exact.
3. **D5 contract line extended** with the symmetric producer bound
   (D157): dead/stalled producer = permanent CAP-bound head-of-line
   block; drops counted; ring never corrupts; `g_dropped` is the
   honest stall record. Still seven contract lines — R6 unchanged.
4. **R2 row** updated to "CAP boundary + flood" with the amended
   totals and M=CAP requirement.
5. **Revision map** notes the D157/D158 addition.

## Arithmetic re-verified

`d141_svctime.c:103-107` — bound check precedes the CAS; a refused
attempt increments only `g_dropped`. During flood: `g_head` stays
511, `g_tail` stays 255 → every attempt deterministically drops.
Post-release: P commits (`emitted==511`), drain yields 256 records
in claim order.
