# D165 frontend result — 2026-10-20

D164 arm/generation comparability audited; amendment appended to the
D164 report (original preserved). Audit doc:
`docs/evidence/devin-d165-frontend-20261020.md`.

## Conflict resolved

"Identical WS generation" × "cold start per arm" was internally
inconsistent — `gen` is pid-derived, so every cold start mints a new
tag. Stratified: identical across arms = image UUID, instrumented
build (armed in BOTH arms), probe payload, sender, device state,
thermal/workload, producer identity; the wait flag is the ONLY
variable. Differs per arm by construction: `gen` tag and `tid` value
(role-resolved, never value-joined). Cross-arm comparison is
distribution-level; no cross-generation record joins.

## Sampling validated

≥30/arm, single fixed wait interval, and `max(8ms, 0.5×interval)`
clustering all trace to D138's design verbatim — no numeric
invention. Drop-flagging preserved. Controls list added without
numeric acceptance.

Design only — no source edits, no builds, no device, no integration
authorization. No commit.

(handover to codex now)
