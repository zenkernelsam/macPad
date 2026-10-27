# Backend R010 — merged provider/F1 package and live activation acceptance

Own the single build/deployment/device lane for this packet. Freeze current
candidate HEAD `c97e219` and confirm it contains `3c4d581` (provider method/local
symbol fixes), `4bcf20d` (F1 exact-window/global rejection), and R012 package
commits `b717fb9`/`c97e219`. No source changes or concurrent rebuilds in this
packet. Read `reviews/R012-frontend-R009-backend-20261018.md`, R012 evidence,
R008 device evidence and the exact Ventura reproduction/deploy runbook first.

Inventory and preserve the live baseline before mutation: device/build,
jailbreak/trust tools, rootfs mount/free space, current package/provider
paths/hashes/inodes/CDHashes, active tccd/WindowServer/agent identities,
markers, final-composite sequence/pixels, native health and thermal state.
Keep credentials transient; never print or save them. Stop if the external
generation is user-owned/active in a way that cannot be safely replaced or if
the inventory is incomplete.

Build the full package once from the frozen tree using the repository's
supported pipeline. Preserve actual command exit codes. Verify the resulting
`.deb` payload with `macws_artifact_contract.py`, including provider main and
both thin companions plus publisher scripts. Record source/build/archive
hashes. Before install, verify install effects and confirm no unrelated
rollback, reset, stash or source synchronization.

After install, read back package version, provider bytes/inode, both
architectures, platform/minos/install name, ldid/CDHashes and live trustcache
admission. Verify the published rootfs file is the same provider. Under one
owned WindowServer/application generation, prove the actual loaded provider
path/UUID/hash and capture one call where the repaired hook replaces the
method and invokes the callable original. Exercise the installed F1 route
contract: exact-window kinds with global flag reject; zero window without the
flag rejects; valid global pointer/system gesture still classifies as global.
If no production route can safely trigger these cases, report the exact
missing runtime witness instead of claiming activation from `otool` alone.

Then perform only the bounded existing startup/health check. Do not broaden
this packet into TCC policy changes, input acceptance or display repair. Keep
separate results for process readiness, actual loaded provider, final-composite
sequence/pixels, TCC reply and accepted input. Do not restart shared compiler,
native tccd, SpringBoard or backboardd. No assertion bypass, constant-true TCC,
fake completion or broad entitlement/ownership changes. If the package changes
the WindowServer generation, use the owned-generation procedure and cleanly
stop only what this packet started.

Record exact identities, paths, hashes, commands/exits, before/after state,
crash deltas, thermal/native health, cleanup, failures and limits. Write only
your dated evidence/artifact and `coordination/_relay/round-010-backend-result.md`;
do not commit unrelated WIP. Finish only after cleanup and end the result with
the exact last line `(handover to codex now)`.
