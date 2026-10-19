# D184 — first-set latch for the D182 UUID accessor (fixing D183)

Task: `coordination/_relay/devin-184-backend-task.md`. Fixes D183's
re-set/torn-read finding inside `analysis/d141-svctime-candidate/` only;
the D182 delta remains uncommitted pending review.

## The defect (real)

`set_generation` rolled `g_state` back to `UNINIT` after publishing, so
a second *pre-gate* call re-won `UNINIT→INITIALIZING` while
`g_uuid_valid==1` and overwrote non-atomic `g_uuid[16]` — a genuine
data race / torn read against a concurrent `d141_get_uuid` memcpy,
violating D180's explicit first-set-wins contract.

## Fix (per spec, minimal)

Immediately after winning the CAS, `set_generation` acquire-loads
`g_uuid_valid`; if already `1` it release-stores `g_state=UNINIT` and
returns `-1` — before the test hook and before any uuid/pid/gen store.
The latch check lives inside the same CAS window so serialization is
unchanged. `d141_reset_for_test` stays the only latch release and is
documented as requiring quiescent test threads — not a production
reset path (header comment + README).

## Tests (barrier-driven, zero sleeps)

- **t19 revised (U6 split)**: U6a — second `set_generation` after the
  first publish but BEFORE `gate_init` → `-1` + uuid verbatim (exercises
  the new latch); U6b — retained post-gate refusal.
- **t21 new**: first writer parked inside INITIALIZING via
  `hook_gen_init`; concurrent second `set_generation` loses the CAS →
  `-1` and leaves nothing touched (`get_uuid` still −1 mid-window);
  release → first tuple publishes verbatim; post-publish pre-gate set →
  `-1` via the latch, uuid intact.

## Exact test results

`bash analysis/d141-svctime-candidate/run_tests.sh`:

- **ASan+UBSan**: `PASS: 462 checks, 0 failures` (437 prior + t19 15 +
  t20 4 + t21 6; 20 scenarios)
- **TSan**: `PASS: 462 checks, 0 failures` — no race reports; the
  torn-read path no longer exists
- **arm64**: `Mach-O 64-bit object arm64` (compile-level, x86_64 host)

## Scope

Changed: `d141_svctime.{c,h}`, `d141_svctime_test.c`, `README.md` +
this report. Nothing staged; no commit until Codex reviews the exact
diff/tests. No drain consumer, v3 writer, sender, hooks, transport,
device, product integration, or wake conclusion.
