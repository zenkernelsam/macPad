# devin-062-frontend — native responsibility interface + CFG audit RESULT

**Doc:** `docs/evidence/devin-d062-frontend-20261019.md`
(Task file `devin-062-frontend-task.md` absent from relay — executed the
dispatch text verbatim; local read-only.)

## Native interface — verified structurally absent on iOS

- iOS 16.3 cache image table (2609 entries): **no libquarantine** —
  `responsibility_*` SPI family has no userspace provider; SYMBOL_MISSING
  is structural.
- `__mac_syscall` = syscall **#381 `AUE_MAC_SYSCALL`**, `ALL` platforms —
  kernel entry exists; iOS `libsystem_kernel` export presence unverified
  locally (`.19` subcache lacks it) → THEORY.
- `p_responsible_pid`: KPI-write only (Private.exports; kext-side
  caller), fork-inherited, **no userspace read flavor** — agrees with
  backend's all-MISSING device sweep. launchd `procinfo` fields are a
  different store.

## Provider existence — kernelcache FACT

iOS 16.3 T8112 kernelcache: exact policy name `"Quarantine"` = **0 hits**
(control names AMFI/Sandbox/AppleMobileFileIntegrity all present).
→ `__mac_syscall("Quarantine",0xb6)` hits `mac_base.c:1587` default
`ENOPOLICY` — **provider absent at kernel-build level**. Residual:
runtime kext can't exist — `Quarantine.kext` is macOS-only.

## Consequence (reframes the fix space)

Every `TCCAccessRequest`/Preflight sender on `tccd system` (flag=1,
argv `system` — D061-verified) reaches `_evaluateResponsibleProcess` →
SPI → #103 — **for any sender token, regardless of registration**.
R002's probe #103 was provider absence all along. **No launchd
registration can fix it.** The Indirect handler's live leg
`evaluateResponsibility = !service.macos_shouldAllowSameTeam`
(`0x1238c`) is the only in-code skip; whether SkyLight's services carry
that flag is an unchecked service-registry fact + the
`override-prompt-policy` entitlement gate stands beyond it.

## CFG audit additions

Builder bail `0x503f4` = nil → `0x69ac` → ERROR+empty send;
`0x69e8` leg = direct dispatch verified; `0x6710` = early-bail send.
`count=0` still needs ERROR correlate.

## Missing inputs

iOS libsystem_kernel `__mac_syscall`/`__sandbox_ms` export check
(device or fuller cache); optional device `__mac_syscall`
"Quarantine"-vs-"Sandbox" A/B (one syscall each, zero TCC);
`shouldAllowSameTeam` per-service registry map (local RE on the same
binary).

(handover to codex now)
