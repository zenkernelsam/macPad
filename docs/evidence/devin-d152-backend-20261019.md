# D152 — D150b isolated repair of the D141 svctime candidate

Date: 2026-10-19. Task: `coordination/_relay/devin-152-backend-task.md`.
Scope: `analysis/d141-svctime-candidate/` ONLY (`.c`, `.h`, `_test.c`,
`README.md`, `run_tests.sh`). No production, package, device, sentinel,
GUI, or other-agent actions. Candidate remains offline **PENDING**.
Commit deferred pending reviewer acceptance, per the task contract.

Consolidated packet implemented: D145, D149–D151, plus the D154/D156/
D158/D159 corrections.

## Diff summary per item

### I1 — gate retry determinism (d141_svctime.c, .h)
- `d141_gate_init` restructured into a single loop: on every `UNINIT`
  observation it re-attempts the `UNINIT→INITIALIZING` CAS instead of
  spinning toward `-1`; on `INITIALIZING` (or a lost retry CAS) it spins
  with a 40M bound (pathological exit only, returns −1 observably).
- Observability: `g_gate_spun` counter, incremented once per caller that
  enters the losing-CAS path; exposed as `d141_gate_spun()`.
- New test hook `d141_hook_gen_init` (`D141_TEST_HOOKS` only), invoked
  inside `d141_set_generation` after the `UNINIT→INITIALIZING` CAS and
  before the pid/gen stores + rollback.
- **T15** executes the required deterministic schedule: G parks inside
  `hook_gen_init` holding `INITIALIZING`; T's `gate_init` CAS-loses,
  enters the retry path (`gate_spun ≥ 1`, still pending); G released →
  rolls back to `UNINIT`; T re-CASes → publishes `ARMED`. Asserts
  `gate_rc == 1`, `gen_rc == 0`, and a subsequently emitted record
  carries coherent `pid` + `(pid<<32)^uuid_lo` generation.
- **T13 fixed, non-flaky**: `gate_rc == 1` is now deterministic under I1
  in both orders; `gen_rc ∈ {0,-1}` resolved by branch — gen-won asserts
  coherent tag, gen-lost asserts zeroed `pid`/`gen` per C3. The
  tautological `emit_seen_gen == 0 || emit_seen_gen == 0` assertion is
  removed.

### I2 — CAP-boundary refusal + saturation flood (T16)
Exact required schedule (`c − tail ≥ CAP → drop`), D159 hook ordering
preserved (global `post_claim` hook cleared after `hook_entered`, before
fill emits):
1. 255 committed (claims 0..254, hook NULL).
2. `post_claim = pause_hook`; P claims index 255, parks pre-commit.
3. Hook cleared while P remains inside its entered call.
4. Mid-stall drain → exactly 255, tail=255 held at the open slot,
   `uncommitted ≥ 1`; second mid-park drain returns 0.
5. 255 fill emits → claims 256..510 (c−tail 1..255 < CAP).
6. One emit → claim attempt 511: `511−255 = 256 ≥ CAP` → dropped;
   `g_head` stays 511 (refusals never consume a claim index).
7. Flood `M = CAP` more emits — all refused.
8. Release P → commits index 255.
9. Final drain → 256 records: `DEADBEEF` first, 255 followers in claim
   order.

Counters asserted: `emitted == 511`, `dropped == 1 + M == 257`,
`drained == 511`, mid-drain == 0 past the prefix, attempts ==
`512 + M == 768` (`emitted + dropped`).

### I3 — overlap churn (T17)
Single consumer = main thread (lease). Phase A: 8 committed prefix
records, then P1 parks post-claim; owner drain overlaps the live claim →
returns exactly the prefix, `uncommitted` counted, tail held. Phase B:
P1 released; 4 producers × 300 records gate on a `drain_started` latch so
emission overlaps live owner draining; `t17_done` latch terminates the
overlap loop, then final drain to quiescence. Per-record validation:
`magic`, `aux == msgid ^ 0x5A5A00`, uniform `gen`, `tid ∈ {producers,
parked, main}` (measured `pthread_threadid_np`), unique msgids across
all three classes. Conservation: `emitted + dropped == attempts ==
drained + dropped`, `emitted == drained`.

### I4 — pass aux route (contract route chosen)
`d141_wrap_pass(exit_aux, fn, arg)` → `d141_wrap_pass(fn, arg)`. The
misleading pre-call-fixed `exit_aux` parameter is removed; PASS_ENTER/
PASS_EXIT carry `msgid=0, aux=0`. Contract: per-pass handled count is
derived decoder-side by counting same-tid DISPATCH/HANDLER/IMPL edges
inside the bracket — lossless, and honors the nothing-held-across-orig
rule. Header + README + T1 assertion reconciled.

### I5 — Mach legs (no API delta)
Semantics preserved; leg contract now stated verbatim in header+README:
CALL `msgid`=send-header id when `MACH_SEND_MSG` (msg2: `hi32(arg4)`),
`aux=option` verbatim, captured pre-call; RET `msgid`=recv-header id iff
`kr==SUCCESS && MACH_RCV_MSG`, else 0, `aux=kr` verbatim; option bits
stay on the paired CALL; `MACH_RCV_TOO_LARGE` ⇒ `msgid=0` (conservative
— no XNU citation produced; only `msgh_size` is defined); pairing =
nearest unmatched same-tid CALL; reply `id−100` is decoder-side; timing
legs excluded from the 40000–41000 trace filter.
**T18**: distinct-id join (`0x74a7`→`0x750b`), timeout exclusion
(`msgid=0`, `kr!=0` unpaired), async interleave `CALL(A)CALL(B)RET(B)
RET(A)` nearest-unmatched pairing, aux preservation, per-leg errno.

### I6 — contract lines
All seven lines (D1 with seq-attempt disambiguation, C3, A3, D5 with
symmetric producer bound and no-async-drain, D8, D9 full enumeration,
D10 split candidate-local vs integration-layer) are stated verbatim in
`d141_svctime.h` and restated in `README.md`.

### I7 — hygiene
T7/T9/T10/T11/T12/T14 retained; no sleeps anywhere (all waits are
barrier/hook latch loops); `seen[]` uniqueness checks are real; the T13
tautology removed; T15/T16/T17/T18 added.

## API deltas (all documented in README)

- `d141_wrap_pass`: dropped `exit_aux` param (I4 contract route).
- New observability: `d141_gate_spun()`, `d141_head()`, `d141_tail()`.
- New test hook: `d141_hook_gen_init` (`D141_TEST_HOOKS` only).
- `d141_gate_init` semantics: UNINIT→re-CAS retry; `−1` reserved for the
  pathological never-settled bound.
- `D141_SVC_VERSION` 2 → 3.

## Results (`run_tests.sh`, deterministic)

ASan+UBSan **434 checks, 0 failures**; TSan **434 checks, 0 failures,
0 race reports** (natural scheduling variance produced 1–2 counted drops
in T17 across runs; conservation held every run); arm64 object:
`Mach-O 64-bit object arm64` compiles clean.

Per-test counts (both sanitizer runs identical): t1 71, t2 4, t3 4,
t4 6, t5 4, t6 5, t7 4, t9 7, t10 4, t11 4, t12 3, t13 4, t14 9,
t15 6, t16 271, t17 18, t18 10.

## Acceptance rows

R1 `gate_spun ≥ 1`, deterministic T15, `gate_rc == 1`, coherent gen/pid —
met. R2 exact CAP boundary + flood (`511 / 257 / 511 / 0 / ordered /
768`) — met. R3 overlap churn (drain_started latch, parked-prefix proof,
per-record correctness, exact conservation) — met. R4 contract route
chosen and reconciled — met. R5 pairing/errno/aux verified — met. R6 all
seven contract lines in header+README — met. R7 no tautologies, no
sleeps — met. R8 regression, counts reported — met.

## Remaining unknowns

- `MACH_RCV_TOO_LARGE`: no XNU citation produced — conservative
  `msgid=0` interpretation stands.
- `gate_init` 40M spin bound is pathological-exit only; −1 observable.
- Dead-producer head-of-line block and dead-consumer drain disable are
  bounded documented bounds (D5), not fixable inside this record layer.
- No production integration exists — separate review + runtime
  validation required before any such step.
