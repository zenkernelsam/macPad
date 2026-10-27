# D064 frontend result — service registry + legitimate-path verdict

Scope executed: local-only RE on exact `tccd_arm64e` (22F82,
`9845D17F`) + `SkyLight_22F82`. No device, no TCC requests, no setter
invocations, no source/build/Git changes. Evidence doc:
`docs/evidence/devin-d064-service-registry-review-20261019.md`.
Overclaim corrections applied in-place with history preserved in
`devin-d062-frontend-20261019.md` and `devin-d063-frontend-20261019.md`.

## Corrections landed

- Provider absence → **HYPOTHESIS** in both D062/D063 docs:
  `CHIP=0x8103` does not close byte-level `.bin`↔img4 payload/runtime
  linkage; the live kernel's registered-policy list was never
  enumerated. Every "unconditional #103" statement is now conditional
  on that hypothesis. Historical claims retained inline.
- Refined my earlier Indirect wording: the `evaluateResponsibility`
  skip (`0x1238c`, `w2=!shouldAllowSameTeam`) applies to the
  **subject chain** built from the dict-supplied `indirect_object_
  token` — and is unreachable for failed senders anyway since the
  outer attribution (`0x6848`) precedes dispatch.

## Service registry (FACT)

Hardcoded per-service blocks `0x35200–0x35700`; flags are compile-time
`w2` literals, not plist data:

- `isPerSystem=1`: Accessibility, **PostEvent**, **ListenEvent**,
  **ScreenCapture**, SystemPolicyAllFiles, DeveloperTool.
- `shouldAllowSameTeam=1`: **only AppleEvents** (`0x35590`). No input
  service carries it; `hasIndirectObject` is AppleEvents/FileProviderDomain
  only — the Indirect subject-token leg isn't input-shaped.
- SkyLight consumes exactly PostEvent/ListenEvent/ScreenCapture —
  all `isPerSystem=1`.

## Legitimate-path verdict

No dispatch leg reaches a verdict-producing handler for input services:

- `tccd system` (deployed): every request → sender attribution →
  `0xb6` → #103 (if provider-absence holds) → empty reply.
- hypothetical non-system instance: `isPerSystem` services hit the
  `0x6710` early-bail → **same empty reply dict**, handler never runs.
- `count=0` cannot distinguish the two without the ERROR line (D060).

SkyLight side is fail-closed at every level (FACT): missing TCC symbol
→ deny; preflight nonzero → deny. `0x185425da0` calls
`TCCAccessPreflightWithAuditToken(service, &auditToken, 0)` and returns
`w0==0`; NULL symbol falls through returning 0.

## Global-input vs exact-AppInput

Global synthesis (`SLSPostEventRecord`/HID taps → PostEvent/ListenEvent
sender check) requires a real verdict on an `isPerSystem` service —
unreachable by construction on this deployment. The accepted
exact-AppInput path (per-PID/window AppInputBridge delivery) never
crosses the `PreflightWithAuditToken` sender gate — it remains the
correct and only working input mechanism. No duplicate transport
proposed or built.

## Missing runtime inputs (device/backend scope, not done here)

1. Byte-level `.bin`↔img4 linkage + live MAC-policy enumeration
   (controlled `"Sandbox"` vs `"Quarantine"` `0xb6` A/B — one syscall
   each, zero TCC) to resolve the provider hypothesis.
2. If provider present: first input request produces the ERROR line
   naming the next producer — observable with no code change.
3. iOS `libsystem_kernel` `__mac_syscall`/`__sandbox_ms` export check
   remains THEORY until device `dyld_info`.

(handover to codex now)
