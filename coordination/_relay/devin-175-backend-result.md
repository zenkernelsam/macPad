# devin-175-backend-result

Status: **PENDING reviewer acceptance** — full-UUID identity + fail-closed
suppression implemented, not committed.

Task: continue the uncommitted offline candidate under
`analysis/d169-svcdecode-candidate/`.

## Delivered

**1. Full-width image identity.** Stream format v3 adds
`image_uuid[16]` + `uuid_valid` to `d169_stream_hdr` and
`d169_stream`. Recovered `uuid_lo64` is now only a consistency check
(rec.gen/pid vs the stream's declared UUID bytes → `E_IMAGE_MISMATCH`
on disagreement); identity requires the authoritative 128-bit UUID on
both streams — same low64/different upper64 rejects (t20). The
`image_id` filter is replaced by explicit `opts->image_uuid` +
`image_uuid_set`. Missing/invalid UUID on either stream (including
empty streams) → `image_unverified` → scoring suppressed; nothing
scores on low64 alone.

**2. Fail-closed suppression.** `tids_capped`, `pend_overflow`,
`span_overflow`, `stats_unknown` (index alloc failure), or
`image_unverified` → `scoring_suppressed` + `probes_scored = 0` with
would-be samples retained in `suppressed_samples`; all other counters
stay honest. No scoring from a truncated argmax; skipped pending CALLs
can't shift RET association into a false score. Correlation-state
allocation failure returns new `D169_E_ALLOC`.

**3. Format documented** — README carries the exact v3 header layout
(no trailer marker; `hdr + n*64` exact-length boundary).

## Exact test results

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- ASan+UBSan: `PASS: 89 checks, 0 failures` (22 scenarios)
- TSan: `PASS: 89 checks, 0 failures` — concurrency sanitizer on a
  single-threaded decoder; not semantic evidence
- arm64: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

D170 G1–G12 and D172 F2 whole-stream seq-index preserved. Evidence:
`docs/evidence/devin-d175-backend-20261020.md`.

Changed: `analysis/d169-svcdecode-candidate/{d169_svcdecode.h,
d169_svcdecode.c,d169_svcdecode_test.c,README.md}` + evidence doc.
Runner unchanged; nothing staged; no commit until reviewer accepts.

(handover to codex now)
