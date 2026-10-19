# D171 — svcdecode repair against the frozen D170 checklist

Task: `coordination/_relay/devin-171-backend-task.md`. The D169
candidate was not accepted; this round rewrites it under
`analysis/d169-svcdecode-candidate/` to satisfy
`docs/evidence/devin-d170-frontend-20261020.md` G1–G12. Decoder/test
repair only — no sender, hooks, permits, device, product integration,
or commit.

## Architectural correction

D169 decoded a single buffer assuming CALL/RET legs shared the WS
services tid. D170 C1 pins the real topology: probe legs live on the
probe process's tid in the probe stream; service edges live on the WS
services tid in a different ring/stream. The decoder now takes two
independent streams (`d169_stream` views) and merges by ts.

## What changed

- **Two-stream API**: `d169_decode(probe, ws, opts, report)`;
  `d169_decode_files(probe_path, ws_path, …)`; `d169_stream_load/_free`
  for canonical stream files.
- **Merge** (C4): full sort by `(ts_ns, stream_id[probe<ws], idx)` —
  deterministic stable tie-break; non-monotonic ts accepted.
- **Per-stream seq validation** (C3/G6): sorted-seq gap+dup stats per
  stream *before* merge; per-span footprint tracked as two seq windows
  (probe/WS); a hole inside a footprint voids the span, outside gaps are
  reported only.
- **Pairing**: CALL↔RET on the probe stream only, nearest unmatched
  same-probe-tid CALL; non-success RET consumes nothing (D162 rule
  preserved). WS edges bind on the selected services tid only.
- **CALL-absent** (C2): `call_absent` flag, zero scored — explicit
  metric failure, no degraded proxy.
- **EOF-open** (C5): `open_eof` counts unclosed spans; never
  auto-closed.
- **Batch** (C6/G11): PASS-bracket depth tracked on svc tid; multiple
  HANDLERs legal, binding by msgid; `handler_outside_bracket`,
  `handler_wrong_tid`, `handler_dup`, `orphan_handler` counted.
- **Reply cross-check** (C7): RET with `msgid != call+100` →
  `reply_mismatch` (pairing stands); closed span lacking
  `ENQUEUE(+100)` → `reply_missing`.
- **Gen fail-closed** (G2): >1 distinct gen in a stream →
  `E_MULTIGEN`; `gen_filter` mismatch → `E_GEN_MISMATCH`; header-vs-record
  disagreement → `E_MULTIGEN`.
- **Parser** (G1/G8): header v2 gains `emitted`; exact
  `sizeof(hdr)+n*64` boundary — truncation and trailing bytes are
  distinct failures, plus `MAGIC/VERSION/CAP/COUNT_BOUND/READ`; bound
  `D169_MAX_STREAM_RECS`.
- **Drops** (G9): `emitted/dropped/uncommitted` echoed per stream;
  nonzero → `drops_flagged`; `gaps < dropped` → `reconcile_flag`
  (gaps legitimately exceed drops via dead-producer stalls).
- **Tid resolution** (G3): argmax PASS_ENTER density; tie →
  `TID_AMBIGUOUS_TIE`, zero → `TID_INSUFFICIENT`; never silent pick-first
  without flag.
- **Stats** (G10): median `HANDLER_BEGIN − MACH_CALL`, cluster bound
  `max(8 ms, wait_ns/2)`; all spans reported by class.

## Fixture coverage (G12)

| Test | Proves |
|---|---|
| t1 | two-stream clean merge; svc tid; HANDLER−CALL = 600 ns across streams |
| t2 | timeout RET excluded; delayed-success RET still pairs |
| t3 | WS-seq hole inside span footprint → `gap_voided` |
| t4 | gaps before/after span reported, span survives |
| t5 | foreign msgid/tid interleave inert (incl. non-probe CALL/RET pair) |
| t6 | second open CALL → `ambiguous`, nothing scored |
| t7 | second gen in WS stream → `E_MULTIGEN` |
| t8 | `dropped` verbatim, `drops_flagged`, reconcile |
| t9 | PASS_ENTER tie → `AMBIGUOUS_TIE`; zero → `INSUFFICIENT` |
| t10 | unclosed span → `open_eof` |
| t11 | batch bracket: 3 HANDLERs, probe bound by msgid |
| t12 | non-monotonic drain-order ts legal, ts-merge rescues order |
| t13 | `RET 0x7777` + wrong ENQUEUE → `reply_mismatch` + `reply_missing`, pairing stands |
| t14 | CALL-absent window → `call_absent`, zero scored |
| t15 | wrong-tid HANDLER + outside-bracket HANDLER flagged |
| t16 | file round-trip + 7 malformed classes (`HDR_TRUNC/MAGIC/VERSION/CAP/TRUNCATED/TRAILING/COUNT_BOUND`) |
| t17 | duplicate seq + orphan RET counted |
| t18 | two arms decode independently; `gen_filter` rejects other gen |

## Verification (exact results)

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- **ASan+UBSan**: `PASS: 65 checks, 0 failures`
- **TSan**: `PASS: 65 checks, 0 failures` — concurrency sanitizer on a
  single-threaded decoder; proves absence of races/UB in the build,
  not decoder semantics
- **arm64**: `Mach-O 64-bit object arm64` — compile-level only on the
  x86_64 host

One defect found by ASan during repair and fixed in-test (8-byte write
from a 5-byte literal in a fixture, not decoder code).

## Honest limits

- Same-host `CLOCK_MONOTONIC` domain assumed for cross-stream ts;
  clock skew produces `orphan_handler`/`open_eof`/negative-latency
  guards, not mis-scores.
- Serialize-one-probe remains the pairing foundation; `local_port` is
  absent from the 64-byte schema by design.
- Span footprint windows are bounded (`D169_WIN_CAP`); overflow voids
  conservatively.
- No device/production claims; decoder remains offline-only.

## Scope

Files: `analysis/d169-svcdecode-candidate/{d169_svcdecode.h,
d169_svcdecode.c,d169_svcdecode_test.c,README.md,run_tests.sh}` plus
this report. No production/WIP staging; uncommitted pending reviewer
acceptance.
