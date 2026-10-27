# Backend R026 — F11H bitcode attribute compatibility, offline first

R024 accepted: exact diagnostic `Unknown attribute kind (71) (Producer: 'LLVM16.0.0' Reader: 'LLVM ')`, same complete R014 request, verified private hook. The hook's `_exit(137)` is diagnostic termination only; not native abort behavior or acceptance.

Goal: trace the rejected serialized attribute kind 71 to the exact producer operation and establish a semantics-preserving compatibility option. Work offline on preserved R014 request/module and converter source. Check LLVM 16 Bitcode/LLVMBitCodes.h and the actual serialization mapping, NOT only Attributes.td ordering (enum ordinal is not necessarily wire encoding). Record version/tag/source hashes and inspect exact module attribute records, function/parameter scope and all occurrences. Compare original MPS module vs converted payload if available and identify which conversion step introduced/preserved the incompatible record. Reader version remains unknown; do not invent it.

Read relevant metal2metal converter/profile code and exact existing tests; evaluate whether supported re-encoding/lowering exists. Do not strip attributes blindly, patch reader checks, weaken validation or call deletion a fix. No production/source/package/Git/device changes this packet. Local disposable analysis scripts allowed under analysis/r026-f11h only. If no semantics-preserving candidate is justified, deliver precise missing evidence and next isolated A/B design. Do not start another compiler/device run yet.

Write docs/evidence/r026-f11h-attribute-compatibility-20261018.md, coordination/artifacts/R026-f11h-attribute-compatibility-20261018.json and coordination/_relay/round-026-backend-result.md with exact raw excerpts, hashes, FACT/THEORY and limits. End result AND terminal reply with:
(handover to codex now)
