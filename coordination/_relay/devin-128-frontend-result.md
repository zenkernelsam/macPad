# devin-128 frontend result — IconServices crash identity/trust audit

Read-only; evidence doc `docs/evidence/devin-d128-frontend-20261019.md`.

## Findings

- **Identity FACT**: chroot `iconservicesagent` arm64e `c0891064`
  (fat `[6a93e10f, c0891064]`; iOS copy `eb76fa5e` differs), uid 0,
  parent launchd, coalition `com.macwsguide.iconservicesagent`; SIGABRT
  on `com.apple.MPS.MPSLibrary.preheat` —
  `MPSKey_Compile→MTLReportFailure→__assert_rtn`, verbatim F11H
  assertion quoted. Sibling `iconservicesd` (uid 240, `240:240 0700`
  store) is a different identity and **stable**. Loaded
  `/usr/local/lib/libmachook.dylib` `c439dbec` present in usedImages;
  both agent CDHash `1c0d3271` and libmachook `ee0f018f` trusted —
  dependency trust closed; libmachook not implicated in the stack.
- **Directory facts**: `HOME=/Users/root`, `TMPDIR=/tmp`, agent cache
  dir absent, store 240-owned — observations only; R005's verdict holds
  (not a uid/HOME/cache cause; supplier UID501/GID20 recipe untested
  and off-target for an MTLReportFailure abort).
- **Causal boundary**: paired iOS-native `MTLCompilerService`
  (`6d2cfe56`) abort in `readModuleFromBinaryRequest`, same coalition,
  40 s prior — FACT-level pair; the exact module lineage remains
  MISSING (D095's no-same-module limit preserved). R024 attr-71 is
  same-module evidence for the owned endpoint module only. R031
  canonical retarget passed decode gates but **was never replayed in
  the exact service** — park preserved, no production-fix claim.
- **Panic boundary**: only panic report in tree is
  `analysis/dyldwork/panic_2130.ips` (Sep-26, ptrauth DA-key,
  `python3.9`) — unrelated era. Oct-19 bundle has **no panic report**;
  the Oct-18 12:26:48 restart was a userspace respring (kernel uptime
  >1.5 d). The user-reported reboot **cannot be attributed** to
  IconServices; no causal record exists.

Minimum next artifacts: production request/module hash for same-module
proof; the panic `.ips` itself for the reboot question; authorized
un-park + bounded replay for R031.

(handover to codex now)
