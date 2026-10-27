# D127 backend — build and install only the verified missing product source set

The user has explicitly authorized installing and testing all product patches,
including pre- and post-panic work. D125 is the frontend's read-only inventory.
Use its final evidence note, not its stale terminal summary. Do not install the
local Oct18 14:26 `.deb`: it predates F1/provider package and would regress the
current device.

## Reconciled installed baseline

- Device is iPad13,11/T8103; package 0.3.4/R012 installed; GUI status is
  WindowServer=no, GUI script exit1. Do not attribute exit1 to TCC.
- F1 `4bcf20d`, provider/AppInput `3c4d581`, R008, R012 XPC PAC-signing fix,
  D035 V2 rootfs libmachook, and R012 provider framework are already installed
  per `docs/evidence/devin-d125-frontend-20261019.md` and exact device artifacts.
  Preserve all of them; avoid duplicate installation claims.
- Missing product source: D043-D048 SENDEROBS integration
  (`libmachook/mac_hooks.m` group C + `MacWSSenderObservation.{c,h}` + Makefile
  source entry) and current dirty `misc/cydiasubstrate_macos.m` relocator
  changes (`fa6579a2…`, source-only per D125).
- Preserve dirty `mac_hooks.m` group A (XPC pointer signing) and B (permit
  gates) already needed by the deployed runtime. The whole source file must be
  built as one unit; document the three groups separately in the manifest.
- Exclude D113/D120 transport prototype, untracked probe/test C sources,
  compiled `misc/macos_ws_event_probe`, `tmpdir-lc/`, docs and relay state from
  the package. Verify explicit payload allowlist; no `analysis/` staging glob.

## Candidate and tests

Freeze exact input identities before build: HEAD, `git diff` hashes for
`libmachook/mac_hooks.m`, `libmachook/Makefile`, and
`misc/cydiasubstrate_macos.m`; hashes for new observation C/H; list all staged
payloads. Build the complete package in an isolated task-specific build/stage
directory from current intended sources. Do not use stale `.theos/obj` or the
old `.deb` as the candidate.

Before device writes run:
- focused AppInput/F1, provider, artifact/package, host relocator, and frozen
  sender-observation tests;
- both arm64 and arm64e product builds; exact D048 arm64e helper evidence may
  be reused, but source/hash identity must match;
- runtime-switch audit, shell/plist syntax, real archive payload verification,
  and full `misc/test_*.py` suite when feasible.

Fail closed on any failed identity, test, package allowlist, architecture,
platform, signature, entitlement, or trust check. Save exact exits. Do not add
test-only binaries to the package.

## Device install and one bounded test

After a fresh bounded read-only device inventory, preserve exact rollback copies
of every destination. Verify all pre-existing R012/F1/D035 hashes first. Stage
with new inodes, package/postinst using the existing strict rootless trust
contract, verify both libmachook slices, the CydiaSubstrate provider, package
version, CDHashes and trust membership. Confirm the rootfs runtime copies
include both D035 permit/XPC behavior and the new SENDEROBS integration. If any
postinst/hash/trust check fails, do not start GUI; restore only files changed
by this attempt from those exact backups.

Baseline GUI is already down with exit1. If install is clean, use the existing
owner-managed macwshostd/control path for at most ONE start attempt; capture
the exact prior failure boundary and new log/status delta. Do not retry in a
loop or diagnose TCC without direct evidence. If ready, use the dedicated
default-off sender-observation sentinel for one canonical bounded event only,
then remove it and verify no probe/debugger leftovers. Report separate layers:
package/hash decode, provider mapped/permit install, SENDEROBS record, final
composite sequence, inspected visible pixels, and exact-window F1 acceptance.
Use existing `WaitForWindowMetricsFlagsAfterGeneration` reopen freshness logic.
Do not claim input works from classify tests; live global `1002` remains a
separate route unless a fresh exact-window witness says otherwise.

Save a dated source/build/deploy report and relay result with source and device
SHA256/CDHash/UUID, backup paths, true command exits, log excerpts, crash delta,
cleanup, and all remaining gaps. Candidate/product acceptance is limited to
what the evidence proves. Finish with `(handover to codex now)` on its own line.
