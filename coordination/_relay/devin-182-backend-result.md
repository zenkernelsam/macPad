# devin-182-backend-result

Status: **PENDING reviewer acceptance** — D180 design implemented in
`analysis/d141-svctime-candidate/` only; nothing staged, no commit.

## Delivered

- `g_uuid[16]` + `_Atomic g_uuid_valid`; `set_generation` retains the
  full UUID inside the INITIALIZING window — memcpy BEFORE pid/gen
  stores, flag release-published before the UNINIT rollback.
- `d141_get_uuid(out[16])`: acquire validity → verbatim 16 bytes + 0, or
  −1 + zeroed out; no locks/syscalls; errno untouched.
- `reset_for_test` clears UUID + validity; first-set immutability and
  the 64-byte record ABI unchanged.
- Provenance contract documented in header + README: hi64 is
  producer-declared, never record-verifiable (D175 wording preserved).

## Tests

t19 covers U1/U2/U3/U4/U6/U7 (verbatim round-trip, unset −1+zeroed,
record lo64 == accessor lo64, second-set refusal, reset, errno both
paths). t20 is U5 deterministic: `hook_gen_init` parks the writer
mid-INITIALIZING → `get_uuid` −1+zeroed while the flag is provably 0;
release → verbatim. No sleeps.

## Exact test results

`bash analysis/d141-svctime-candidate/run_tests.sh`:

- ASan+UBSan: `PASS: 454 checks, 0 failures` (437 prior + 17 new)
- TSan: `PASS: 454 checks, 0 failures` — no race reports
- arm64: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

Changed: `analysis/d141-svctime-candidate/{d141_svctime.c,
d141_svctime.h, d141_svctime_test.c, README.md}` +
`docs/evidence/devin-d182-backend-20261020.md`. No consumer/writer,
sender, hooks, transport, device, product integration, or wake
conclusion. Commit awaits Codex review of the exact diff/tests.

(handover to codex now)
