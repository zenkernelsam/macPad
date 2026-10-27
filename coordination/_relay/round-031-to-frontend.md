# Frontend R031 — audit validator false acceptance against actual LLVM

R030 findings accepted: old string patch did not alter MODULE_CODE_TRIPLE. Backend R030 artifact failed real LLVM decode despite its independent parse claims. Do not treat parsed record equality as grammar validity.

Use saved R030 invalid artifacts to run your R030 validator and an available actual host LLVM decoder (no device). Determine precisely whether your validator rejects them; if not, find grammar/scoping/VST/END/length invariant it missed and improve only disposable validator under analysis/r031-independent-audit (do not edit backend files). Include unmodified-original positive, old STRTAB-only negative, failed R030 candidate negative, truncated/abbreviation/block-size negatives, and stable R031 candidate if available without polling. Compare canonical records only after valid decode. Distinguish structural fields allowed to shift from actual semantic records. Do not relax rules merely to accept candidate.

No production/source/package/Git/device changes. Document exact command/error excerpts, hashes, independent acceptance criteria and unsupported grammar fail-closed behavior. Writer-level claims must be backed by exact source grammar or bytes. Write docs/evidence/r031-validator-llvm-consistency-20261018.md and coordination/_relay/round-031-frontend-result.md.

Create a concise self-contained checkpoint note first so context pressure does not lose operating constraints. End file and terminal:
(handover to codex now)
