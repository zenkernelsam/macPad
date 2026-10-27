# devin-100 — backend result

Exact-binary audit of `analysis/d050-tccd/tccd_22F82_live` complete.
Offline only — no device, TCC/KRW, builds, edits, or Git.
Evidence: `docs/evidence/devin-d100-backend-20261019.md`.
Artifacts: `analysis/d050-tccd/d100/d100-disasm.txt` (sha256
`633019e2…`), `d100-raw-bytes.txt` (sha256 `029ef7ca…`).

- **Correction**: D096/D097's "Ventura tccd absent locally" was wrong —
  `tccd_22F82_live` sha256 `42c388e8…` / arm64e UUID `9845D17F…`
  verified. Ventura launchd remains absent. Thin slice `tccd_arm64e`
  = same slice; VAs are `0x100000000`-based (fat adds 688128).
- **`-[TCCDAttributionChain _evaluateResponsibleProcess` @
  `0x10004fac4`** (identity verified via relative method list
  `0x10005d6e0`→selref `0x10008cac0`): errno cleared, then ONE
  `cbz w0` gate on `responsibility_get_responsible_audit_token_
  for_audit_token`. **No absent/failure/malformed discrimination** —
  errno is read only by the os_log error helper (`(#%d) %{public}s`),
  never branched on; the output token is trusted unconditionally;
  "no entry" under a working provider returns as self-responsible
  success, not an error.
- **Setter endpoint mapped**: `handle_TCCAccessSetPidResponsibleForPid`
  @ `0x10008c70`, dispatched via strcmp on opname, gated by
  `macos_isSystemServer` + SecTask entitlement check
  (`com.apple.private.tcc.manager.set-responsible` string present);
  replies bool `result` only — client never sees rv/errno.
- Bonus: initWithAuditToken initializer @ `0x100022d18` uses
  `responsibility_get_attribution_for_audittoken`; NULL identity →
  nil.
- **Consequence**: strengthens the v3 grant — the daemon caller
  flattens every nonzero rv identically, so userspace observation
  cannot recover the kernel errno; only the `mac_policy_list`
  snapshot (or a syscall probe) decides provider registration.
  Setter rv is also unvalidated in the privileged-helper path
  (returns 1 regardless).
- Still NOT APPROVED; nothing executed.

(handover to codex now)
