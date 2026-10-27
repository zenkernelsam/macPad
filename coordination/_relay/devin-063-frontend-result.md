# devin-063-frontend — provenance + contradiction + #103 audit RESULT

**Doc:** `docs/evidence/devin-d063-frontend-20261019.md`
(Task file absent from relay; dispatch text executed verbatim.
Local read-only.)

## Image source — resolved, filename misleads

Device `iPad13,11`/`RELEASE_ARM64_T8103`. The img4 manifest carries
`CHIP=0x8103` in both entries — the cache IS T8103-family; `.bin`
contains T8103 (LEAP/MCA2) + T8112 (DPTX/USBXHCI) drivers = shared
arm64e cache. "T8112" filename is a mislabel; **the Quarantine-name
absence holds for the deployed device's kernel**. Residual: byte-level
`.bin`↔img4 decompress match not done; device-side kc pull closes it.

## libquarantine contradiction — dissected

`8CE8DAD0` = **MACOS 15.6 + MACCATALYST 18.6** universal (vtool);
its symtab exports **35+ responsibility symbols** incl. the `0xb6`
getter (`0x18da94750`, the file R008 disassembled). Backend D062's
"UUID 8CE8DAD0 = iOS pull with zero responsibility strings" is
contradicted — either a `strings`-vs-symtab measurement error, or a
different file under a colliding-UUID claim. Preserved FACT on the
other side: **iOS's own dyld cache has zero libquarantine images** —
iOS userspace carries no SPI regardless.

## #103 path — narrowed

`__mac_syscall`(#381): copyinstr → `mac_proc_check_mac_syscall` veto
(any errno) → **default `ENOPOLICY`** → name-strcmp scan → dispatch.
On this kernel: no "Quarantine" policy → producer = the no-match
default (`mac_base.c:1587`). Precheck-veto returns of 103 are possible
only if a registered policy hooks `mpo_proc_check_syscall_mac` —
unaudited, flagged THEORY. All selectors 0xb4–0xbb equally dead.

## Settled / still-open

- registration-vs-provider split is **dead**: provider absent ⇒ #103
  unconditional; procinfo-family reads can't speak to it anyway.
- Still open (local only): which TCC services set
  `macos_shouldAllowSameTeam`/`macos_isPerSystem`; iOS libsystem_kernel
  `__mac_syscall` export presence; byte-exact kc↔img4 linkage.

(handover to codex now)
