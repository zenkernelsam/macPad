# devin-136 frontend result — TCC upstream-cause reconciliation

Read-only synthesis; full ledger at
`docs/evidence/devin-d136-frontend-20261019.md`.

## Ledger

- **(a) pre-remap**: all FACT — system-domain absent (scoped), 8/8
  connection_invalid, Metal_hooks.x table was the missing map; R005-era
  w0=1→1002 was client-side transport default, **no tccd ran**.
- **(b) post-remap**: real `OS_xpc_dictionary` replies round-trip FACT;
  `get_bool("result")→0` FACT-as-read but key presence unproven;
  `count=0`-only shape; 1002 persists.
- **(c) #103**: msgID-correlated FACT (WS 48891 msgids .17/.19 →
  `from PID[target]: (#103)` → `count=0` → `sls_rc 1002`); logged pid =
  **declared target token's** pid; fires for launchd-hosted, self-target
  and native tokens alike → environment/provider-level; tccd's logged
  `sender_auid=501` is interpose-forged.
- **(d) upstream**: FACT — getter → helper → auth stub with
  `"Quarantine"`/`0xb6`, no errno synthesis in-image, propagated
  verbatim; THEORY — provider=`__mac_syscall` identity (slot
  `0x1dfef8f88` unresolved offline) and which of three kernel producers
  emitted 103.

## Contradictions resolved

D103 len!=0x20 producer → retracted (D104 erratum); D050 "connection
audit" vs D105 "target token" → D105 stands (self-target was
degenerate); R002 zero-lines vs R003 #103 → cached vs fresh-window, both
true; R005 "hosting suffices" → refuted twice; D105 unique-P0 vs D106
three kernel producers → both true at different levels; D056 stray/SETEXEC
→ D057 retracted (SETEXEC active).

## Missing witness + discriminator

Current-generation (WS 22527) tccd-window quadruple and any decoded
reply dict are absent; no successful responsibility lookup ever
observed. Smallest discriminator = **frozen v3 `mac_policy_list`
snapshot** (`8bb90399`, read-only, prepared-unexecuted) + same-boot
getter rc/errno pairing — decides branch (b) kernel-policy-gap (→ no
userland repair justified) vs (c) policy-denies (→ spawn-side
registration justified). Optional cheaper prerequisite: 8-byte live
read of the stub slot resolves the provider identity.

(handover to codex now)
