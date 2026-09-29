#!/bin/bash
# build-rootfs-15.6.1.sh — assemble a macOS 15.6.1 chroot rootfs staging tree
# from this running VirtualMac VM, following the MacWSBootingGuide layout.
# Runs as a normal user; ownership is recorded by tar at pack time and
# restored by root extraction on the iPad.
# NOTE: rsync exclude patterns are relative to each SOURCE root.
#   -x keeps us on the source filesystem (firmlinks/mounts look like other
#   devs to stat) — belt & suspenders against swallowing /System/Volumes.
set -euo pipefail

STAGE="$HOME/Desktop/macos-15.6.1-rootfs"
LOG="$HOME/Desktop/rootfs-build.log"
exec > >(tee -a "$LOG") 2>&1
echo "=== build start: $(date) ==="

mkdir -p "$STAGE"
# rsync exits 23 when a few root-only files can't be read (we run without
# sudo). Those are logged + tolerable — see docs/porting/rootfs-15.6.1-install.md.
RSYNC() { rsync -aEHx "$@" || { rc=$?; [ $rc -eq 23 ] || return $rc; }; }
RSYNCX() { rsync -aEH "$@" || { rc=$?; [ $rc -eq 23 ] || return $rc; }; }

echo "--- [1/6] /System (SSV only) ---"
RSYNC \
  --exclude='/Volumes' \
  --exclude='/Library/Caches' \
  --exclude='/Library/Assets' \
  --exclude='/Library/AssetsV2' \
  --exclude='/Library/PreinstalledAssets' \
  --exclude='/Library/PreinstalledAssetsV2' \
  --exclude='/Library/Speech' \
  /System/ "$STAGE/System/"

echo "--- [2/6] OS cryptex -> System/Volumes/Preboot/Cryptexes/OS ---"
mkdir -p "$STAGE/System/Volumes/Preboot/Cryptexes"
RSYNCX /System/Volumes/Preboot/Cryptexes/OS/ \
  "$STAGE/System/Volumes/Preboot/Cryptexes/OS/"

echo "--- [3/6] /usr /bin /sbin ---"
RSYNC --exclude='/local' /usr/  "$STAGE/usr/"
RSYNC /bin/  "$STAGE/bin/"
RSYNC /sbin/ "$STAGE/sbin/"

echo "--- [4/6] Templates/Data skeleton merge ---"
RSYNCX /System/Library/Templates/Data/ "$STAGE/"

echo "--- [5/6] real Data-volume bits needed ---"
mkdir -p "$STAGE/private/etc" \
  "$STAGE/System/Library/CoreServices/CoreTypes.bundle/Contents/Library" \
  "$STAGE/Users/root"
RSYNCX /private/etc/ "$STAGE/private/etc/"
RSYNCX "/System/Library/CoreServices/CoreTypes.bundle/Contents/Library/" \
  "$STAGE/System/Library/CoreServices/CoreTypes.bundle/Contents/Library/"

echo "--- [6/6] symlinks per guide ---"
cd "$STAGE"
ln -sfn ../..                        System/Volumes/Data
ln -sfn private/etc                  etc
ln -sfn private/tmp                  tmp
ln -sfn private/var                  var
ln -sfn System/Volumes/Data/home     home
mkdir -p var/folders
ln -sfn /var/folders/zz              var/folders/zz

echo "=== build done: $(date) ==="
du -sh "$STAGE"
