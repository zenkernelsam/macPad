# D168 result — measurement integration boundary audit (read-only)

Status: complete. Read-only audit — no source edits, builds, device
access, or product integration. Evidence:
`docs/evidence/devin-d168-backend-20261020.md`.

## Map summary

**Exists (committed `6d5f2e2`)**: 64B record schema, bounded MPSC ring
with contiguous-commit drain + single-consumer lease, sentinel-gated
init with retry, emit/wrap/leg API, and a decoder-side pairing model in
the test harness. Design rules fixed by D164/D166 (serialize-one-probe,
seq-gap voiding, non-success RET exclusion, local_port schema-absent).

**Missing, smallest-first**:
- A: canonical record-stream file format (header + `d141_record[]` +
  end marker) — trivial, offline.
- B: decoder implementing exactly the accepted join rules — gen filter,
  services-tid by PASS_ENTER density, open-span construction,
  non-success RET exclusion, seq-gap voiding, single-sender arm
  metadata substituting for absent `local_port`, per-arm median +
  exit-cluster + honest drop flagging.
- C: synthetic stream corpus proving every rule offline.

**Integration-gated (out of scope)**: six SkyLight permits, libmachook
hook install, sentinel arming ordering, drain consumer + transport
(D5 teardown caveat: drain-to-quiescence before exit — lease is
process-lifetime), D8 exclusivity, serialized probe sender, calibration
arm, cold-start scheduling.

## Proposed smallest reviewable milestone

**"svcdecode"** — offline decoder + stream format + synthetic corpus
under `analysis/`, same acceptance model as D150b. Zero permits/hooks/
device; it is the only pipeline component provable before any
integration gate, and it freezes the join semantics integration must
later satisfy.

No commit this round (read-only). Any later implementation commits only
after review acceptance, excluding unrelated WIP.

(handover to codex now)
