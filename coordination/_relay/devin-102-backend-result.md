# devin-102 — backend result

Exact token-identity trace through TCCDAttributionChain complete.
Offline only — no device/probes/setters/TCC/KRW/builds/edits/Git.
Evidence: `docs/evidence/devin-d102-backend-20261019.md`.
Artifacts: `analysis/d050-tccd/d102/` (disasm `a31e04ab…`, raw bytes
`f15ded33…`).

- **Token provenance (traced through method lists, not inferred)**:
  `_requestingProcess` = kernel-attested `xpc_dictionary_get_audit_
  token(msg)`; `_accessingProcess` = client-supplied 32-byte dict
  token (`target_token` / `indirect_object_token` / credential-dict
  `AUTHENTICATOR_AUDIT_TOKEN` @ type==1), all len==0x20-gated;
  accessing getter falls back to requestingProcess. **The getter's
  subject is the dict-declared token**, not the attested sender.
- **Returned token**: responsible out-token consumed only on rv==0 →
  pid equality + `binaryPath` equality decide self vs
  `initWithAuditToken:responsibleIdentity:` (RI built from the
  *accessing* token) → +0x18.
- **Validation gaps**: pidversion/asid never checked on this path
  (no callsites; au32 called with asidp=NULL) — pid-reuse/session
  unchecked. `proc_pidpath_audittoken` and SecTask/csops are the
  real nil producers; resolved-path nil is tolerated.
- **Nil identity = distinct later producers**: P0 getter-rv (#103
  class, errno log) vs P1 RI-nil (attribution NULL) vs P2
  process-init nil (SecTask/proc_pidpath) — same empty-reply shape,
  different log tails; matches D060's bailed-for-another-reason arm.
- Bonus arm: `processInfo != nil` skips the getter entirely.

(handover to codex now)
