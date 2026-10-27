# devin-133 frontend result — mount_bindfs vendoring suitability

Read-only audit; full report at
`docs/evidence/devin-d133-frontend-20261019.md`.

## Verdict

- **Binary verified**: `34c9c30b`, fat arm64+arm64e, iOS platform,
  minos 14.0, libSystem-only, ldid adhoc CodeDirectory (CDHash-able).
- **D132 overclaim refuted**: `34c9c30b` has **no symlink/unlink
  imports** — mount-only, structurally incapable of the documented
  symlink fallback. The Oct-6 "via symlink" output came from a
  different, locally-unavailable helper build. Provenance gap: real.
- **Contract check**: `SRC DST` argv compatible; err/errx nonzero on
  failure → fail-closed preserved; no entitlements/libjailbreak → runs
  with caller creds (mountdevfs's credential-borrow comment does not
  describe this binary).
- **Suitability**: vendoring restores the mount path only; on a
  bindfs-rejecting kernel with no exposure it does not clear the
  blocker. Revised fix = vendor sha-pinned copy + explicit
  `add_all_trustcache` line + relocate the symlink fallback (postinst
  provisioning or script) + correct the "packaged helper" comment.
- **New unverified premise flagged**: T8103/16.3 bindfs rejection was
  never runtime-verified (only iPad14,3/16.5.1) — the device may accept
  the mount outright.
- **Pre-install decode checks enumerated** (sha pin, both slices, iOS
  platform, libSystem-only imports, per-slice CDHash→trustcache, 0755,
  helper-check acceptance).
- D132 ledger: vendoring direction stands narrowed; three imprecisions
  corrected.

(handover to codex now)
