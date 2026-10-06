# Ventura handover — device + host read-only inventory (2026-10-06)

Evidence level: `runtime-confirmed` (device SSH read-only) and
`source-confirmed` (host filesystem). No device mutation was performed.

## 1. Device identity (SSH root@<device>:2222, read-only)

```text
hw.machine: iPad13,11
kern.osversion: 20D47
df /private/var: 1.8Ti size, 754Gi used, 1.1Ti avail
dpkg: com.kdt.macosbooter 0.3.4 (installed)
dpkg: com.mac.virtual 2:1.2.3+105.forceidentity (installed)
```

## 2. Rootfs state

```text
/var/mnt: only `r2/` (empty dir, dated Sep 27) — NO rootfs, NO rootfs-13.4.bak
```

15.6.1/24G90 rootfs confirmed absent, matching
`HANDOVER-MACOS13-VENTURA-2026-10-04.md` §2. The historical
`install_rootfs_15.sh` `BAK=/var/mnt/rootfs-13.4.bak` backup is also gone.

## 3. MacWS runtime still installed (intentionally kept)

- `/var/jb/usr/macOS/` intact: bin, lib, libexec, Frameworks,
  PrivateFrameworks, LaunchDaemons, gui-launchd, share, retired-launch-jobs.
- `/var/jb/usr/macOS/LaunchDaemons/`: 15 plists (WindowServer,
  launchservicesd, sharedfilelistd, systemstatusd, uikitsystemapp +
  10 `com.macwsguide.*` jobs).
- `/var/jb/Library/LaunchDaemons/`: `com.macwsguide.alloc.plist`,
  `com.macwsguide.hostd.plist`, `com.macwsguide.keychain.plist`.
- Tools present: `jbctl`, `python3`, `ldid`, `mount_bindfs`, `uicache`,
  `chroot`, `bash` (all under `/var/jb/usr/bin` / `usr/local/bin`).
- Running now: `macwskeychaind` (pid 385) and the `MacWSHost` iOS app;
  `com.macwsguide.alloc`/`.hostd` jobs loaded but not running.

## 4. Live trustcache (cold state, this boot)

```text
jbctl trustcache info: Jailbreak Trustcache 0 <UUID: 25EB9AFEE5094172ACE27B2FC7BDBB69> (length: 188)
grep -icE 'b5da3940|bbb76598|2b9cccd5|8c7ba7e5' -> 0
```

None of the known Ventura/24G90 shared-cache CDHashes are admitted this
boot — consistent with a clean post-reboot baseline.

## 5. Residual files found outside the installed package

| Path | Size | Identification | Action |
|---|---|---|---|
| `/var/mobile/dscq/` | 217 M, 387 files | Extracted **iOS** shared-cache dylibs (libFDR, libssl.48, libNFC_HAL, libAudioDSPCore, …), created Sep 28 for iOS-side RE comparison. Re-derivable from the device DSC. | Archived to `analysis/device-leftovers-20261006/var-mobile/dscq/` (local only; Apple binaries — NOT for git). |
| `/var/mobile/macws-runtime-stage/` | 428 K, 12 files | macPad runtime staging: `misc/` (metal2metal*, steam/vscode plists) + `layout/usr/macOS`. | Archived. |
| `/var/mobile/__pycache__/cdhash.cpython-39.pyc` | 1.8 K | Byte-compiled `misc/cdhash.py` left by an on-device run, Sep 29. | Archived. |
| `/var/mobile/macwsallocd.{err,out}` | 167 B / 0 B | macwsallocd launch log Oct 5 21:10 — benign startup lines. | Archived. |
| `/var/jb/var/mobile/MacWSBootingGuide/` | 428 K | Partial on-device repo (only `layout/` + `misc/`). | Archived. |
| `/var/mnt/r2/` | empty | Old mount-point directory, Sep 27. | Nothing to archive; documented. |

The AGENTS.md-referenced kernel RE scripts
(`/var/mobile/{kscan_sig,kpatch_c2,kc2check,kptr}.py`) are **already absent**.
No 15.6 runners, chroot processes, WindowServer or `run_dbg` remnants.

## 6. Host-side Ventura assets

```text
VirtualMac/build/downloads/UniversalMac_13.2.1_22D68_Restore.ipsw   12,494,476,408 B
VirtualMac/build/downloads/UniversalMac_11.6_20G165_Restore.ipsw    13,957,005,940 B
VirtualMac/build/inputs/macos/22D68__MacOS/
  dyld_shared_cache_arm64e      1,600,389,120 B
  dyld_shared_cache_arm64e.01   1,719,320,576 B
  .map (936,269 B) / .a2s (713,357,412 B)
  System/, usr/                 VirtualMac payload frameworks only (NOT a rootfs)
22D68__MacBookAir10,1/          kernelcache only (90 M)
VirtualMac/build/toolchain/bin/ ipsw, ipsw-a2sb
```

No 22F66/22F82 rootfs or 13.4 IPSW exists on this host. Device has only
`Sequoia.bundle` (macOS 15 VM) — no installed Ventura VM to rsync from.

## 7. Trust-contract source state (this repo, verified)

- `layout/usr/macOS/bin/macos_gui.sh` `restore_cold_boot_trust`:
  `22F82|22F66|""` → Ventura pair; `24G90` → 24G90 pair; `*` →
  `log ERROR … return 1` (fail closed). 22D68 currently hits `*`.
- `layout/usr/macOS/bin/postinst.sh` cache-hash case: same Ventura/24G90
  pairs; `*` prints a warning and **skips** cache trust (does not fail).
- `misc/test_restore_boot_contract.py` runs the real restore function for
  24G90/22F82/22F66/"" and asserts unknown non-empty build is rejected
  before the helper. 22D68 is not yet covered.
- `control` `Depends: python3, ldid` — still mismatches the test's
  expected `{gawk, ldid, odcctools, plutil, python3}` (pre-existing
  baseline failure, unchanged).
