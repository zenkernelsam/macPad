# devin-132 frontend result — patch inventory + mount_bindfs trace

Read-only audit; full report at
`docs/evidence/devin-d132-frontend-20261019.md`.

## Key results

- **Inventory table delivered** (§1): every relevant change mapped to D127
  deb inclusion + device hash + next proof. D127 deb carries `3c4d581`,
  `4bcf20d`, `b717fb9`/`c97e219`, R008, and all three dirty groups (R012
  PAC fix, unconditional permits, SENDEROBS `SENDEROBS` strings verified
  in payload+installed copy). No later frontend/UI patch exists — HEAD is
  docs, UI trees clean, dirty set = exactly D127's build inputs.
- **mount_bindfs trace (FACT)**: never in git layout or any extant deb
  (267/262 entries verified, zero matches); only copy is share-overlay
  fat binary `34c9c30b` signed with project `b703f318` id; helper itself
  creates the exact documented symlink on bindfs-rejecting kernels
  (Oct-6 runtime output verbatim). "Ships inside the deb" claims in
  `install_rootfs_13.sh:85` + install docs are stale.
- **Disappearance = THEORY**: dpkg upgrade pruning over an older
  owning deb is the plausible mechanism; unproven.
- **Narrow fix identified**: vendor `usr/local/bin/mount_bindfs` into the
  payload (postinst codesign/trustcache covers it), leave
  `ensure_jb_usr_bind.sh` fail-closed and unchanged, fix the stale doc
  claims. Rejected alternative: duplicating symlink logic in the script
  (semantic drift).
- **Hazard flagged**: `.theos/obj` still pre-F1/pre-SENDEROBS — a naive
  local package build would emit a stale deb.
- No runtime acceptance claimed; layers 2–6 remain unproven.

(handover to codex now)
