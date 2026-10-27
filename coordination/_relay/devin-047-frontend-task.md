# D047 frontend — D046 final boundaries and minimal raw preflight design

D045 independent offsets/segment/descriptor audit accepted. EMPTY/default1 is not explicit policy denial. Your D044 all-pass wording is scoped to tested snapshot, not FIFO blocking or complete wrapper incoming-errno behavior; backend D046 actively fixing those. Do not contact/interrupt backend or edit their files.

Local read-only independent review: inspect stable D046 changes if present, validate safe nonblocking log opening (FIFO/device/symlink/permissive-existing-file rejection), incoming errno restoration BEFORE original plus original errno/result at return, actual wrapper tests rather than emitter-only checks, live disarm ceasing token reads, bounded record budget and precise latency caveats. No relaxed approval based on prior35 checks. If files changing, record exact pending snapshot, no polling.

Also deliver design ONLY for minimal fourth TCC preflight boundary observation to distinguish raw0/1/2 while preserving three-hook invocation correlation. Verify exact TCC UUID/offset/prologue and full ABI from actual binary, lazy resolution/PAC, whether hook is reached through existing resolver, token/service argument ownership and failure paths. Raw1 still cannot distinguish EMPTY/default/policy verdict: explicitly map remaining reply/transport gap and avoid promising it resolves policy. Compare cost/benefit of fourth hook vs existing bounded reply evidence; do not implement or expand production source. Dedicated toggle must remain default-off and independent of display diagnostics. No deploy/restart/device/build/Git changes.

Write docs/evidence/devin-d047-observer-final-independent-review-20261019.md and coordination/_relay/devin-047-frontend-result.md with precise blockers and smallest next experiment. STOP after handover.
(handover to codex now)
