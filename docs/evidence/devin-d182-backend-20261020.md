# D182 — isolated D141 full-UUID retention/accessor (per accepted D180)

Task: `coordination/_relay/devin-182-backend-task.md`. Implements D180's
accepted design only inside `analysis/d141-svctime-candidate/`
(committed at `f33b5e2` baseline `6d5f2e2`; delta uncommitted pending
review). No production source/hooks, no drain consumer, no v3 stream
writer, no device/product integration.

## Changes

`d141_svctime.c` (+17 lines):

- `static unsigned char g_uuid[16]` + `static _Atomic int g_uuid_valid`.
- `d141_set_generation` INITIALIZING window now: `memcpy(g_uuid, uuid,
  16)` BEFORE the relaxed `g_pid`/`g_gen` stores → `g_uuid_valid = 1`
  (release) → existing `g_state = UNINIT` (release). The flag sits
  inside the CAS window so the existing single-winner protocol
  serializes the (uuid,pid,gen) triple as one publication.
- `int d141_get_uuid(unsigned char out[16])` — acquire-loads
  `g_uuid_valid`: `1 → memcpy 16 + return 0`; `0 → memset out + return
  -1`. No locks, no syscalls, errno untouched.
- `d141_reset_for_test` clears `g_uuid` and `g_uuid_valid`.

`d141_svctime.h` (+12): accessor declaration + contract note —
**provenance only**: records carry gen/pid (uuid low64) exclusively;
the high 64 bits are producer-declared and can never be record-verified
(D175 decoder wording preserved).

## Tests — D180 U1–U8 (barrier/hook-driven, zero sleeps)

| U | Covered by | Asserts |
|---|---|---|
| U1 | t19 | `get_uuid` → 0, all 16 bytes verbatim incl. hi64 |
| U2 | t19 | before any set: −1 + zeroed out (no stale bytes) |
| U3 | t19 | drained record `(gen,pid)` recovers lo64 == accessor lo64 — the exact D175 decoder relation |
| U4 | t19 | post-`reset_for_test`: −1 + zeroed out |
| U5 | t20 | `d141_hook_gen_init` parks writer inside INITIALIZING → concurrent-path `get_uuid` −1 + zeroed (flag still 0 — deterministic, no scheduling luck); release → verbatim read |
| U6 | t19 | post-gate second `set_generation` → −1, uuid untouched |
| U7 | t19 | errno preserved across both valid and invalid paths |
| U8 | full run | all pre-existing 437 checks pass; new total reported |

## Exact test results

`bash analysis/d141-svctime-candidate/run_tests.sh`:

- **ASan+UBSan**: `PASS: 454 checks, 0 failures` (437 prior + t19 13 +
  t20 4; 19 scenarios)
- **TSan**: `PASS: 454 checks, 0 failures` — no race reports; the
  acquire/release flag pair is the only new synchronization edge
- **arm64**: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

## Acceptance rows (D180 §5)

- R1 ✓ — U1–U8 pass under both sanitizers, counts reported.
- R2 ✓ — U5 is barrier-driven via `hook_gen_init`; the writer is
  provably parked mid-window when the reader observes −1.
- R3 ✓ — header + README record the provenance-only contract verbatim.
- R4 ✓ — emit/drain/ring ABI untouched; `d141_record` is still 64 bytes
  (`gen`/`pid` only); no new dependencies; accessor is additive.

## Scope

Changed: `d141_svctime.{c,h}`, `d141_svctime_test.c`, `README.md` under
`analysis/d141-svctime-candidate/` + this report. Nothing staged; no
commit until Codex reviews the exact diff/tests. Unrelated WIP
preserved.
