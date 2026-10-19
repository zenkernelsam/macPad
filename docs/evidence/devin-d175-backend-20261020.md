# D175 — svcdecode full-UUID identity + fail-closed scoring suppression

Task: `coordination/_relay/devin-175-backend-task.md`. Continue the
uncommitted offline candidate under `analysis/d169-svcdecode-candidate/`;
no commit, no device/product integration.

## Change 1 — full-width image identity (never low64-only)

D173's `image_id` was recovered `uuid_lo64` — 64 bits of the SkyLight
Mach-O UUID, not exact identity. Implemented:

- `d169_stream_hdr` v3 gains `image_uuid[16]` + `uuid_valid` +
  `reserved`; `D169_STREAM_VERSION` bumped 2→3; the in-memory
  `d169_stream` and `d169_stream_stats` carry the same pair.
- `stream_identity()` still enforces per-stream uniform `gen`
  (`E_MULTIGEN`), `rec.pid ==` header `pid` (`E_IMAGE_MISMATCH`), and
  uniform recovered low64 between records — but now the recovered
  low64 must equal the stream's **declared `image_uuid` low bytes**
  when `uuid_valid` (uuid/gen consistency check), and identity
  verification requires the authoritative 128-bit UUID on BOTH streams.
- Cross-stream: both `uuid_valid` and `memcmp` differs →
  `E_IMAGE_MISMATCH`. Same low64 / different upper64 is correctly
  rejected (t20).
- `opts->image_uuid` + `image_uuid_set` replaces `image_id`: the
  filter requires both streams to be `uuid_valid` AND byte-equal to the
  filter → `E_IMAGE_MISMATCH`.
- Missing/invalid UUID (including empty streams that rely on header
  identity) → `image_unverified` → scoring suppressed; decode continues
  so diagnostic counters remain truthful. Samples are never accepted
  on low64 alone.

## Change 2 — any bounded truncation suppresses scoring

`scoring_suppressed` is set and `probes_scored` forced to 0 whenever
ANY correlation-capacity limit fires: `tids_capped` (never score a
truncated argmax), `pend_overflow` (never let skipped CALLs shift RET
association), `span_overflow`, `stats_unknown` (seq-index alloc
failure), or `image_unverified`. Would-be samples are retained in
`suppressed_samples`; every other counter/class stays honest.
Merge/state allocation failure returns new `D169_E_ALLOC`.

## Fixture deltas

| Test | Change |
|---|---|
| t18 | rewritten for full-UUID filter: same-UUID/different-pid accepted across cold starts; `image_uuid` filter accepts U16 / rejects U16B |
| t20 | rewritten: same-low64/different-upper64 → `E_IMAGE_MISMATCH`; uuid/gen low64 disagreement → `E_IMAGE_MISMATCH`; missing WS UUID → `image_unverified`+`scoring_suppressed`+`suppressed_samples=1`; empty stream header UUID sufficient; empty stream no UUID → suppressed |
| t21 | rewritten: clean span + pend overflow → suppressed, `suppressed_samples=1`; clean span + tid cap → suppressed |
| all | fixtures now carry full `U16`/`U16B`/`U16C` 128-bit UUIDs; `write_stream` emits the v3 header |

## Verification (exact results)

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- **ASan+UBSan**: `PASS: 89 checks, 0 failures` (22 scenarios)
- **TSan**: `PASS: 89 checks, 0 failures` — concurrency sanitizer on a
  single-threaded decoder; races/UB only, **not** semantic evidence
- **arm64**: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

D170 G1–G12 and the D172 F2 whole-stream seq-index fix preserved.

## Scope

Files: `analysis/d169-svcdecode-candidate/{d169_svcdecode.h,
d169_svcdecode.c,d169_svcdecode_test.c,README.md}` + this report.
`run_tests.sh` unchanged. Uncommitted pending Codex review acceptance;
unrelated WIP preserved.
