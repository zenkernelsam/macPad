# devin-138 frontend result — frame-wait evidence audit

Read-only audit; report at
`docs/evidence/devin-d138-frontend-20261019.md`.

## Verdict

- **No bracketed data exists in any generation.** Pace records were
  never emitted (D107/D109 is a candidate patch — never applied/built/
  deployed); SkyLight-side timing points are pinned-by-design only and
  their instrumentation was **never written** (candidate contains zero
  SkyLight hooks; would need new permit-registered hooks at the D129
  VAs). D102 stacks = placement proof only; composite sequence advance =
  cadence, not latency — cannot be inferred to wait-vs-queue.
- **D131 threshold unscored**: 0/arm samples, no exit clustering, no
  median excess, no current-generation services-tid. Producer wait vs
  queue wait never separated by data.
- **Absent fields enumerated**: all pace/recv/dispatch/reply timestamps,
  per-record tid, request-id/msgid correlation, generation/UUID stamps,
  no-wait baseline arm.
- **Minimum safe measurement**: pace half via the D107/D109 candidate +
  SkyLight half via *new* record-only permit hooks at pinned VAs; one
  variable = wait on/off, ≥30 `0x74a7` sends/arm, same UUID/workload/
  thermal; accept per the max(8ms, 0.5×interval) rule on services-tid.
- **Instrumentation safety**: candidate is safe-when-disarmed (cached
  stat only) and bounded-when-armed (32-B ring, publish-after-memcpy,
  errno-preserved, capped); residuals — never arm64-compiled, observer
  cost inside measured region, diagnostic-only per AGENTS.md, and alone
  it cannot answer causality.
- **D130/D135**: zero frame-wait measurement — D135 added a real
  composite pixel witness (L5's pixel half) + confirmed D130 never
  exercised reopen freshness; neither bears on wait causality.

(handover to codex now)
