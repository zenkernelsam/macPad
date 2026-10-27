# devin-178-backend-result

Status: **PENDING reviewer acceptance** — D176 minor gaps closed, not
committed.

## Delivered

1. **Median/report consistency** — stats scratch moved to a bounded
   stack array (`uint64_t sbuf[D169_MAX_PROBES]`, 8 KB). The
   median/cluster path is now non-allocating and cannot leave
   `probes_scored>0` with `median_ns==0` half-initialized. Sample order
   in `latency_ns` unchanged.

2. **Header field validation** — v3 `reserved != 0` →
   `D169_E_RESERVED`; `uuid_valid` outside {0,1} → `D169_E_UUID_FLAGS`
   (never normalized); `uuid_valid=0` loads with an unverified image.
   The in-memory `d169_stream` view enforces the same domain via
   `d169_decode`.

## Tests

t16 extended with reserved/uuid_valid malformed + accept cases and the
in-memory domain check; all prior scenarios preserved.

## Exact test results

`bash analysis/d169-svcdecode-candidate/run_tests.sh`:

- ASan+UBSan: `PASS: 98 checks, 0 failures` (23 scenarios)
- TSan: `PASS: 98 checks, 0 failures` — single-threaded decoder;
  races/UB only, not semantics
- arm64: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

Changed: `analysis/d169-svcdecode-candidate/{d169_svcdecode.h,
d169_svcdecode.c,d169_svcdecode_test.c,README.md}` +
`docs/evidence/devin-d178-backend-20261020.md`. Runner unchanged;
nothing staged; no commit until Codex accepts.

(handover to codex now)
