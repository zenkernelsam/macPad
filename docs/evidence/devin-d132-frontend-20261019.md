# D132 — installed-vs-pending patch inventory + mount_bindfs packaging trace

Date 2026-10-19. Packet `coordination/_relay/devin-132-frontend-task.md`.
**Read-only.** No device commands, no source/build/package edits, no GUI
action, no Git writes. Device facts from D125/D127 captured artifacts; local
facts from git, both on-disk `.deb` payloads (re-extracted for this audit),
`.theos/obj` hashes, and the share-review overlay.

Panic anchor: the only recorded kernel panic is the Sep-26 ptrauth DA-key
report (python3.9 era); the user-reported reboot was a respring per D128 —
unattributable. Every inventoried change below post-dates both.

## 1. Reconciliation table

| Change / commit | Component | In D127 deb `67d65e32`? | Device presence evidence | Next required proof |
|---|---|---|---|---|
| `3c4d581` provider method hooks + AppInput route contract | libmachook + provider | YES (committed source) | libmachook `42b75cf3`/`e84d992a` installed at `/var/jb` + rootfs (D127 §5); earlier R012-era `47f78880`/`6de9b688` superseded | layer-2 live provider-map witness |
| R012 XPC PAC-signing fix (uncommitted, mac_hooks group A) | libmachook | YES (built from dirty tree) | same installed slices | next generation: tccd/authd/systemstatusd do not die in `_xpc_bootstrap_services` |
| `4bcf20d` F1 global-flag reject on exact-window kinds | `macws_app_input_route.h` consumer (libmachook) | YES (committed 15:51 Oct-18, built in) | inside installed libmachook slices; contract-level only | layer-6: exact-window ConfigureWindow accepted, `-7` on flagged record, `1002` retired on `SLSPostEventRecord` |
| `b717fb9`+`c97e219` provider package payload contract | CydiaSubstrate.framework payload + postinst publish | YES (framework + companions in payload; postinst `671a685f`) | publish JSON `main_sha256 594cfdff` → rootfs `c97acda3`, arm64e CDHash `8864745f`, all four CDHashes live in `jbctl trustcache info` | layer-2 provider map at runtime |
| R008 `8e470ba` lifecycle/readiness/trust package | daemons/scripts/tccd entitlements | YES | package `ii 0.3.4`; tccd `42c388e8` persists (R008 record) | none pending — accepted, since superseded slices replaced |
| Group B unconditional production permit registrations (uncommitted) | libmachook | YES (built from dirty tree) | inside `42b75cf3`/`e84d992a`; **replaces** D035 diag-gated V2 (`aa041efd`/`21f81b5c` → preserved as `.old`) | layer-2: permits actually registered at runtime; SwapEnd hooks fire |
| D043–D048 SENDEROBS (groups C + new TUs `6641d9df`/`a7c8438b` + Makefile `a7cc7862`) | libmachook | YES — `SENDEROBS`/`CGXSYNTH-INSTALL` strings verified in payload and installed copy | marker present in installed rootfs arm64e slice; sentinels all absent → inert | layer-3: bounded record-only event at one armed sentinel |
| `misc/cydiasubstrate_macos.m` relocator `fa6579a2` (uncommitted) | provider | YES (provider rebuilt) | rootfs provider `c97acda3` is the re-signed build product | provider map witness; relocator exercised only via live arm64 path (host test was x86_64 thunk) |
| MacWSHost.app `d4a0ec51` / MacWSWindowing | iOS UI | n/a — no source delta since R012 install | unchanged on device (D125); clean working tree | none — no pending change |
| Later frontend/UI patches | — | — | **none exist**: HEAD `c97e219` is docs; dirty set = exactly the D127 build inputs; MacWSHost/MacWSWindowing trees clean | — |
| Stray/host-only artifacts (`macos_ws_event_probe` binary, probe/test sources, D113–D120 transport, `.theos` stale objects `5b7e3b2d`/`28e85425`) | — | correctly absent (verified 267-entry payload) | n/a | keep excluded; `.theos` local objects predate F1+SENDEROBS — do not deploy |
| `mount_bindfs` helper | host exposure tool | **NO** | absent device-wide, not dpkg-owned, not in either deb | see §2 |

## 2. `mount_bindfs` trace

FACT chain:
- `ensure_jb_usr_bind.sh` requires `$mount_tool=/var/jb/usr/local/bin/
  mount_bindfs` when no accepted exposure exists; exits 1 otherwise (D127's
  abort line).
- **Never in this repo's payload**: `layout/usr/` contains only `macOS/`;
  no `usr/local/`; no subproject builds it; `git ls-tree` across history
  shows no tracked copy. Verified absent from **both** extant debs (D127
  `67d65e32`: 267 entries; debug deb: 262 entries — zero `local/bin`/
  `bindfs` matches; `mountdevfs` ships separately at `usr/macOS/bin/`).
- **Historical copy**: share overlay `/var/jb/usr/local/bin/mount_bindfs`,
  fat arm64+arm64e, sha256 `34c9c30b…`, ldid id
  `mount_bindfs.b703f318.unsigned` — the project's `b703f318` signing
  convention. libSystem-only, `usage: mount_bindfs [-o options] target
  mount_point`. Classic BSD bindfs mounter with the documented behavior:
  on bindfs-rejecting kernels it prints `exposed … via symlink (bind
  unsupported on this kernel)` and creates the exact absolute link
  (runtime-verified iPad14,3 Oct-6, `ipad14-3-…-port-20261006.md:126`).
- **Docs claim it ships**: `install_rootfs_13.sh` (e5c73cd) requires it and
  comments "mount_bindfs ships inside the deb"; install docs describe the
  0.3.4 deb as "含预拆分 libmachook + mount_bindfs". Both claims are stale —
  no extant 0.3.4 deb contains it.
- **Device presence timeline**: missing in one Sep-28 (15.6.1-era) context;
  present Oct-6 inventory; absent Oct-19, `dpkg -S` finds no owner.
- **Disappearance mechanism — THEORY**: a `dpkg -i` upgrade over an older
  deb that owned the file would remove it automatically (same package name;
  files owned by old version absent in new get pruned). The R012-era
  install (Oct-18 ~19:10) is a candidate trigger. Alternative: manual or
  cleanup removal. Unproven — no surviving deb that owned it to diff.
- `mountdevfs/main.c` documents the helper's authority model: bounded
  libjailbreak credential transaction around mount(2) — it is a privileged
  jailbreak helper, not a shell script.

Narrow packaging fix (recommended, preserves the documented exact symlink
fallback + fail-closed):
1. Vendor the fat helper into the payload at `usr/local/bin/mount_bindfs`
   (→ `/var/jb/usr/local/bin/mount_bindfs`). The share-overlay binary
   `34c9c30b` is the only available copy; flag its provenance (no source
   in repo — vendored binary) and let the existing postinst codesign/
   autosignd + trustcache reconcile cover it like every other payload tool.
2. Keep `ensure_jb_usr_bind.sh` unchanged — it already fails closed
   (helper absent + no valid exposure → exit 1) and already accepts the
   helper's documented symlink output.
3. Correct the stale claims (`install_rootfs_13.sh:85`, install docs) so
   "ships inside the deb" becomes true rather than deleting the check.
4. Do NOT instead duplicate symlink creation inside the ensure script —
   that would fork the helper's fallback semantics into two places and
   drift; the script's contract is "helper or valid exposure", which the
   restored payload satisfies.

## 3. Residual unknowns (marked, not inferred)

- Whether the missing helper alone explains all pre-D127 exit-1 runs:
  consistent signature, no captured ERROR lines for the earlier attempts.
- The exact deb that once owned mount_bindfs on this device.
- Whether a live GUI start completes through the symlink exposure (helper
  contract accepts it; nothing downstream witnessed).
- `.theos/obj` remains R012-era (`5b7e3b2d`/`28e85425`, 0 SENDEROBS
  strings) — a naive local `make package` outside the isolated dir could
  emit a pre-F1 deb; the stale-path hazard persists.

(handover to codex now)
