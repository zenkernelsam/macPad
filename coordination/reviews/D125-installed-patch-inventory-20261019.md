# D125 installed patch inventory review — 2026-10-19

## Verdict: APPROVED_WITH_LIMITS

The final D125 evidence note is a sound source/install inventory when read
with the saved device hashes and R010/R012/D035 records. The earlier terminal
summary was stale on the dirty provider classification; the final note
correctly states that current `misc/cydiasubstrate_macos.m` hash
`fa6579a2…` is source-only, while the device has the R012 provider build.

### Installed and preserved

- F1 route ordering `4bcf20d` and provider/AppInput code `3c4d581` are present
  in the R010/R012 package/build identities. D041 independently proves the
  exact PID/window AppInput path reached Finder `sendEvent`, opened Cmd-F and
  inserted visible text. The specific malformed global-flag rejection arm
  remains contract-test evidence, not a fresh device exercise.
- R008 lifecycle/readiness/trust package and R012 XPC A-key/zero signing fix
  are installed. R012 records a successful package install and byte-identical
  installed arm64e callsite; its exact call execution and pixel/input layers
  have their own limits.
- The rootfs D035 V2 libmachook slices are separate from the R012 package
  copies. Current device hashes match D035 (`aa041efd…` / `21f81b5c…`) and
  contain the already-deployed XPC/permit-gate behavior. A package upgrade
  must preserve them from current source rather than reinstalling the stale
  Oct18 14:26 local `.deb`.
- The R012 provider framework is installed and package-owned: device main
  SHA256 `992b70d3…`, arm64 `2ad4c652…`, arm64e `6dfa858e…`; rootfs publisher
  has its own verified target `b2ae21da…`. This is not proof that the later
  dirty provider source `fa6579a2…` is installed.

### Missing product source and open runtime layers

- D043-D048 sender-observation integration (new `.c/.h`, Makefile entry and
  `mac_hooks.m` group C) is source-only. The exact installed arm64e library
  contains legacy CGXSYNTH strings but no `SENDEROBS`/`sender_observation`
  strings. Host/helper validation does not equal live WindowServer acceptance.
- Dirty `misc/cydiasubstrate_macos.m` relocator changes are not in the
  R012-installed provider according to the final D125 source/mtime/build
  comparison. Re-run its focused host tests and include its current exact
  source only if the new package build proves it.
- The exact-window input witness in D041 passes at AppKit and visible-pixel
  layers. Global WindowServer input remains `1002`; these are separate paths.
- Current device has no WindowServer and reports GUI script exit 1. Thus no
  current provider mapping, SENDEROBS record, final composite or visible
  pixels are established. The old exit1 is not attributed to TCC.
- R016 reopen freshness is the existing acceptance path and should be reused
  after any new ready generation.

Do not install the stale local `.deb`; it predates the F1/provider payload and
would revert installed state. D127 is the active backend build/deploy task;
D126 is a read-only frontend audit of the current exit1 evidence. No product
patch was installed in this review turn.
