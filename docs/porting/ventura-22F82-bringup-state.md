# Ventura 13.4.1 / 22F82 bring-up — working state (2026-10-06)

> Ground-truth file for the macOS 13 Ventura branch. Keep it updated at every
> milestone before moving on. Evidence labels: `runtime-confirmed` /
> `RE-confirmed` / `source-confirmed` / `THEORY`.

## 0. Target and first milestone (source: user decision 2026-10-06)

- Guest userspace: **macOS 13.4.1 / 22F82** (UniversalMac restore IPSW).
  22F82 is the newest build inside the author-validated Ventura family
  (13.4/22F66 and 13.4.1/22F82 share one cache CDHash pair in
  `postinst.sh`/`macos_gui.sh`). macOS 13's true last release is 13.7.x
  but is NOT in the validated family — not the target.
- First milestone (unchanged):
  `/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI` printing `HI`
  through the real 22F82 dyld + real shared cache.
  A disk-shim `HI`, shim libSystem, process survival, installer rc=0, or
  trust-helper rc=0 do NOT count.
- Historical note (`CLI-MILESTONE-2026-09-28.md`): the earlier
  `echo HELLO`/`cat`/`sh` passes used a **disk libSystem shim**; syscall 536
  was failing with error 40 at that time. Those are shim witnesses, not
  cache witnesses.

## 1. Device baseline (runtime-confirmed, read-only, 2026-10-06)

- iPad13,11 / iPadOS 16.3 / 20D47, Dopamine rootless, 1.1 TiB free on
  /private/var.
- `/var/mnt/` holds only empty `r2/` — no rootfs, no `rootfs-13.4.bak`.
- `com.kdt.macosbooter 0.3.4` package intact: `/var/jb/usr/macOS`
  complete, all launch plists present, jbctl/python3/ldid/mount_bindfs/
  uicache/chroot/bash present.
- Running: `macwskeychaind`, `MacWSHost` app; `alloc`/`hostd` jobs loaded
  but not running. No chroot/WindowServer/15.6 runner remnants.
- Live trustcache: 188 entries, none of the 4 known macOS cache CDHashes
  admitted this boot.
- Residuals archived to `analysis/device-leftovers-20261006/` (local only,
  untracked): `dscq/` (217 M iOS DSC dylib extraction, re-derivable),
  `macws-runtime-stage/` (428 K), `__pycache__/cdhash.pyc`,
  `macwsallocd.{err,out}`, `/var/jb/var/mobile/MacWSBootingGuide/` (428 K
  partial repo). Inventory evidence:
  `docs/evidence/ventura-handover-device-inventory-20261006.md`.
- `/var/mobile/k*.py` kernel RE scripts are already absent.

## 2. Assets (source-confirmed)

### 22F82 IPSW — downloading

```text
URL      https://updates.cdn-apple.com/2023SpringFCS/fullrestores/042-01877/2F49A9FE-7033-41D0-9D0C-64EFCE6B4C22/UniversalMac_13.4.1_22F82_Restore.ipsw
sha256   5ac144d1661614806d765bc0466d719152e2594c2db3888f1ac02276f5638e98   (appledb)
size     ~12.5 GB (22F66 entry listed 12,739,995,153 B; verify after fetch)
local    VirtualMacOniPad/VirtualMac/build/downloads/UniversalMac_13.4.1_22F82_Restore.ipsw.part
method   curl -C - (resumable); rename .part -> .ipsw only after sha256 match
devices  includes MacBookAir10,1 / VirtualMac2,1 (appledb deviceMap)
```

### 22D68 assets — retired to Trash (recoverable)

User ordered 13.2.1 removed on 2026-10-06 after choosing 13.4.x:

```text
~/.Trash/UniversalMac_13.2.1_22D68_Restore.ipsw        (12.49 GB)
~/.Trash/22D68__MacOS-20261006/                        (extracted cache + .a2s + payload frameworks)
~/.Trash/22D68__MacBookAir10,1/                        (kernelcache only)
```

Caveat recorded: VirtualMac's shipping payload was built from 22D68;
rebuilding the VM inputs would need these back.

## 3. What the existing package already gives us (source-confirmed)

- **Trust contract already supports 22F82.**
  `postinst.sh:1066` and `macos_gui.sh restore_cold_boot_trust:1686` map
  `22F82|22F66|""` to the Ventura cache pair
  `b5da39409492ac85e5a8e8ab618fe77e2d7a2980` +
  `bbb765988e2677b98d47a549d612fa0d4af25f69`. No code change needed for
  trust admission — only runtime verification after install.
- **Cache path convention**: package expects cache at
  `$ROOTFS/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/
  dyld_shared_cache_arm64e{,.01}` — runtime-confirmed for Ventura on
  iPad13,11/iPadOS 16.4.1 (`layout/DEBIAN/postinst:47-62`,
  `normalize_restored_runtime_metadata`).
- **Loader closure**: `postinst.sh` admits
  `/var/mnt/rootfs/usr/lib/dyld` (the real dyld, expected as a file inside
  rootfs) plus chroot `CydiaSubstrate`.
- **`macos_gui.sh trust`** may pause on `thermal-state=serious`
  (FROZEN doc); plan around it for device runs.
- **`misc/cdhash_slices.py` / `misc/cdhash.py`** exist for computing cache
  CDHashes if the hardcoded pair needs verification.

## 4. Open questions before first exec (marked, not assumed)

| # | Question | Status |
|---|---|---|
| Q1 | Is the author's 13.4 dyld **stock** or **patched**, and with what? DEBIAN/postinst:84 says the chroot executes "patched macOS dyld/shared-cache images". `postinst.sh` does not patch dyld — so the patch must live inside the rootfs image itself. Fresh 22F82 rootfs will ship **stock** dyld. | OPEN — inspect zhuowei tool lineage + repo history; test stock first |
| Q2 | Old `syscall 536 → 40` failure: RE-confirmed producer is Sandbox `mpo_file_check_mmap` (`cred_sb_evaluate` of `file-map-executable`) firing on a vnode **lacking VSHAREDCACHE**. Was that failure specific to the 15.6 cache/layout, or will a legit 22F82 cryptex cache carry the flag and pass? | OPEN — likely non-issue for a proper cryptex-placed cache; verify at runtime |
| Q3 | The 15.6 PMAP-CS nested-owner bug (frozen branch root cause) — does it also bite a Ventura cache mapped through the **systemwide** 536 path? THEORY: no — 536 registers the cache AS the shared region rather than private-mapping inside the iOS SR submap; the author's 13.4 devices ran it. | THEORY — runtime verdict at first exec |
| Q4 | 22F82 IPSW OS volume contains no on-disk `libSystem.B.dylib` (macOS 11+ design; §19.6 for 22D68, same family). For `echo`, dyld resolves libSystem from the cache — no disk stubs needed. | THEORY — confirm with `DYLD_PRINT_LIBRARIES` |
| Q5 | `install_rootfs_15.sh` is 15.6-shaped (build checks `24G90`, arm64ify targets, swap logic using `rootfs-13.4.bak`). Needs a version-clean `install-rootfs-13.sh` — never reuse the 15.6 script as-is. | TODO |
| Q6 | `usr/lib/systemhook.dylib` inside rootfs is trust-listed by `postinst.sh:1467` but **absent on this device** (`/var/jb/usr/lib/` has no `systemhook.dylib`; ElleKit uses `libhooker.dylib`/`libellekit.dylib`/`libinjector.dylib` instead). Old rootfs backups that used to carry it are deleted. | OPEN — candidate: harvest `libhooker.dylib` under a documented name, or source the original systemhook build; only needed for the chroot tweak-injection chain, NOT for the echo milestone |

## 4.1 Injection sources on device (runtime-confirmed 2026-10-06)

```text
OK   /var/jb/usr/lib/TweakLoader.dylib
OK   /var/jb/Library/Frameworks/CydiaSubstrate.framework
OK   /var/jb/usr/lib/ellekit/{pspawn,MobileSafety,libinjector}.dylib
OK   /var/jb/usr/lib/{libsubstrate,libhooker,libellekit,TweakInject}.dylib
OK   /var/jb/usr/lib/TweakInject/{MacWSCatalystLaunch,MacWSWindowing,
      MTLCompilerBypassOSCheck,VZKeyboardPassthrough}.dylib (+ plists)
MISS /var/jb/usr/lib/systemhook.dylib
MISS /var/jb/usr/lib/CydiaSubstrate.framework
MISS /var/jb/var/mobile/theos
```

`launchdchrootexec` injects `DYLD_INSERT_LIBRARIES=libmachook` itself
(`main.m:61-72`), so the echo milestone does not depend on the chroot
tweak-injection chain. The `systemhook`/`TweakLoader`/`CydiaSubstrate`
closure is required later by `postinst.sh:1463-1467` and chroot-side tweaks.

## 5. Execution plan (current)

```text
A ✅ device/host inventory + residual archive   (evidence committed 6cba769)
B host staging (no device touch):
  B1 finish 22F82 download -> sha256 verify -> rename .ipsw
  B2 ipsw mount fs -> inventory OS vol; ipsw mount sys -> cryptex cache
  B3 build-rootfs-13.4.sh: OS vol {System,usr,bin,sbin} +
     Templates/Data skeleton + cryptex cache -> dyld dir +
     /System/Volumes/Data, /etc->private/etc, /tmp->private/tmp,
     /var->private/var symlinks + SystemVersion.plist check ==22F82
  B4 host-verify /bin/echo dep closure + dyld identity/UUID/CDHash pair
     vs postinst constants (must equal b5da3940…/bbb76598…)
C device install:
  C1 write misc/install-rootfs-13.sh (version-clean; fresh-inode rules;
     keep old rootfs backup semantics)
  C2 content-verified transfer (rsync --partial --append-verify)
  C3 postinst.sh + trust verify (jbctl trustcache info membership)
D bounded echo milestone, capture stdout/stderr/rc/DYLD_PRINT_LIBRARIES/
  crash reports; only then cat/sh/WindowServer.
```

## 6. Hard rules carried forward

- No kernel/PAC/PTE writes; no `fmt13_patch.py`; no 15.6 patch carry-over.
- No NOP/forced-branch/constant-return/zero-blob "fixes".
- `jbctl trustcache add` can no-op silently — always re-check `info`.
- `cp` over an existing signed file keeps stale inode → `rm` first / new
  inode (AMFI Invalid Page lesson).
- IDA Pro MCP for binary RE; verify `server_health` input_path/imagebase
  before citing addresses.
- Every commit → immediate `git push origin main`.
- Apple payloads (rootfs/IPSW/cache/archives) never enter git.
