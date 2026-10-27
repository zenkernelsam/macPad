# Frontend R022 result — EXC_GUARD namespc=7/reason=9 classification

Read R010/R012 package evidence, R021, the exact crash excerpts, R012/R013
crash deltas, local XNU source, the local 15.6 libxpc and the postinstall
codesign paths. Read-only: no code/test/build/package/device/SSH/Git change.

Evidence:
`docs/evidence/r022-exc-guard-classification-20261018.md`
(SHA-256 `4e12414c357375b65193f25f93d898f23772564bb4190dc2127280107ae28833`).

## Verdict

The codesign report is a **libxpc user-fault**, not a code-signing/trustcache
guard:

- Exact report: `EXC_GUARD`/`GUARD_TYPE_USER`, namespc 7, reason 9,
  termination `LIBXPC`/`XPC_EXIT_REASON_FAULT`, flags 518 (`0x206`).
- Local XNU `kern_exit.c:1342` shows this tuple is the *simulated* EXC_GUARD
  created by `os_fault_with_payload`; `reason.h:94` maps namespace 7 to
  `OS_REASON_LIBXPC`.
- Adjacent 15.6 libxpc `__XPC_MISUSE_FAULT` passes exactly `(7, 9)` to
  `_os_fault_with_payload`, with the misuse message as the reason string.
- The report contains only the 22F82 shared cache and codesign images; no
  provider and no libmachook image.
- The same tuple and shared-cache frame prefix recur in pkd, SystemUIServer,
  OSXvnc-server and many services (15 reports at 19:32, 29 at 19:50), outside
  any publisher/install window.

Attribution:

- **Publisher Identifier: not attributable.** The publisher signs with ldid,
  not codesign; postinst codesign targets are `MacWSInteropService.app`, not
  the provider; termination namespace is LIBXPC, not CODESIGNING; no provider
  image; timing alone is not causation.
- **Trustcache admission: not attributable.** `jbctl` is a different process,
  both provider CDHashes were live, and no jbctl report exists.
- **Codesign tool guard: partially implicated, trigger unknown.** Codesign is
  the process and caller frames are present, but the fault is libxpc's
  `__XPC_MISUSE_FAULT`, not a codesign-internal guard. The misuse string and
  exact codesign command are not preserved.
- **XPC PAC crash: separate.** Different exception, termination, frames,
  far address, and libmachook placement.

## Missing inputs to name the trigger

1. The `os_reason` reason string (`x4` at the libxpc fault call) or a device
   oslog/ReportCrash line around 18:24:44.
2. The exact codesign argv, parent PID 67477 identity and exit status; the
   install transcript has no codesign line and `DPKG_EXIT=0`.
3. Exact 22F82 codesign (UUID `AF141C28-…`) and libxpc (UUID `F3F19227-…`)
   disassembly to map the caller frames and the fault call site.
4. Optional A/B with the provider publisher disabled/old provider.

Limits: local XNU is 8792.81.2 vs device 8792.82.2; local libxpc is 15.6 vs
device 22F82. The device crash report is exact; adjacent sources establish the
mechanism, not an exact-build disassembly. No claim that the class is benign.

(handover to codex now)
