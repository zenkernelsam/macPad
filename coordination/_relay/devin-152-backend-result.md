# D152 result — D150b isolated repair of the D141 svctime candidate

Status: **PENDING** — implementation complete, all acceptance items
implemented and verified locally; **not committed** (commit deferred
until reviewer accepts the milestone, per the task contract). Scope held
to `analysis/d141-svctime-candidate/` only; no production, device,
sentinel, GUI, package, or other-agent actions taken.

## Implemented (I1–I7)

- **I1** — `gate_init` retries the UNINIT→INITIALIZING CAS on every
  UNINIT observation (covers the `set_generation` window); bounded spin
  across INITIALIZING; `g_gate_spun`/`d141_gate_spun()` observability;
  `d141_hook_gen_init` test hook inside the generation window. **T15**
  runs the exact schedule: gen thread parks holding INITIALIZING, gate
  thread CAS-loses (`gate_spun ≥ 1`), rolls back, gate re-CASes →
  `gate_rc == 1`, `gen_rc == 0`, emitted record carries coherent
  pid+`(pid<<32)^uuid_lo`. **T13 fixed**: deterministic `gate_rc == 1`,
  gen-lost branch asserts zeroed pid/gen per C3; tautology removed.
- **I2** — **T16** exact schedule: 255 commit → P parks claim 255 →
  global hook cleared post-`hook_entered` (D159) → prefix drain 255 →
  255 followers (claims 256..510) → claim 511 refused at `c−tail=256≥CAP`
  → flood `M=CAP` all refused, head frozen → release → ordered drain.
  `emitted=511, dropped=1+M=257, drained=511, mid-drain=0, attempts=768`.
- **I3** — **T17**: committed prefix + producer parked post-claim while
  owner drain overlaps → prefix only, `uncommitted` counted, tail held;
  then 4×300 churn producers gated on `drain_started` overlapping live
  draining. Per-record `magic`, `aux==msgid^0x5A5A00`, uniform gen,
  `tid ∈ producers`, unique msgids; `emitted+dropped==attempts==
  drained+dropped`.
- **I4** — contract route: `d141_wrap_pass(fn, arg)` (misleading
  `exit_aux` removed); PASS records carry `aux=0`; handled count derived
  decoder-side via same-tid DISPATCH/HANDLER/IMPL edges inside the
  bracket. README/header/tests reconciled.
- **I5** — Mach-leg contract verbatim (CALL aux=option, RET aux=kr,
  option bits on paired CALL only, TOO_LARGE `msgid=0` conservative,
  nearest-unmatched-same-tid pairing, raw ids, no 40000–41000 filter).
  **T18**: distinct-id join, timeout exclusion, async interleave
  pairing, aux verbatim, per-leg errno. No API delta.
- **I6** — all seven contract lines D1/C3/A3/D5/D8/D9/D10 stated in
  `d141_svctime.h` + `README.md`, with D1 seq-attempt clarification,
  D5 symmetric producer bound, D9 full worklist, D10 split scope.
- **I7** — no sleeps, no tautologies; T7/T9/T10/T11/T12/T14 retained;
  per-test check counts now printed by the harness.

## Results

`run_tests.sh` — ASan+UBSan **434 checks, 0 failures**; TSan **434
checks, 0 failures, 0 race reports**; arm64 object compiles
(`Mach-O 64-bit object arm64`). Per-test counts: t1 71 / t2 4 / t3 4 /
t4 6 / t5 4 / t6 5 / t7 4 / t9 7 / t10 4 / t11 4 / t12 3 / t13 4 /
t14 9 / t15 6 / t16 271 / t17 18 / t18 10.

## API deltas (documented in README)

`wrap_pass` signature change (I4); new `d141_gate_spun/head/tail`
accessors; new `d141_hook_gen_init` test hook; `gate_init` retry
semantics; `D141_SVC_VERSION` 2→3.

## TOO_LARGE citation

None produced — the conservative `msgid=0` interpretation stands.

Evidence: `docs/evidence/devin-d152-backend-20261019.md`.
Files: `analysis/d141-svctime-candidate/{d141_svctime.c,.h,
d141_svctime_test.c,README.md,run_tests.sh}`.

Awaiting reviewer acceptance; will commit on accept.

(handover to codex now)
