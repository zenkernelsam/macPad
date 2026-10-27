# D124 backend — build, install and test every pending product patch

The user explicitly directed that all new product patches be installed on the
iPad and tested, including pre- and post-panic changes. This is authorization
for the bounded build/deploy/runtime verification below. Do not omit a patch
because it predates the panic.

Start by reading `coordination/REVIEWER_STATE.md`, the latest user/device
artifacts, `docs/evidence/r008-tccd-transport-repro-20261015.md`,
`docs/evidence/r012-provider-package-20261018.md`, and the D043-D051 reports.
Current device is reported at 192.168.68.51; refresh identity and installed
fingerprints. Credentials only via transient environment.

Patch inventory to reconcile against installed and packaged hashes:
- committed F1/exact route fix `4bcf20d` and provider/AppInput contract
  `3c4d581` (Host app + `libmachook` consumers);
- packaged macOS CydiaSubstrate provider `b717fb9` plus current uncommitted
  `misc/cydiasubstrate_macos.m` relocation/diagnostic changes;
- current uncommitted sender-observation integration in `libmachook/` from
  D043-D048 (diagnostic, default-off, exact permits);
- R008 lifecycle/readiness/trust package already deployed, verify rather than
  duplicate;
- D113/D120 is an offline test prototype, not a product payload: exclude from
  package and document that boundary; do not deploy it.

Fresh read-only inventory on 2026-10-19 confirms package version `0.3.4`,
`WindowServer=no`, and GUI script exit 1. A
`/var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework` already exists on the
device; its relationship to R012/current source is UNKNOWN until exact hashes,
slice metadata and trust membership are compared. Installed libmachook files
are thin arch-specific files whose hashes differ from the old local fat
package; do not infer source revision from size/mtime alone. The local Oct18
14:26 `.deb` archive itself lacks the newer provider framework payload, but
that does not prove the device copy is absent. Capture exact per-file facts.

Current read-only device fingerprints (2026-10-19; recorded in
`coordination/artifacts/current-ipad-patch-runtime-fingerprints-20261019b.json`):
- package `com.kdt.macosbooter 0.3.4` is installed;
- CydiaSubstrate fat payload is dpkg-owned and present, SHA256
  `992b70d37024ac453096d9017ab94d6bde7dea8bb702a109636cfe0948abc322`,
  arm64e CDHash `45820e5b4ca974059f3a343c35e82581ec83c8df`;
- installed arm64e libmachook SHA256
  `47f78880f6de1fadc9faabbf79d16ccc56cfba2feb5a45e2155e09c22c2d9432`;
- installed Host executable SHA256
  `d4a0ec516e8a2c729400cf3091416b136eddecff957a5282d5c0ef191315469a`;
- `strings` of installed arm64e libmachook found legacy `CGXSYNTH-INSTALL`
  but no `SENDEROBS`/`sender_observation` strings. Treat as evidence about
  that exact binary, not the second architecture or actual hook activation.

These are identifiers to reconcile, not a priori desired values. The first
fingerprint command used unavailable `sysctl`/`shasum` paths and BSD `ls -T`
semantics; its true exit and limitations are recorded. The corrected command
uses device `sha256sum`, `ldid`, `lipo`, `strings` and `dpkg` where available.

Build the complete package from the current intended source snapshot including
the committed Host/F1 changes and reviewed/passing uncommitted product patches.
Before any device write: run focused route/provider/relocator tests, package
contract, runtime-switch audit, relevant shell/plist checks, and full
`misc/test_*.py` suite when feasible. Build both arm64 and arm64e payloads,
validate the real `.deb` with the artifact contract, and record source/build/
package SHA256, architectures, UUIDs and true command exits. If any product
patch fails review/tests, stop before install, report the exact blocker, and
continue only with the remaining independently safe patches if the package
contract can prove exclusion.

On device do a fresh read-only inventory, capture rollback copies of every
destination that the install will replace, and verify the rootfs/device/build.
Install the verified package using the project postinst path; require provider
CDHashes/trust membership and installed-file hashes to match. Do not change
native iOS Substrate or jailbreak state. If install/postinst/hash/trust fails,
do not launch GUI; preserve evidence and restore only files changed by this
attempt from the captured exact backups.

The existing GUI baseline is `WindowServer=no`, status reports GUI script exit
1, and its cause is not attributed to responsibility/TCC. After successful
install, perform at most one owner-managed GUI start attempt using the existing
macwshostd/control path, with before/after status, process generation, log
delta, and cleanup. Do not loop/restart repeatedly. If GUI reaches ready, test
in order: package/decode identities; provider loaded/permit install; one
bounded sender-observation record-only event with dedicated sentinel; final
composite sequence; actual visible pixels; then exact-window F1 input route.
Reuse existing reopen freshness logic for the final visible-window check. A
failed layer must be reported as failed, never inferred from the next layer.
Leave the diagnostic sentinel removed and verify no owned probe/debugger jobs
remain. Do not claim a fix for the old exit-1 unless fresh evidence links it.

Save one dated deployment report and relay result with source/device hashes,
backup paths, commands/exits, layer-by-layer results, and remaining gaps. Keep
candidate status explicit; do not claim universal acceptance. Stop with a
separate `(handover to codex now)` line.
