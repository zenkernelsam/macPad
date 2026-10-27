# D127 — Build/install of the verified missing source-only patches

Date: 2026-10-19. Device: iPad13,11 / T8103 (M1), iPadOS 16.3 (20D47), Dopamine
rootless. Scope: build + install D043–D048 sender-observation integration and
the dirty provider relocator (`misc/cydiasubstrate_macos.m`) while preserving
the R012 package baseline, F1 (`4bcf20d`), provider/AppInput (`3c4d581`), R012
XPC PAC-signing, and D035 V2 runtime behavior. One bounded GUI start attempt.

## 1. Frozen source identities (pre-build)

- HEAD: `c97e219f3f367662dc9bdb1f20d7e76fba544df1` (`c97e219`, clean except the
  intended dirty set)
- `libmachook/mac_hooks.m` `7321d92c530368b737e9b149a2548d524f6e67b4`
  (working tree) — one build unit, three logical groups:
  - Group A: R012 XPC PAC-signing fix (uncommitted, load-bearing — same file
    D125 flagged as also carrying the R010-era pointer fix)
  - Group B: unconditional production permit registrations (repairs the
    D035-era gap where registrations sat behind diagnostic markers → zero
    production permits / zero SwapEnd hooks)
  - Group C: D043–D048 sender-observation call sites (default-off,
    record-only)
- `libmachook/MacWSSenderObservation.c` `6641d9dfdf23d7273b61325a96018dbaa0718d4a`
- `libmachook/MacWSSenderObservation.h` `a7c8438b3b509ff6199d05a7499f201e228ab633`
- `libmachook/Makefile` `a7cc7862def3aa7eb9eca3df2187f2bd2d2fc447` (adds the
  observation TUs)
- `misc/cydiasubstrate_macos.m` `fa6579a2fcbd1f2f9ace00aad8895821a466da55`
  (dirty relocator changes)
- Diff hashes: `mac_hooks.m` `b9740786…`, `Makefile` `4959c23e…`,
  `cydiasubstrate_macos.m` `010da818…`

## 2. Pre-install verification (host, offline)

- Focused `misc/test_*.py`: **59 passed, 13 skipped** (host lacks `dpkg-deb` at
  run time; skips are the deb-dependent cases).
- Sender-observation host C tests: **ALL PASS (failures=0)** — default-off,
  bounded writes, single-fd publication, nested TLS correlation, errno
  envelope, prologue pins, global offsets.
- Relocator host test: **ALL PASS** on x86_64 with `-DMACWS_RELOC_TESTONLY`
  thunk path (live arm64 relocation is not executable on this host; arm64
  behavior is covered by the on-device permit path).
- Runtime-switch audit + shell/plist syntax: clean.
- The stale local `.deb` (`8601d8ff`, Oct 18 14:26) was **not** used — it
  predates `4bcf20d`/`b717fb9` (D125 finding).

## 3. Build

- Isolated dir `/tmp/d127-build`, `THEOS_PACKAGE_SCHEME=rootless`,
  `FINALPACKAGE=1`, `STRIP=0`, lzma shimmed (host lacks `lzma(1)`).
- First attempt produced a rootful layout (`usr/…` bare paths) — **rejected
  and rebuilt** with the rootless scheme. Correct archive:
  `packages/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb` (final:
  sha256 `67d65e32238119b37d965c31e9037ce3953eb29d01af123952718f27b5962135`,
  5,896,774 B).
- Payload verification (extracted): 180 files, `var/jb/…` layout, provider
  framework + arm64/arm64e companions, libmachook fat staged under both
  dependency names (postinst thin-slices on device), `SENDEROBS`/
  `CGXSYNTH-INSTALL` strings present in libmachook, postinst = committed
  `671a685f` (R012-era publish wiring, `macws_publish_substrate.sh` at
  line 488), no `analysis/` / D113–D120 transport / untracked probe files.
  The two `*probe` payloads (`macwstccdprobe`, `macws_control_probe`) are the
  package's production tools (present in the installed R012-era package).
- MacWSWindowing arm64e fixup audit vs the R008-validated `d416dffe`:
  `dyld_info -fixups` shows identical `__cfstring` **auth-bind** records
  (key=DA, ad=1) at the same offsets — the fresh Theos build is not the
  unsafe plain-bind variant.
- First device install attempt failed: the python lzma shim emitted
  `FORMAT_XZ`, device `dpkg-deb` requires legacy `.lzma` (LZMA-Alone).
  Device state verified intact after the failure (`ii`, old hashes).
  Shim corrected to `FORMAT_ALONE` (`5d00` magic), repackaged.

## 4. Device pre-state and backups

- Pre-install inventory (read-only): package `ii 0.3.4`, R012-era `/var/jb`
  payloads (`47f78880`/`6de9b688` libmachook, `992b70d3` provider,
  `d4a0ec51` MacWSHost), D035 out-of-package rootfs libmachook
  (`aa041efd`/`21f81b5c`), GUI `windowserver=no` (pre-existing exit-1).
- Exact rollback copies at `/var/mobile/macws_d127_backup/`:
  `usr_macOS`, `Applications`, `MobileSubstrate/DynamicLibraries`,
  `rootfs_lib` (D035 libmachook pair), `rootfs_provider`.
- All four diagnostic/sender-observation sentinels absent before any write.

## 5. Install result — PASS (layer 1)

`dpkg -i` → `DPKG_RC=0`. Postinst: LC_BUILD_VERSION→macOS 13.0 on both
libmachook slices, thin-split, provider publish JSON (`main_sha256
594cfdff` → rootfs `c97acda3`), autosignd+codesign verified, weather plist
prepared, SkyLight WindowServer verified, thin launchservicesd loader
installed, trust reconcile `backend=already-trusted` (248 files/248 images),
rootfs runtime synced, GUI jobs preserved stopped.

Installed identities (verified on device + pulled back to host):

| File | SHA-256 | Arch | CDHash (sha256) |
|---|---|---|---|
| `/var/jb/usr/macOS/lib/libmachook.dylib` | `42b75cf3…` | arm64e | `a08fdb50…` |
| `/var/jb/usr/macOS/lib/libmachook_arm64.dylib` | `e84d992a…` | arm64 | `74d95de2…` |
| `/var/mnt/rootfs/usr/local/lib/libmachook.dylib` | `42b75cf3…` | arm64e | `a08fdb50…` |
| `/var/mnt/rootfs/usr/local/lib/libmachook_arm64.dylib` | `e84d992a…` | arm64 | `74d95de2…` |
| `/var/mnt/rootfs/…/CydiaSubstrate` | `c97acda3…` | arm64 | `2e19270d…` |
| provider arm64e slice cdhash | — | arm64e | `8864745f…` |

- `jbctl trustcache info`: all four CDHashes live (2 entries each — the
  publish registers both slices).
- `SENDEROBS` marker present in the installed rootfs arm64e slice → the new
  D043–D048 code is in the runtime copy.
- Rootfs `/usr/local/lib/libmachook{,_arm64}.dylib.old` = the Oct-14 D035
  copies, preserved in place by postinst. `/var/mnt/rootfs/var/jb/usr`
  exposure contract intact (see §7).
- Package state `ii com.kdt.macosbooter 0.3.4`.

## 6. The one bounded GUI start attempt — FAILED before chroot launch

Command: `timeout 300 bash /var/jb/usr/macOS/bin/macos_gui.sh start coexist
--no-vnc` (owner-managed path; baseline first: no GUI processes, hostd/
keychaind/allocd loaded only).

Sequence: windowing bridge ready → contracts → cleanup → METAL-CACHE current
→ NATIVE-AGX preflight → watchdog armed (pid 16432) →
**`MacWS: mount_bindfs is unavailable`** → cleanup → "Stopped. The iPad is
back on iOS."

### Root cause (evidence-backed)

- `ensure_jb_usr_bind.sh` (installed, also `layout/usr/macOS/bin/…`) emits
  the message at line 71 when `/var/jb/usr/local/bin/mount_bindfs` is not
  executable and no accepted exposure exists.
- `mount_bindfs` is **absent device-wide** (`find /` outside the rootfs → no
  match), **not dpkg-owned** (`dpkg -S` / `*.list` → no match), and **not in
  this package's payload** (nor in the D127 deb). `/var/jb/usr/local/bin/`
  contains only old probes. The script comment calls it "the packaged
  mount_bindfs helper", but no package on this device ships it.
- At start time `/var/mnt/rootfs/var/jb/usr` was an empty real dir (no
  symlink, no live bind — `mount | grep mnt` → 0), so ensure reached the
  helper check and exited 1 → `macos_gui.sh` ran `stop_all` and exited.
- This is a **pre-existing infrastructure gap, not a D127 regression**: the
  D125-era baseline already had `windowserver=no` with GUI exit 1, and the
  missing helper produces exactly this abort before any chroot/WindowServer
  work.
- Postinst had passed `ensure_jb_usr_bind.sh || exit 1` during install, so a
  valid exposure (live bind or the exact symlink) existed at that moment;
  the start attempt's own cleanup tore it down before ensure re-ran. The
  helper's documented behavior on bindfs-rejecting kernels (this T8103 is
  one) is to publish `/var/jb/usr` as an **absolute symlink** — ensure
  accepts exactly that link form.

### Post-attempt state restoration

My start attempt destroyed the pre-existing exposure. Restored it in the
helper's documented form: `rmdir` the empty dir, `ln -s /var/jb/usr
/var/mnt/rootfs/var/jb/usr`; `ensure_jb_usr_bind.sh` → **exit 0**. No second
start was attempted (task bound: one attempt).

## 7. Layered acceptance status

| Layer | Result |
|---|---|
| 1. Package/hash/decode | **PASS** — `dpkg rc=0`, rootless payload, thin slices, four CDHashes live in trustcache, provider published, postinst `671a685f`, SENDEROBS present in installed copy |
| 2. Provider mapped / permit install | **NOT REACHED** — start aborted pre-chroot; no live permit or provider-map witness exists |
| 3. Bounded SENDEROBS record-only event | **NOT REACHED** — sentinel never armed (no runtime to observe); sentinels remain absent |
| 4. Final-composite sequence | **NOT REACHED** — WindowServer never launched |
| 5. Inspected visible pixels | **NOT REACHED** |
| 6. Exact-window F1 acceptance | **NOT REACHED** — no reopen/witness possible; live `1002` remains an open separate route |

## 8. Cleanup / leftovers

- Watchdog (16432) and all start-attempt processes: gone (`ps` clean; only
  system `watchdogd` remains — unrelated iOS daemon).
- No diagnostic or sender-observation sentinels exist on device (all four
  paths absent, checked after install and after the attempt).
- Uploaded deb removed from `/var/mobile`; backup tree retained at
  `/var/mobile/macws_d127_backup/`.
- GUI status post-attempt: `(none running)`, VNC not running — same as the
  pre-attempt baseline.
- Pre-existing debris noted (not mine, left in place):
  `/var/mnt/rootfs/tmp/libmachook_new.dylib` (Oct 11), old
  `macws_catalyst_child.*.info` files, `macws_audio_ring`.
- No new crash reports.

## 9. Gaps and blockers

1. **`mount_bindfs` helper is missing on this device** — the only current
   start blocker. It is referenced by `ensure_jb_usr_bind.sh` as "the
   packaged helper" but is absent from the package payload, from dpkg, and
   from the filesystem. On bindfs-rejecting kernels it only needs to create
   the accepted absolute symlink (`ln -s /var/jb/usr`), which now exists by
   hand. Whether a live GUI start can complete through the symlink form is
   unproven (ensure accepts it; nothing downstream has been witnessed).
2. Layers 2–6 all lack runtime evidence; candidate stays **PENDING**.
3. The provider's publisher identifier remains the known limitation noted
   in prior rounds (temp/ldid identifiers).
4. `/var/mnt/rootfs/usr/local/lib/libmachook_arm64e.dylib` (`c408dc94`,
   Oct 14) is a separate arm64e copy not replaced by this install — likely
   the launchservicesd loader target; left as-is.
