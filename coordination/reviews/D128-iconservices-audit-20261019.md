# D128 IconServices audit review — 2026-10-19

## Verdict: APPROVED_WITH_LIMITS

D128 correctly reconciles the saved R005/R013 crash evidence and D071/D094/
D095 causal limits. It separates the chroot `iconservicesagent` from its sibling
store daemon and from the user's reported iPad panic.

- **FACT**: the saved chroot agent is arm64e UUID `c0891064`, uid 0, launched
  by the project job; its MPS preheat queue aborts through
  `MPSKey_Compile -> MTLReportFailure -> __assert_rtn` with the verbatim
  F11H compiler-internal-error assertion. Injected `libmachook` is in the
  used-image list; the captured stack does not implicate it.
- **FACT**: R005 records the agent CDHash and injected libmachook CDHash as
  trusted. `iconservicesd` is a separate uid/gid 240 service with a
  `240:240` mode-0700 store and no matching crash in that capture. The agent
  HOME/TMPDIR/cache-directory facts are observations, not a causal diagnosis.
- **FACT with limit**: an iOS-native MTLCompilerService abort occurred about
  40 seconds earlier in the same coalition, but no production request/module
  hash links that request to the agent's failing F11H compile. R024 attr-71
  reproduces only for the owned endpoint module; R031 was never replayed in
  the exact service and remains parked.
- **FACT**: the saved Oct19 device bundle has no panic `.ips`; the only panic
  report in the checkout is the unrelated Sep26 Python/dyld DA-key report.
  The Oct18 SpringBoard restart was a userspace respring with kernel uptime
  greater than 1.5 days. The user's reported iPad reboot therefore remains
  unattributed.

No deployment or policy change follows from this audit. Minimum missing items
remain the production request/module hash for same-module proof and the actual
panic `.ips` for reboot attribution. Any R031 replay remains parked pending its
separate explicit renewal.
