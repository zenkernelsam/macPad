#!/bin/bash
# install_rootfs_15.sh — install the macOS 15.6.1 chroot rootfs on the iPad.
# Run ON THE DEVICE as root. The rootfs staging tree is produced on the
# VirtualMac VM by build-rootfs-15.6.1.sh (Desktop/macos-15.6.1-rootfs).
#
# Usage:
#   install_rootfs_15.sh                 # pull from VM over ssh (default below)
#   install_rootfs_15.sh user@vm-ip      # pull from a different VM address
#   install_rootfs_15.sh /path/to.tar    # extract a local tar instead
#
# What it does:
#   1. preflight: kernel build, free space, current rootfs, tools
#   2. stream/extract the staging tree into /var/mnt/rootfs-15.new
#   3. verify SystemVersion.plist == 24G90 + required binaries
#   4. arm64ify WindowServer + Installer Progress (arm64e -> arm64/ALL)
#   5. swap /var/mnt/rootfs (old rootfs kept as rootfs-13.4.bak)
#   6. re-create /var/jb bind mount, run postinst.sh
#   7. smoke test: run_bash.sh -c "echo hi"
set -euo pipefail

# Filza/SSH terminals may export a minimal PATH missing /usr/sbin (sysctl)
# and the jb bootstrap dirs (tar/ssh) — make it explicit so every tool below
# resolves regardless of the calling environment.
export PATH="/var/jb/usr/bin:/var/jb/usr/sbin:/var/jb/usr/local/bin:/var/jb/bin:/var/jb/sbin:/usr/bin:/usr/sbin:/bin:/sbin:$PATH"

SRC="${1:-ciscohe@192.168.64.2}"
ROOTFS=/var/mnt/rootfs
NEW=/var/mnt/rootfs-15.new
BAK=/var/mnt/rootfs-13.4.bak
GUIDE=/var/jb/var/mobile/MacWSBootingGuide
PY=/var/jb/usr/bin/python3

echo "=== [1/7] preflight ==="
BUILD=$(sysctl -n kern.osversion)
echo "kernel build: $BUILD (expected 20D47 for iPadOS 16.3)"
[ "$BUILD" = "20D47" ] || echo "WARN: not 20D47 — iOS-side patches are gated on 16.3"
AVAIL=$(df -k /var/mnt | awk 'NR==2{print $4}')
echo "free on /var/mnt: $((AVAIL/1024)) MB"
[ "$AVAIL" -gt 26214400 ] || { echo "FAIL: need ~25GB free"; exit 1; }
[ -d "$ROOTFS" ] || { echo "FAIL: $ROOTFS absent (no existing rootfs?)"; exit 1; }
[ -x /var/jb/usr/local/bin/mount_bindfs ] || { echo "FAIL: mount_bindfs missing"; exit 1; }
[ -f "$PY" ] || { echo "FAIL: $PY missing"; exit 1; }
[ -f "$GUIDE/misc/arm64ify_macho.py" ] || { echo "FAIL: sync macPad repo first (git reset --hard origin/main)"; exit 1; }
mount | grep -E "on $ROOTFS( |/)" && echo "WARN: mounts inside old rootfs (will be unmounted at swap)" || true

rm -rf "$NEW"; mkdir -p "$NEW"

echo "=== [2/7] fetch + extract ==="
if [ -f "$SRC" ]; then
    echo "extracting local tar: $SRC"
    tar -xpf "$SRC" -C "$NEW"
else
    echo "streaming from VM: $SRC (Remote Login must be ON on the VM)"
    ssh -o ConnectTimeout=10 "$SRC" \
        'tar -C ~/Desktop/macos-15.6.1-rootfs -cf - .' | tar -xpf - -C "$NEW"
fi

# The staging tree was rsynced as a normal user on the VM, so every file
# arrives owned by uid 501. postinst evidence: "a uid/gid 501 Terminal main
# image was rejected by the root-owned first-party application admission
# invariant". Restore system ownership before anything else reads the tree.
echo "chown -R root:wheel (takes a minute)"
chown -R 0:0 "$NEW"

echo "=== [3/7] verify ==="
RB=$(/var/jb/usr/bin/plutil -extract ProductBuildVersion raw \
    "$NEW/System/Library/CoreServices/SystemVersion.plist" 2>/dev/null | tr -d '[:space:]')
echo "rootfs build: $RB"
[ "$RB" = "24G90" ] || { echo "FAIL: expected 24G90 (15.6.1), got '$RB'"; exit 1; }
for p in \
    "bin/bash" \
    "usr/lib/dyld" \
    "sbin/launchd" \
    "System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer" \
    "System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e" \
    "System/Volumes/Data" \
    "private/etc"; do
    [ -e "$NEW/$p" ] || { echo "FAIL: missing $p"; exit 1; }
done
echo "layout OK"

echo "=== [4/7] arm64ify entry-point executables ==="
WS="$NEW/System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer"
IP="$NEW/System/Library/CoreServices/Installer Progress.app/Contents/MacOS/Installer Progress"
for b in "$WS" "$IP"; do
    [ -f "$b" ] || { echo "FAIL: $b missing"; exit 1; }
    if ! "$PY" "$GUIDE/misc/arm64ify_macho.py" --check "$b" | grep -qw arm64; then
        "$PY" "$GUIDE/misc/arm64ify_macho.py" "$b"
        /var/jb/usr/bin/ldid -S "$b"
        /var/jb/usr/bin/ldid -S "$b"   # second pass: settled __LINKEDIT
    else
        echo "already arm64: $b"
    fi
done
# bash is the chroot smoke-test exec; convert too — proven harmless pattern.
"$PY" "$GUIDE/misc/arm64ify_macho.py" --check "$NEW/bin/bash" | grep -qw arm64 || {
    "$PY" "$GUIDE/misc/arm64ify_macho.py" "$NEW/bin/bash"
    /var/jb/usr/bin/ldid -S "$NEW/bin/bash"
}

# launchservicesd runs via the entitled iOS shim dlopening a converted
# dylib. The repo ships the 13.4 conversion — regenerate from THIS rootfs's
# binary so postinst's [ ! -e ] guard keeps the 15.6.1 build.
LSD="$NEW/System/Library/CoreServices"
if [ ! -e "$LSD/launchservicesd.dylib" ]; then
    "$PY" "$GUIDE/misc/exec_to_dylib.py" "$LSD/launchservicesd" \
        "$LSD/launchservicesd.dylib"
    /var/jb/usr/bin/ldid -S "$LSD/launchservicesd.dylib"
fi

echo "=== [5/7] swap rootfs ==="
for m in $(mount | grep -oE "on $ROOTFS/[^ ]*" | cut -d' ' -f2); do
    echo "umount $m"; umount "$m" || umount -f "$m" || true
done
rm -rf "$BAK.old"; [ -d "$BAK" ] && mv "$BAK" "$BAK.old"
mv "$ROOTFS" "$BAK"
mv "$NEW" "$ROOTFS"
echo "old rootfs -> $BAK (delete after 15.6.1 is proven)"

echo "=== [5.5/7] harvest iOS-side injections from old rootfs ==="
# These are not macOS files — they were baked into the 13.4 rootfs at setup
# and postinst expects them present (System/Tweaks/TweakLoader.dylib,
# CydiaSubstrate, systemhook). Copy them across unchanged.
for p in \
    "System/Tweaks/TweakLoader.dylib" \
    "System/Library/Frameworks/CydiaSubstrate.framework" \
    "usr/lib/systemhook.dylib" \
    "usr/local/Frameworks" \
    "usr/local/bin" \
    "usr/local/lib" \
    "usr/local/libexec" \
    "usr/local/share" \
    "opt" \
    "private/var/root" \
    "private/var/db/dslocal" \
    "private/etc/master.passwd" \
    "System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/Resources/CursorAsset_base"; do
    if [ -e "$BAK/$p" ] && [ ! -e "$ROOTFS/$p" ]; then
        mkdir -p "$ROOTFS/$(dirname "$p")"
        cp -a "$BAK/$p" "$ROOTFS/$p" && echo "  harvested $p"
    elif [ -e "$ROOTFS/$p" ]; then
        echo "  already present: $p"
    else
        echo "  MISSING in old rootfs too: $p"
    fi
done

echo "=== [6/7] bind mount + postinst ==="
mkdir -p "$ROOTFS/var/jb"
mount | grep -q "on $ROOTFS/var/jb " || \
    /var/jb/usr/local/bin/mount_bindfs /var/jb "$ROOTFS/var/jb"
bash "$ROOTFS/var/jb/usr/macOS/bin/postinst.sh"

echo "=== [7/7] smoke test ==="
bash /var/jb/usr/macOS/bin/run_bash.sh -c "echo hi" || {
    echo "SMOKE-FAIL — check oslog; recover with cleanup_all.sh if looping"; exit 1; }
echo
echo "=== rootfs 15.6.1 installed ==="
echo "Next: build the tweak, then boot:"
echo "  THEOS=/var/jb/var/mobile/theos bash $GUIDE/misc/build_on_ios.sh"
echo "  bash /var/jb/usr/macOS/bin/macos_gui.sh start"
echo "  sudo oslog | grep 'AMFI\|debugbydcmmc\|WindowSer\|MTL\|Metal'"
