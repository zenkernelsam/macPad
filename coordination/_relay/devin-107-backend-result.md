# D107 backend result — kernelcache identity + bounded static audit

Offline only. Doc: `docs/evidence/devin-d107-backend-20261019.md`; raw disasm:
`analysis/kc-d107/mac_syscall-0xfffffe000865db70.txt` (`60122e94…`).

## Reconciliation verdict

- **D094's "target T8103 kernelcache are absent" is incorrect as written** —
  correction needed. Both artifacts exist and hash-verify: img4 `78959dd9…`
  (byte-identical to deployed device kernelcache per D064's live sha256sum;
  T8103-personalized despite the `T8112` basename) and `kc_raw` `6673ce28…`
  (MH_FILESET, `xnu-8792.82.2~1/RELEASE_ARM64_T8103` = live uname).
- **Genuinely absent**: (1) bvx2 decompressor → kc_raw↔img4 payload byte-link
  never established (named missing extraction stage); (2) Ventura dyld-cache
  fixup records → libquarantine stub provider still unnameable; (3) kc_raw
  symtab/export trie → kernel names resolvable only via callsite work.

## Bounded audit — image-verified in kc_raw

- `mac_syscall` `0xfffffe000865db70`: all three arms confirmed verbatim —
  precheck `+0x388` `blraa` (diversifier `0x6827`, error-aggregation ladder,
  foreign veto CAN produce 103), name-strcmp dispatch → `+0x3a8` `blraa`
  (`0xfee3`), and **two** `mov w21,#0x67` presets (`de80` busy-path,
  `dfbc` fast-path exhaustion). Zero `Quarantine\0` in the 80 MB image —
  no in-image registrant.
- **mac_late vs unload scoped**: static code proves *persistence* only —
  non-UNLOADOK policies can't unload for the boot epoch (EBUSY gate); late
  registration is blocked only for NOTLATE policies (EPERM gate at
  `mac_late=1`, boot-time). It proves nothing about what was actually
  registered — `Quarantine` epoch absence still needs the snapshot + no
  post-boot kext vector (SLE absent). Errno 103 never implies the no-match
  arm by itself.

(handover to codex now)
