# D064 backend result — kernel linkage closed where possible, claims calibrated

2026-10-19. Local + two small read-only device outputs (uname, deployed-kc
SHA digest — no file pull). No TCC/setter/MAC-syscall/deploy/restart.
Evidence: `docs/evidence/devin-d064-kernel-linkage-20261019.md`.

## What the artifacts prove — and don't

- **FACT**: device boot kernelcache SHA-256 (`/private/preboot/…/kernelcache`,
  21.8 MB) = `78959dd9…` = **byte-identical to the repo img4**. The analyzed
  image IS the deployed kernelcache image (T8103-personalized, mislabeled
  filename confirmed).
- **FACT**: `kc_raw` embeds `xnu-8792.82.2~1/RELEASE_ARM64_T8103` — the exact
  build string of the live kernel (uname-identical).
- **MISSING**: kc_raw↔img4 *payload* byte-linkage — bvx2/RAWPack2, no
  decompressor on host (img4tool/librawpack absent). Consistency is
  strong (build string + SoC strings + 30-year doc calls it T8103) but the
  decompression step was never run — do not claim byte-level payload identity.
- **Not claimable**: mapped-kernel byte identity at runtime.

## Withdrawals (history preserved in the doc)

- "Provider absent at kernel-build level" → scoped to: no "Quarantine"
  name *in this analyzed artifact*; dynamic/encoded registration can't be
  excluded by strings alone; Extensions-dir absence bounds only post-boot
  kexts. Close via `mac_policy_register`-caller enumeration.
- "Registration can never fix #103" → withdrawn pending that enumeration +
  a runtime boundary observation.
- R009's three-producer model stands: image evidence eliminates only the
  handler arm; precheck-vs-no-match formally open.

## Legitimate service path (the actual route to a real reply)

`com.apple.tccd` non-system instance: `isSystemServer=0` → messages reach
dispatch **without sender attribution** for `macos_isPerSystem=0` services —
the only in-code populated-reply path that never touches the dead `0xb6`
provider. `shouldAllowSameTeam` skips are subject-side inside do_Indirect
(unreachable past sender bail). Per-service flag map = MISSING input
(TCC.framework resources not extracted).

## Bounded discriminator (proposed)

Local: enumerate `mac_policy_register` callers in `kc_raw` → the exhaustive
registerable-policy set; "Quarantine" absent from callers = provider absence
proven for the image (can't cover runtime patches or a Quarantine-named policy
with NULL syscall hook — both recorded). Missing inputs: bvx2 decoder OR
scope conclusions to artifact-level; TCC resources pull for service flags.

Desktop untouched; last observed `ready` seq 5145.

(handover to codex now)
