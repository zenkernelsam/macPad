# D169 — svcdecode candidate implemented (offline-only)

Date: 2026-10-20. Task: `coordination/_relay/devin-169-backend-task.md`.
Implements the milestone D168 proposed: stream format + decoder +
synthetic corpus under `analysis/d169-svcdecode-candidate/` only. No
sender, hooks, permits, device, or product integration. Not committed —
commit waits for reviewer acceptance.

## Delivered

- **`d169_svcdecode.h`** — canonical stream format (64-B header with
  gen tag, pid, ring cap, drop/uncommitted snapshots, arm label +
  `d141_record[]`), decoder API (`d169_decode`, `d169_decode_file`),
  `d169_report` (per-probe latencies, median, cluster count/bound,
  ambiguous/gap_voided/timeout/foreign_gen/open-span counters, honest
  drop accounting). Reuses the committed `d141_svctime.h` ABI via a
  cross-candidate include — no schema duplication.
- **`d169_svcdecode.c`** — the D164/D166 rules verbatim: gen filter;
  services-tid by max in-gen `PASS_ENTER` density; span FSM
  (`AWAIT_RET → AWAIT_HANDLER → DONE`); CALL→RET pairing by
  nearest-unmatched same-tid CALL on a bounded stack; non-success RET
  excluded without consuming (D162); **serialize-one-probe** — a second
  probe CALL poisons both spans; **seq-gap voiding** via a bounded
  per-span window set over *all* gen-matching records (foreign-tid seqs
  legitimately interleave — D1 non-monotonicity honored; a missing seq
  in the span's range = dropped attempt = voided, flagged not scored);
  reply join by `probe+100`; arm stats vs `max(8 ms, wait/2)`.
- **`d169_svcdecode_test.c`** — 11 synthetic scenarios covering every
  rule incl. constructive gap/ambiguity cases.
- **`run_tests.sh` + `README.md`**.

## Design notes discovered during implementation

- `local_port` is schema-absent (D166 §1) — sender identity exists only
  as arm metadata; decoder never infers ports on-record.
- `record.seq` non-monotonicity across producers (D1) forced the
  windowed contiguity check: comparing consecutive svc-tid seqs alone
  false-flags foreign-producer interleave, and naive global-seq
  comparison false-flags out-of-order commits. The window-set range
  check is exact for the invariant "no attempt id missing inside the
  span".
- `resolve_svc_tid` uses pure PASS_ENTER density (contract rule) —
  an earlier draft requiring a probe HANDLER_BEGIN on the tid failed
  s10 (a never-handled span can't resolve its tid); corrected.

## Results

ASan+UBSan **38 checks, 0 failures**; TSan **38/0**; arm64 object
`Mach-O 64-bit object arm64`. Per-test counts in candidate README.

## Boundaries held

- `analysis/` only; no production source touched.
- Emit-side probes, permits, drain consumer, sentinel arming remain
  integration-gated per D168 §3 — none attempted.
- No commit — awaiting reviewer acceptance per the task contract.
