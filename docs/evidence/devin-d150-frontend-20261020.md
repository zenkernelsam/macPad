# D150 — adversarial feasibility audit of the D149 packet

Self-audit of `devin-d149-frontend-20261020.md` against the frozen D143
sources. Three real defects found in my own packet; corrected schedules
below. Review/result supersede D149 where they differ.

## Defect 1 — item-1 schedule cannot observe the losing CAS (needs a second hook)

`d141_hook_gen_init` holds `set_generation` inside INITIALIZING, but
nothing makes `gate_init`'s failed CAS observable: the spin loop has no
hook and no counter, so "gate is spinning" vs "gate hasn't started" are
indistinguishable externally — the release of gen's hook would race the
very observation it depends on. The schedule as written is NOT
deterministic.

**Correction**: one more observability point is mandatory —
- preferred: a diagnostic counter `g_gate_spun` incremented once when
  the losing-CAS spin begins (production-harmless, same class as
  `drain_refused`); test polls `gate_spun ≥ 1` before releasing gen, or
- a second test hook `d141_hook_gate_cas_failed`.
Then: `gen_spun` observed + `gate_spun ≥ 1` ⇒ release gen → rollback
UNINIT → gate re-CASes (per the required fix) → ARMED, `gate_rc==1`.
Without the observability point the test only proves "works
eventually", which even the buggy code could satisfy flakily — exactly
the T13 trap.

## Defect 2 — item-2 totals internally inconsistent

Replayed against the real bound rule:

```
fill 255 committed        head=255 tail=0
park producer at claim255 (post-claim hook)
drain → n=255, tail=255, uncommitted=1
emit 255 records          claims 256..510 all land (c−tail = 1..255 < CAP)
                          head=511, emitted=510
emit once more            claim 511: 511−255 = 256 ≥ CAP → dropped=1
release park → commit     emitted=511
final drain               256 records: [DEADBEEF, 255 followers]
```

- claim 511 **is** the refused `c+CAP` attempt (`255+256=511`) ✓ — that
  part of the packet was right.
- but expected totals were wrong in the packet: `emitted == 511`
  (2·CAP−1), not `CAP`; `drained total == 511` across both drains.
- `uncommitted` is cumulative — stays 1 (only the first drain probed
  the open slot); asserting it returns to 0 after quiesce is WRONG —
  assert `== 1` at end.
- additional assertion worth adding: mid-drain must return 0 (tail at
  255 with open slot) — T12's no-skip-past check carries over.

## Defect 3 — "drain started before `go`" does not prove overlap

A scheduler can delay the consumer past all producers' completion —
zero overlap while appearing to test it. Required two-stage barrier:

1. `drain_started` latch — consumer thread sets it before entering the
   loop; producers' `go` waits on BOTH `drain_started` and `go` ⇒ drain
   was at least scheduled before production began (necessary, not
   sufficient alone).
2. **Deterministic overlap proof** (carry the park trick): park P1
   mid-commit via `d141_hook_post_claim`; P2 emits and drain runs
   concurrently — drain returning *prefix-only* with `uncommitted>0`
   proves it observed live uncommitted state during active production.
   Then release and continue the churn phase.

## Item 4 — post-call aux: feasible, but a cheaper contract-only option exists

`d141_wrap_pass_ex(aux_after,…)` is implementable and matches the real
trampoline pattern (compute after orig). **But** the per-pass handled
count is already derivable from the record stream: count
`HANDLER_BEGIN`/`IMPL_INVOKE` edges between PASS_ENTER and PASS_EXIT on
the same tid. Two acceptable resolutions; backend picks one:
- API route: add `wrap_pass_ex`; test aux == post-call-only value.
- Contract route: no API change; D3 closes by contract line "handled
  count = dispatch-edge count within the pass bracket" + decoder rule.
Flagged missing input either way: whether `CGXRunOneServicesPass`
itself exposes a count is unverified — the stream-derived count does
not depend on it.

## Item 5 — mach-leg semantics: three contract gaps to fix

- **Send-only call**: RET leg should carry kr but msgid semantics for
  send-only ops need a stated rule — recommend: RET msgid = send id
  when `option` had no RCV bit (so the pair joins on identical ids),
  else recv id on success, else 0.
- **`MACH_RCV_TOO_LARGE`**: header (incl. `msgh_id`) is valid even
  though body is truncated → allow msgid passed on SUCCESS or
  TOO_LARGE; 0 otherwise. Documented decision needed.
- **mach_msg2_internal**: combined-header plumbing differs — verify
  all three real wrappers get symmetric pre/post leg calls; prototype
  only models call/ret generically. Flagged as a required input check
  for the backend task.
- `MACH_CALL` on recv-only calls gets msgid=0 — multiple recv-only
  CALLs on one tid then pair ambiguously only if interleaved RETs
  interpose; nearest-unmatched-same-tid rule covers it, state that
  ties resolve by record order.

## Corrected acceptance rows (replace D149 §per-test)

| Test | Accept when |
|---|---|
| gate-retry | `d141_hook_gen_init` + `gate_spun` counter (or cas-fail hook) give provable spin observation; gate re-CAS on UNINIT; `gate_rc==1` deterministic |
| CAP-boundary | `emitted==511`, `dropped==1`, mid-drain `==0` at open slot, final drain 256 records claim-ordered `[DEADBEEF, followers…]`, `uncommitted==1` cumulative |
| overlap churn | `drain_started` latch + parked-producer prefix proof; per-record `aux==f(msgid)`+magic+unique msgid; conservation exact |
| post-call aux | API route: PASS_EXIT.aux == post-call-derived; OR contract route: count-derivation line + decoder rule; README claim reconciled either way |
| mach legs | distinct-id join, send-only msgid rule, TOO_LARGE decision, nested LIFO; verify all three wrappers' leg coverage |

## Corrected D150 backend task body (replaces D149's)

> **D150b backend — repair D141 candidate per D150-corrected packet.**
> Edit only `analysis/d141-svctime-candidate/`. Implement: (1) gate_init
> re-CAS on UNINIT inside bounded spin AND `g_gate_spun` counter or
> `d141_hook_gate_cas_failed` for deterministic spin observation;
> (2) `d141_hook_gen_init` invoked while set_generation holds
> INITIALIZING; T15 retry schedule per D150 §defect-1; (3) T16
> CAP-boundary per §defect-2 (emitted==511, dropped==1, mid-drain==0,
> ordered 256-record final drain); (4) T17 overlap churn with
> `drain_started` latch + parked-producer prefix proof + per-claim
> payload pattern; (5) post-call aux per §item-4 (choose API or
> contract route, state which); (6) mach-leg semantics per §item-5
> incl. send-only msgid rule, TOO_LARGE decision, wrapper leg coverage
> check; (7) T13 non-flaky (gate_rc∈{1,-1}+deterministic retry OR
> re-CAS makes it deterministic) + remove tautological else; (8)
> contract lines D1/C3/D8/D9/D10 per D149 §item-6. No production
> source/permits/package/device/sentinel/GUI. Keep record format;
> document API deltas. Report check counts. Reviewer applies
> `docs/evidence/devin-d150-frontend-20261020.md` rows.

Milestone reminder: commit only after the reviewer accepts a concrete
backend milestone; read-only audits require none.
