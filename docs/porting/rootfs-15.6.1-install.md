# macOS 15.6.1 rootfs install runbook (iPadOS 16.3 / 20D47)

Status: staging-tree builder + device installer written 2026-09-24;
not yet executed on device.

## What changed vs the 13.4 flow

| piece | 13.4 | 15.6.1 | handled by |
|---|---|---|---|
| rootfs source | full-filesystem dmg | this VirtualMac VM (24G90, sealed) | `build-rootfs-15.6.1.sh` |
| dyld cache CDHash | b5da3940…/bbb76598… | 2b9cccd5…/8c7ba7e5… | postinst selects by ProductBuildVersion |
| exec cpusubtype | arm64/ALL already | all fat x86_64+arm64e | `misc/arm64ify_macho.py` |
| launchservicesd.dylib | shipped pre-converted | must re-convert | `misc/exec_to_dylib.py` |
| patches | original | dual-gated (4567225) | libmachook already handles both |

## Phase A — on the VM (this machine, no sudo needed)

```bash
~/Desktop/build-rootfs-15.6.1.sh        # ~25GB into ~/Desktop/macos-15.6.1-rootfs
```

Produces the upstream-guide layout: SSV (`System/usr/bin/sbin`),
`System/Volumes/Preboot/Cryptexes/OS` (real cryptex, dyld cache inside),
`System/Volumes/Data -> ../..`, `Templates/Data` skeleton merged at root,
`etc->private/etc`, `var->private/var`, `tmp->private/tmp`,
`home->System/Volumes/Data/home`, `var/folders/zz -> /var/folders/zz`,
`Users/root`.

Known skipped (permission denied without sudo, all confirmed
non-load-bearing for chroot boot):
`System/Library/DirectoryServices/DefaultLocalDB/Default` (local OD node —
recreate on device if dscl needed), `Templates/Data/private/var/spool/postfix/*`
(empty mail queues).

Then enable **Remote Login** on the VM (System Settings -> Sharing) so the
iPad can pull. The VM is behind the host's VZ NAT at 192.168.64.2.

## Phase B — on the iPad (root shell)

Two artifacts were produced on the VM (cross-compile, no Theos on device
needed for the package itself):

- `~/Desktop/macPad/packages/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb`
  (5.6 MB — built 2026-09-25 via theos + dpkg-deb --root-owner-group;
  verified: postinst carries the 24G90 dyld-cache hash pair, libmachook
  carries the dual-variant 15.6.1 code)
- `~/Desktop/macos-15.6.1-rootfs.tar` (19 GB)

Order matters — the deb refreshes the tools (incl. postinst) that the
rootfs installer invokes at the end:

```bash
cd /var/jb/var/mobile/MacWSBootingGuide
git fetch origin && git reset --hard origin/main   # gets misc scripts
sudo dpkg -i /path/to/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb
sudo bash misc/install_rootfs_15.sh /path/to/macos-15.6.1-rootfs.tar
# (or with no arg: ssh-pull from ciscohe@192.168.64.2 staging dir)
```

The script: preflight (20D47, ~25GB free, tools) -> stream/extract to
`/var/mnt/rootfs-15.new` -> `chown -R root:wheel` -> verify 24G90 + key
binaries -> arm64ify WindowServer + Installer Progress + /bin/bash ->
convert launchservicesd -> umount old binds -> swap (`/var/mnt/rootfs` ->
`rootfs-13.4.bak`) -> `mount_bindfs /var/jb` -> `postinst.sh` -> smoke test.

## Phase C — build + boot

```bash
THEOS=/var/jb/var/mobile/theos bash misc/build_on_ios.sh
bash /var/jb/usr/macOS/bin/macos_gui.sh start
sudo oslog | grep "AMFI\|debugbydcmmc\|WindowSer\|MTL\|Metal"
```

Watch for the new dual-variant log lines: `CURSOR-ABI resolved`,
`IOMFB-ABI resolved`, `QC-FRAMEINFO resolved`, `STUB_FIX repaired-early`,
`COEXIST verified kern_SwapEnd BL`, `CANCEL-COMPLETION observer`.

## Rollback

```bash
bash /var/jb/var/mobile/MacWSBootingGuide/misc/cleanup_all.sh   # stop loops
umount /var/mnt/rootfs/var/jb 2>/dev/null
mv /var/mnt/rootfs /var/mnt/rootfs-15.failed
mv /var/mnt/rootfs-13.4.bak /var/mnt/rootfs
mount_bindfs /var/jb /var/mnt/rootfs/var/jb
bash /var/jb/usr/macOS/bin/postinst.sh
```

## Open risks (evidence-based, not theories)

1. **arm64e-exec**: if iOS rejects arm64e macOS execs generally (not just
   WS/InstallerProgress), smoke test fails with exec errors; widen the
   arm64ify list in the installer.
2. **launchservicesd**: converted fresh; if the shim's dlopen_entry_point
   needs more than LC_MAIN, watch its crash log — isolated to its own job.
3. **DefaultLocalDB absent**: dscl/OpenDirectory lookups in chroot may
   fail; only matters for loginwindow/user-switch flows, not first pixels.
4. **xattr loss**: tar stream drops xattrs (staging tree was rsync anyway).
   Rootless-attr-dependent paths would show as weird "Operation not
   permitted" — none expected on the SSV payload.
5. **dyld cache .atlas**: 15.6.1 adds `.atlas`/`.map` siblings; dyld may
   want them trusted too — if WS dies in dyld-map, register their CDHashes
   (they showed no signature on the VM — likely unsigned data files).
