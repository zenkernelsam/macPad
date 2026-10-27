# Review: Frontend R012 and Backend R009

Date: 2026-10-18 Asia/Hong_Kong. Candidate package commits: `b717fb9` and
`c97e219`; source base recorded by frontend: `7e5d771`. The active tree also
contains `3c4d581` provider-hook and `4bcf20d` F1 route fixes; both are
ancestors of current HEAD `c97e219`.

## Frontend R012 — approved with limits

The package work stages the repaired macOS provider under the package-owned
`/var/jb/usr/macOS` path, publishes it into the mounted macOS rootfs, checks
the exact arm64/arm64e macOS 13 Mach-O contract and install name, and fails
closed on missing slices or trust admission. The native iOS Substrate path is
only trust-registered, not written. Focused tests independently pass:
`python3 -m unittest -v misc.test_substrate_package_contract misc.test_artifact_contract`
reported 54 tests OK, 13 skipped because this host lacks `dpkg-deb`.

Approval is limited to source/package structure and host tests. No real `.deb`
payload extraction, device install, loaded provider identity, hook/original
call behavior, F1 runtime behavior, final-composite pixels or input acceptance
is established. The host publish test uses fake `jbctl`, so its hashes and
trust witness are not device identities. These gaps are the next backend
packet.

## Backend R009 — accepted as a read-only feasibility result

The report distinguishes structural support for a private endpoint from its
unverified first launch. It also correctly says the existing type-14 offline
replay harness cannot consume the full type-15 F11H request, and that the
available crash report is correlated by code path/coalition rather than exact
request bytes. It did not touch shared compiler or WindowServer generations.

No server-side F11H cause is established yet. Do not change UID, entitlement,
MPS assertion behavior, or shared compiler state from this result. The private
launch/request capture can be scheduled separately after the current merged
provider acceptance.

## Next acceptance boundary

Run one frozen merged-tree full-package transaction and read back the actual
published and loaded provider. Verify the repaired hook replacement and
callable-original path, and exercise the F1 exact-window/global rejection
cases from the installed image. Keep package build, deployment, process
generation and source fingerprint tied together. Final-composite pixels,
TCC reply and input acceptance remain separate endpoints and are not implied
by successful provider activation.
