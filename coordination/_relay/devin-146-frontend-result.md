# D146 frontend result — 2026-10-19

Positive-witness audit of frozen D127/D130/D134 evidence complete.
Report: `docs/evidence/devin-d146-frontend-20261019.md`.

## Governing fact (new, load-bearing)

`#define fprintf macws_filtered_fprintf` (:334) + latched
`macws_runtime_diagnostics_enabled` (`_Atomic cached`, sampled once) =
production `.err` can only ever contain the provider's raw-write
`MACWSHook: REFUSED` lines. Every `#### …installed`/`delivered` line is
unreachable without a cold, sentinel-armed start — absence is
information-free.

## Answers

1. **Provider mapped**: REFUSED lines = direct in-process witness.
   r013 saved artifact has 58 (older gen); D130's 17-line count for
   WS 22527 is transcription-only — the gen-err was never archived.
2. **Permits**: no direct positive witness exists by design (success
   logs are filtered). Sole callsite of `macws_publish_scanout_after_
   swap` (:21856) + advancing `validated-final-composite` seq +
   `producer=<WS pid>` ⇒ QC-observer permit; SwapEnd permit is
   transitively required by the same chain (its translation produces
   the cancellation path). **Pacing permits are unwitnessed in all
   saved generations.**
3. **Publisher**: firing is consumer-direct (displayd state file);
   install is inference via sole-callsite; per-fire `delivered` log is
   cold-load-only.

## Cautions flagged

- An "installed" line alone never proves install under the real
  provider: REFUSE sets `*result=orig` (non-NULL) → discriminate by
  `trampoline != target` + no same-target REFUSED.
- State-file producer binding is generation-pid-scoped — must re-check
  WS liveness.

## Smallest no-restart observations (for a future device task)

1. Re-read final-composite state twice (publisher still firing).
2. Save live WS `.err` (provider-mapped witness + closes artifact gap).
3. Verify WS pid liveness.
4. Read the 16 bytes at `publicSwapEnd`/`enableTagList`/pacing offsets
   in live WS — patch-stub pattern is the only retroactive install
   witness, and the only possible pacing witness.

## Cold-load-only list

All `####` lines (filtered+latched), success-path permit rc (never
logged), SENDEROBS hooks (SkyLight-load sentinel), any install-time
witness for a running generation.

(handover to codex now)
