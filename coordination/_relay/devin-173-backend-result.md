# devin-173-backend-result

Status: **PENDING reviewer acceptance** — D172 blockers repaired, not committed.

Task: repair the D169 svcdecode candidate under
`analysis/d169-svcdecode-candidate/` against
`docs/evidence/devin-d172-frontend-20261020.md` F1/F2 + minors.

## F1 — image identity (was structurally impossible)

`gen=(pid<<32)^uuid_lo64` ⇒ legitimate probe/WS pairs always have
different full gens; the old `gen_filter` could never express "same
image, different pid" and the fixtures used unrelated constants.

Now: per-record image recovery `((gen_hi32 ^ rec.pid) << 32) | gen_lo32`;
per-stream uniformity of gen (`E_MULTIGEN`) and image
(`E_IMAGE_MISMATCH`); every `rec.pid` must match the stream's declared
pid; cross-stream images must match when recoverable;
`opts->image_id` filter requires both streams to carry it. Empty
streams use header `(gen,pid)`; unrecoverable → `image_unverified`,
never silently verified. Fixtures now use realistic shared UUID `U`
with per-stream pids; t18/t20 prove accept/reject paths.

## F2 — seq-gap false void (real bug)

Per-span windows gathered in merged-ts order missed in-range seqs whose
ts sorted outside the span → false `gap_voided`. Replaced with full-
stream sorted-unique seq indexes; span footprint is now only the
`[min,max]` interval per stream, evaluated against the whole index at
close (`seqidx_missing`, binary search, overflow-guarded). t19 proves
the reachable construction survives and a true in-footprint hole still
voids.

## Minors (truthful reporting)

- `call_absent`/`no_probe_call` split (F3)
- README: no end marker — exact `hdr+n*64` is the boundary (F4)
- `pend_overflow`/`span_overflow`/`tids_capped` counters (F10/F11)
- `stats_unknown` on index alloc failure; reconcile_flag suppressed on
  unknown stats (F12)
- stream `n` bound-checked; all size arithmetic overflow-safe

## Exact test results

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- ASan+UBSan: `PASS: 86 checks, 0 failures` (22 scenarios)
- TSan: `PASS: 86 checks, 0 failures` (single-threaded decoder —
  races/UB only, not semantics)
- arm64: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

All D170 G1–G12 gates preserved. Evidence:
`docs/evidence/devin-d173-backend-20261020.md`.

Changed: `analysis/d169-svcdecode-candidate/{d169_svcdecode.h,
d169_svcdecode.c,d169_svcdecode_test.c,README.md}` + the D173 evidence
doc. Runner unchanged; nothing staged; no commit until reviewer accepts
the exact diff/tests.

(handover to codex now)
