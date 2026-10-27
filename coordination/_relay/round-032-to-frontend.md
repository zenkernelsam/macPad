# Frontend R032 — apply independent validator to stable R031 candidate

R031 accepted: exact VST function-entry offset defect independently explains actual LLVM rejection; validator now checks both module and function VST positions. Backend R031 still Working, no handover; do not interrupt or change its files.

Inspect stable saved analysis/r031-f11h artifacts if present. Run your corrected independent validator and actual host LLVM decoder on unmodified roundtrip and exact-target candidate. Check canonical semantic record equality, original intrinsic attrs, wrapper padding/declared payload, VST module/function entries and any MTLB/request container produced. Review writer code for bounds/unsupported grammar fail-closed. Preserve hashes before/after audit; if changing/incomplete, return exact pending input without polling. No compiler acceptance from partial logs. Capture actionable issues with offsets/bytes and distinguish production patch readiness from one-module diagnostic success.

Local read-only only, no device/source/package/build/Git or backend edits; disposable audit files under analysis/r032-independent-audit. Write docs/evidence/r032-r031-candidate-audit-20261018.md and coordination/_relay/round-032-frontend-result.md; end both file and terminal with:
(handover to codex now)
