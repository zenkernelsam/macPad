# D173 — svcdecode repairs for D172 blocking findings (F1/F2) + minors

Task: `coordination/_relay/devin-173-backend-task.md`. Repair the
uncommitted D169 candidate under `analysis/d169-svcdecode-candidate/`
against `docs/evidence/devin-d172-frontend-20261020.md`. Decoder/test
repair only — no sender, hooks, permits, device, product integration,
or commit.

## F1 — generation/image identity (was structurally broken)

`d141_set_generation` encodes `gen = (pid<<32) ^ uuid_lo64`, so
`gen_lo32 = uuid_lo32` and `gen_hi32 = pid ^ uuid_hi32`. Legitimate
probe/WS pairs have **different pids ⇒ different full gens** — the old
`gen_filter` compared full 64-bit gens and could never accept a true
pair; D169's t18 fixture (unrelated gen constants `0xA5A5DEAD` /
`0xB6B6CAFE`) blessed the defect.

Implemented:

- Per-record image recovery: `image = (u64)((gen>>32) ^ rec.pid) << 32
  | (u32)gen` — recovers `uuid_lo64` without knowing the UUID.
- `stream_identity()` enforces per stream: uniform `gen`
  (`E_MULTIGEN`), every `rec.pid ==` header `pid` when declared
  (`E_IMAGE_MISMATCH`), uniform recovered image (`E_IMAGE_MISMATCH`).
  Empty streams derive identity from the header `(gen,pid)`; `gen=0` or
  `pid=0` ⇒ unrecoverable → `image=0` + `image_unverified` (decode
  proceeds — no records exist to corrupt).
- Cross-stream: both images known and differing → `E_IMAGE_MISMATCH`.
- `opts->image_id` (replacing `gen_filter`): nonzero requires BOTH
  streams' recovered images to equal it → `E_IMAGE_MISMATCH`.
- New error `D169_E_IMAGE_MISMATCH`; fixtures now use realistic shared
  UUID `U` with `PGEN=(555<<32)^U`, `WSGEN=(777<<32)^U`.

## F2 — seq-gap false void (real bug)

Old code collected a span's seq set in merged-ts order only while the
span was open; a record whose seq lay inside the footprint but whose
`ts_ns` sorted outside the span never entered the set →
`range != count` false void. Replaced the per-span windows with full-
stream sorted-unique seq indexes (`seqidx_build`), and the footprint is
now only the `[min,max]` seq interval per stream over the open span.
At span close, `seqidx_missing` counts absent seqs inside the interval
against the whole-stream index (binary search) — presence anywhere in
the stream disproves the hole. Overflow-guarded (`hi-lo == UINT64_MAX`
→ gap; `-1` on unknown index ⇒ conservative void + `stats_unknown`).

## Minors

- **F3**: `call_absent` (zero MACH_CALLs at all) split from
  `no_probe_call` (CALLs exist, none msgid=probe). t22 covers both.
- **F4**: README states the format has no end marker — exact
  `hdr + n*64` length is the boundary; truncated and trailing are
  distinct failure classes (t16).
- **F10**: `pend_overflow` (CALLs beyond `D169_PENDING_CALLS`) and
  `span_overflow` (probe CALLs beyond `D169_MAX_PROBES`) counted.
- **F11**: >64 distinct PASS_ENTER tids → `tids_capped` counted.
- **F12**: seq-index alloc failure → `stats_unknown`; `reconcile_flag`
  only evaluated when stats are known.
- Stream `n` bound-checked against `D169_MAX_STREAM_RECS` before
  allocation; all size arithmetic overflow-safe.

## Fixture additions

| Test | Proves |
|---|---|
| t19 | F2: in-range seq ts-sorted outside span survives; control with truly-missing seq still voids |
| t20 | F1: different-UUID WS → `E_IMAGE_MISMATCH`; bad rec.pid → `E_IMAGE_MISMATCH`; empty-stream header identity accepts + `call_absent`; unidentified header → `image_unverified` |
| t21 | `pend_overflow`, `mach_calls`, `tids_capped` |
| t22 | `no_probe_call` vs `call_absent` split |
| t18 | rewritten: same-image new-pid pair accepted, `image_id` filter accepts U and rejects U2 |

## Verification (exact results)

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- **ASan+UBSan**: `PASS: 86 checks, 0 failures` (22 scenarios)
- **TSan**: `PASS: 86 checks, 0 failures` — concurrency sanitizer on a
  single-threaded decoder; proves absence of races/UB in the build,
  not decoder semantics
- **arm64**: `Mach-O 64-bit object arm64` (compile-level only, x86_64
  host)

All D170 G1–G12 gates preserved; `d169_err` order extended with
`IMAGE_MISMATCH` at the end (no enum renumbering churn).

## Scope

Files: `analysis/d169-svcdecode-candidate/{d169_svcdecode.h,
d169_svcdecode.c,d169_svcdecode_test.c,README.md}` plus this report.
Uncommitted pending reviewer acceptance; unrelated WIP preserved.
