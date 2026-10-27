# D170 — frozen svcdecode offline acceptance gates (audit of D168/D169)

Scope: freeze reviewer checklist for the proposed `svcdecode`
milestone (D168 map → D169 implementation task). No implementation,
no fixtures built, no device.

## Contradictions / omissions found (before checklist)

- **C1 span-tid wording (D168 §B)**: "HANDLER_BEGIN(0x74a7) on the
  same tid" is wrong if read as same-tid-as-CALL — CALL legs live on
  the *probe* process's tid, HANDLER on WS's services tid, in a
  *different ring/stream*. Gate must pin: span edges may cross
  streams; "same tid" applies only inside the WS-side edge chain
  (PASS/HANDLER/IMPL/ENQUEUE/FLUSH).
- **C2 CALL-absent span**: client legs exist only if the probe
  process is instrumented — integration-conditional, so svcdecode
  must define behavior for CALL-absent windows (fail the metric vs
  degraded `HANDLER − PASS_ENTER` proxy). Undecided in D168/D169.
- **C3 seq-gap scope**: `record.seq` is per-ring/per-stream. Two
  stream files (probe + WS) have independent seq spaces — contiguity
  is checked per source stream BEFORE ts-merge, never on the merged
  order. A gap inside an open span voids only that span; gaps
  elsewhere are reported, not voiding (kind unknowable ⇒ cannot
  selectively excuse).
- **C4 ts ordering**: claim order ≠ ts order across producers; merged
  streams must sort by `ts_ns` with a documented tie-break (stream
  id). Non-monotonic drain-order ts is LEGAL, not malformed.
- **C5 open-at-EOF span**: CALL with no paired RET (killed client /
  excluded timeout) must surface as an open-unclosed sample —
  reported, never auto-closed or silently dropped.
- **C6 batch semantics**: multiple HANDLER_BEGIN inside one
  PASS bracket is legal (batch dequeue); pairing is by msgid, not by
  bracket position. Foreign-kind records may interleave inside a
  bracket and must not disturb pairing.
- **C7 reply-id cross-check**: `0x750b` (id−100 rule) is expected on
  ENQUEUE/RET of a probe; mismatch → flag anomaly (adjacency pairing
  still stands), distinct from foreign-msgid fixture.

## Frozen acceptance checklist (reviewer applies)

| # | Gate | PASS when |
|---|---|---|
| G1 | stream format | header magic/version/CAP/gen/pid verified; version or CAP mismatch → reject; record count == file size consistent; missing end marker = malformed (fail closed), not EOF-tolerant |
| G2 | generation isolation | one `gen` per window; second gen value in a stream → segregate per-arm or reject outright (rule must be stated); zero cross-generation joins |
| G3 | services-tid | selected by argmax PASS_ENTER density; tie or zero PASS_ENTER records → ambiguous/insufficient flag, never pick-first silently; HANDLER/IMPL/ENQUEUE tid must equal selected tid else flag |
| G4 | span construction | open at `CALL(0x74a7)` in probe stream, close at paired `RET`; WS edges inside must share services tid (C1); both-streams fixture proves the merge |
| G5 | serialize-one-probe | a second `CALL(0x74a7)` inside an open span → ambiguous flag + span voided, never ordinal-paired |
| G6 | seq-gap voiding | gap detected per source stream (C3); gap inside a span voids it, gap outside reported-only; voided vs preserved counts in output |
| G7 | non-success RET | `aux != MACH_MSG_SUCCESS` → consumes nothing; CALL stays open; delayed `RET(id,0)` still pairs (D162); open-at-EOF reported per C5 |
| G8 | malformed bounds | truncated header/record, bad magic, count>bound, and oversized inputs all rejected with distinct failure classes; non-monotonic ts NOT treated as malformed (C4) |
| G9 | drop counters | `dropped`/`uncommitted`/`emitted` reported verbatim per window; nonzero drops inside a probe window flag it (D164 §8); seq-gap count ≥ dropped reconciled (dead-producer gaps exceed dropped) |
| G10 | statistics | per-arm median `HANDLER_BEGIN − MACH_CALL`; exit-cluster count vs `max(8 ms, 0.5×interval)`; all samples reported (valid+voided+invalid), no selective exclusion |
| G11 | batch/bracket | HANDLER inside PASS bracket verified same-tid; multiple HANDLERs per bracket legal; foreign edges inside bracket don't disturb pairing (C6) |
| G12 | fixtures | clean; timeout→delayed-success; gap-in-span (voids) vs gap-outside (survives); foreign msgid interleave; second-CALL ambiguity; cross-gen reject; drop flagging; two-arm; **plus** two-stream merge fixture and CALL-absent window fixture per C1/C2 |

## Verdict

D168/D169 direction is sound; C1–C7 are contract-level corrections
the checklist now pins. Two mandatory fixture additions beyond
D169's list: two-stream merge (G4) and CALL-absent window (G12).
Acceptance = all G1–G12 with fixture evidence; no device/integration
gate is waived by this milestone.
