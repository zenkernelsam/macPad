# D178 — svcdecode median/report consistency + header field validation

Task: `coordination/_relay/devin-178-backend-task.md`. Two narrow D176
findings on the uncommitted offline candidate under
`analysis/d169-svcdecode-candidate/`; no commit/device/integration.

## Change 1 — median scratch cannot fail (fail-closed consistency)

The stats block malloc'd a scratch array sized by `probes_scored`; a
failure left `probes_scored>0` with `median_ns==0` and half-computed
cluster stats — internally inconsistent output. Replaced with a bounded
stack scratch `uint64_t sbuf[D169_MAX_PROBES]` (8 KB, the sample array
is already capped at `D169_MAX_PROBES` entries): the median/cluster
path is now non-allocating and cannot fail half-initialized. Output
order in `latency_ns` unchanged.

## Change 2 — v3 header field validation

- `reserved != 0` → new `D169_E_RESERVED` on load.
- `uuid_valid` must be exactly 0 or 1 → new `D169_E_UUID_FLAGS`; never
  normalized to true. `uuid_valid=0` loads as a valid stream with an
  unverified image (decode runs, `image_unverified`+suppressed).
- The in-memory `d169_stream` view enforces the same domain:
  `uuid_valid` outside {0,1} → `D169_E_UUID_FLAGS` from `d169_decode`.

## Tests

t16 extended: `reserved=1` → `E_RESERVED`; `uuid_valid=2` →
`E_UUID_FLAGS`; `uuid_valid=0` → loads OK with `s.uuid_valid==0`;
in-memory `uuid_valid=5` → `E_UUID_FLAGS`. All prior tests preserved.

## Verification (exact results)

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- **ASan+UBSan**: `PASS: 98 checks, 0 failures` (23 scenarios)
- **TSan**: `PASS: 98 checks, 0 failures` — concurrency sanitizer on a
  single-threaded decoder; races/UB only, not semantics
- **arm64**: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

## Scope

Files: `analysis/d169-svcdecode-candidate/{d169_svcdecode.h,
d169_svcdecode.c,d169_svcdecode_test.c,README.md}` + this report.
Uncommitted pending Codex review; unrelated WIP preserved.

## Acceptance note (D181)

D141 currently retains only the producer's UUID **low64** (inside the
`gen` tag). The D169 v3 header `image_uuid[16]` high64 is
**producer-declared** — asserted by the stream writer, not
independently verified — until the separately reviewed D141
retention/serializer milestone carries the full 128-bit UUID. The
decoder enforces consistency (low64 must match recovered gen/pid
data) and equality between streams, but cannot detect a producer that
misdeclares the upper bytes.
