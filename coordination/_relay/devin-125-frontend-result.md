# devin-125 frontend result — installed-vs-pending UI/F1 inventory

Read-only audit complete; evidence doc:
`docs/evidence/devin-d125-frontend-20261019.md`. No device commands, no
source edits, no builds, no Git/runtime changes.

## Key corrections to assumptions

- **Installed package is the R012-era dpkg install**, not R008 and not the
  local `.deb`. Device sha256s (Oct-19 read-only capture): `/var/jb`
  libmachook `47f78880` arm64e / `6de9b688` arm64, MacWSHost `d4a0ec51`
  thin arm64, provider framework `992b70d3`+thin companions, rootfs
  provider `b2ae21da`.
- **The local `.deb` `8601d8ff` is a stale pre-F1/pre-provider
  intermediate** — payloads byte-match `.theos/obj` builds stamped Oct 18
  13:09–14:19; `4bcf20d` (15:51) and `b717fb9` (17:39) both postdate the
  build. It contains no `CydiaSubstrate.framework` and carries the R008
  postinst. **D124 must not install it.**
- **Rootfs runtime libmachook `aa041efd`/`21f81b5c` ≠ package copies** —
  out-of-package D035 narrow-permit V2 scp deploy (Oct 18 23:53).
- **D124 manifest omission**: the dirty `mac_hooks.m` delta also carries
  the uncommitted R012 xpc-pointer patch (14+/1-) that fixed the R010 PAC
  crash — it is load-bearing, not part of D043–D048.

## Classification

- **Installed (verified)**: `3c4d581`, `4bcf20d`, `b717fb9`/`c97e219`
  provider payload, R012 xpc patch, R008 package (superseded but recorded).
- **Installed separately**: D035 V2 rootfs libmachook.
- **Source-only/unbuilt**: D043–D048 obs (dirty `mac_hooks.m`+Makefile+
  untracked `MacWSSenderObservation.{c,h}`, = D048-frozen hashes), dirty
  `cydiasubstrate_macos.m` `fa6579a2`, untracked test/probe files incl.
  compiled `misc/macos_ws_event_probe` + `tmpdir-lc/`.
- **Documentation/offline-only**: D113/D120 transport prototype — verified
  absent from the `.deb` and `libmachook/Makefile`.

## R003/F1 audit

Broker (`macwsinputd`) can stamp `GlobalSystemSurface` on resolved
records → F1 is load-bearing. Post-F1 classify is present in both
installed libmachook slice identities (R010/R012 build records + R010
on-device F1 contract probe PASS). MacWSHost.app `d4a0ec51` is thin
arm64 — its only shipped arch, unchanged by pending commits. **F1 was
never exercised end-to-end** — last real generation still returned input
`1002`, no exact-window witness.

## Exact-window acceptance witness (§5 of evidence doc)

Nonzero-window `ConfigureWindow` → `APP-CONTROL … sent=YES` +
`route=ExactWindow` + non-nil `windowWithWindowNumber:` + applied
geometry; negative control with global flag → `-7`; freshness via the
existing `reconnectGeneration`/reopen postcondition only.

Live posture: WS `state=missing`, GUI exit-1 unattributed, zero SwapEnd
hooks (V2 permits diag-marker-gated). Candidate remains **PENDING**.

(handover to codex now)
