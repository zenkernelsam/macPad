# devin-171-backend-result

Status: **PENDING reviewer acceptance** — candidate repaired, not committed.

Task: repair the rejected D169 svcdecode candidate under
`analysis/d169-svcdecode-candidate/` against the frozen
`docs/evidence/devin-d170-frontend-20261020.md` G1–G12 checklist.

## What was wrong vs what is now implemented

D169 was a single-buffer decoder that assumed CALL/RET legs shared the
WS services tid — structurally wrong per C1. The repair is a genuine
two-stream decoder:

- Probe stream (probe-process MACH_CALL/RET legs, own gen/seq space)
  and WS stream (PASS/HANDLER/IMPL/ENQUEUE/FLUSH, own gen/seq space)
  merge by `ts_ns` with stable tie-break `(ts, stream_id, idx)`; non-
  monotonic ts is legal input.
- Per-stream sorted-seq gap/dup stats computed before merge; per-span
  seq-footprint windows on each stream void a span only when a hole
  lands inside its footprint (gaps outside are reported-only).
- CALL↔RET pairing exists only inside the probe stream, nearest
  unmatched same-probe-tid CALL; non-success RETs (timeout/TOO_LARGE)
  consume nothing and delayed-success RETs still pair.
- WS edges bind on argmax-PASS_ENTER services tid; ties →
  `AMBIGUOUS_TIE`, zero → `INSUFFICIENT`; batch brackets allow multiple
  HANDLERs bound by msgid; wrong-tid/outside-bracket/dup/orphan all
  counted.
- CALL-absent windows set `call_absent` and score nothing — no
  degraded proxy metric. EOF-open spans reported via `open_eof`.
- Expected reply `probe+100` (`0x750b` for `0x74a7`) cross-checked:
  RET-id mismatch and missing ENQUEUE each flag distinct anomalies while
  adjacency pairing stands.
- Generation fail-closed: >1 gen in a stream → `E_MULTIGEN`;
  `gen_filter` → `E_GEN_MISMATCH`; zero cross-generation joins.
- Parser v2: exact `hdr + n*64` boundary; `HDR_TRUNC/MAGIC/VERSION/
  CAP/COUNT_BOUND/TRUNCATED/TRAILING/READ` are distinct failure classes;
  `emitted/dropped/uncommitted` preserved verbatim with a
  `gaps < dropped ⇒ reconcile_flag` sanity cross-check.

## Exact test results

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- ASan+UBSan: `PASS: 65 checks, 0 failures` (18 scenarios)
- TSan: `PASS: 65 checks, 0 failures` — concurrency sanitizer on a
  single-threaded decoder; no race/UB claim beyond that
- arm64: `Mach-O 64-bit object arm64` (object-level, x86_64 host)

Evidence: `docs/evidence/devin-d171-backend-20261020.md`.
Fixture-to-gate mapping is tabulated there (t1–t18 cover G1–G12 incl.
two-stream merge, CALL-absent, timeout→delayed success, in/out-of-span
gaps, batch, nonmonotonic ts, reply mismatch, all malformed classes).

## Scope

Changed: `analysis/d169-svcdecode-candidate/` (h, c, test, README;
runner unchanged). New: `docs/evidence/devin-d171-backend-20261020.md`.
Nothing else touched; unrelated WIP preserved; no commit until reviewer
accepts the exact diff/tests.

(handover to codex now)
