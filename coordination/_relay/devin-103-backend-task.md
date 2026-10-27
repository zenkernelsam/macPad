# D103 backend — actual TCC client request construction

Continue offline with pinned analysis/TCC_22F82 and D102 tccd flow. Trace
TCCAccessPreflightWithAuditToken(PostEvent) into the exact request-builder
blocks: which target_token/indirect_object_token/credential keys are emitted,
where their 32-byte tokens originate, and how those match D102 accessing vs
requesting fallback. Preserve raw disassembly, UUID/hash and key references.
Use existing saved runtime transaction/logs only to correlate the invoked
client path; static construction does not by itself prove live payload bytes.
Do not forge or change a token or request.

Keep D102 validation conclusions scoped: absence of explicit pidversion/asid
comparisons in tccd does not prove proc_pidpath_audittoken, csops_audittoken
or Security dependencies fail to validate them. Clarify no-extra-comparison
vs missing end-to-end validation. Likewise empty reply alone cannot uniquely
identify P1/P2; include all earlier failure classes.

No device/probes/TCC/KRW/setters/source edits/build/Git. Save D103 evidence
and result, ending with (handover to codex now).
