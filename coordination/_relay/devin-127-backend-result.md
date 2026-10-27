# D127 backend result — build/install missing source-only patches

Date: 2026-10-19 · Device: iPad13,11 T8103 iPadOS 16.3 (20D47) · Evidence:
`docs/evidence/devin-d127-backend-20261019.md` · Candidate: **PENDING**
(layers 2–6 unreached — GUI aborted before chroot launch)

## What was built

- HEAD `c97e219`; dirty set: `mac_hooks.m` `7321d92c` (Groups A=R012 XPC
  PAC fix, B=unconditional production permit registrations, C=D043–D048
  sender-observation), `Makefile` `a7cc7862`, `MacWSSenderObservation.c/h`
  `6641d9df`/`a7c8438b`, provider `cydiasubstrate_macos.m` `fa6579a2`.
- Tests: 59 python pass / 13 skip (no host dpkg-deb), sender-obs host
  `ALL PASS`, relocator `ALL PASS` (x86 thunk path).
- Rootless deb `/tmp/d127-build/packages/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb`
  sha256 `67d65e32238119b37d965c31e9037ce3953eb29d01af123952718f27b5962135`
  (5,896,774 B). First build was rootful → rejected; lzma shim first emitted
  xz → device rejected; corrected to LZMA-Alone (`5d00` magic) and repackaged.
- Payload verified: 180 files, provider framework + companions, SENDEROBS
  strings in libmachook, postinst `671a685f` (R012 publish wiring),
  MacWSWindowing arm64e `__cfstring` auth-bind fixups identical to the
  validated artifact; no offline-prototype/probe leakage.
- The stale local `.deb` `8601d8ff` was not installed (D125 finding).

## Install — layer 1 PASS

- Backups at `/var/mobile/macws_d127_backup/` (usr_macOS, Applications,
  MobileSubstrate, rootfs_lib=D035 pair, rootfs_provider). Sentinels absent.
- `dpkg -i` → `DPKG_RC=0`. Postinst thin-sliced libmachook, published
  provider, trust reconcile `already-trusted` (248 files/images), synced
  rootfs, preserved D035 copies as `*.dylib.old`.
- Device identities: rootfs libmachook arm64e `42b75cf3` (cdhash `a08fdb50`),
  arm64 `e84d992a` (`74d95de2`); rootfs provider `c97acda3` (`2e19270d`,
  arm64e `8864745f`). All four CDHashes live in `jbctl trustcache info`.
  `SENDEROBS` marker present in installed copy. `dpkg -l` → `ii 0.3.4`.

## GUI start — one bounded attempt, FAILED pre-chroot

`macos_gui.sh start coexist --no-vnc` → `MacWS: mount_bindfs is unavailable`
→ cleanup → stopped. Never reached WindowServer.

Root cause (evidence-backed): `/var/jb/usr/local/bin/mount_bindfs` is absent
device-wide, not dpkg-owned, and not in this package's payload — a
**pre-existing infrastructure gap** matching the D125 baseline
(`windowserver=no`, exit 1). `ensure_jb_usr_bind.sh` accepts either a live
bind or the exact absolute symlink `/var/mnt/rootfs/var/jb/usr → /var/jb/usr`;
my attempt's cleanup destroyed the prior exposure, which I restored as the
documented symlink → `ensure_jb_usr_bind.sh` now exits 0. No second attempt.

## Layers 2–6 — all NOT REACHED

No provider-map/permit witness, no SENDEROBS event, no composite sequence,
no pixels, no F1 live acceptance. Live `1002` remains an open separate
route. No claims from process liveness.

## Cleanup / leftovers

- No stray GUI/watchdog/probe processes; no diagnostic or obs sentinels;
  no new crash reports; uploaded deb removed; backup retained.
- Next blocker to resolve before a further start attempt: a real
  `mount_bindfs` helper at `/var/jb/usr/local/bin/mount_bindfs` (or package
  the symlink-publishing equivalent the script documents for
  bindfs-rejecting kernels).

(handover to codex now)
