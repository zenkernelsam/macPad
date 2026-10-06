#!/bin/bash
# build-rootfs-13.4.1.sh — assemble a macOS Ventura 13.4.1 (22F82) chroot
# rootfs staging tree entirely from the verified UniversalMac restore IPSW.
#
# Sources (all must already be mounted read-only; nothing else is trusted):
#   $1  mounted OS volume      e.g. /tmp/096-09648-077.dmg.mount
#                                 (produced by `ipsw mount fs <ipsw>`)
#   $2  mounted arm64e cryptex e.g. /Volumes/RomeF22F82.arm64eSystemCryptex
#                                 (096-09706-080.dmg via hdiutil attach)
#   $3  output staging dir     default ~/Desktop/macos-13.4.1-rootfs
#
# The staging tree is host-side only. Ownership is recorded by tar at pack
# time and restored by root extraction on the iPad (install_rootfs_13.sh).
# Everything here is source-confirmed: ProductBuildVersion is checked on
# BOTH mounts before a single byte is copied.
set -euo pipefail

OSDMG=${1:?usage: build-rootfs-13.4.1.sh <os-mount> <cryptex-mount> [stage]}
CRYPTEX=${2:?usage: build-rootfs-13.4.1.sh <os-mount> <cryptex-mount> [stage]}
STAGE=${3:-"$HOME/Desktop/macos-13.4.1-rootfs"}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG="$STAGE.build.log"
exec > >(tee -a "$LOG") 2>&1
echo "=== build start: $(date) ==="

require_build() {
    local mount_point=$1 want=$2
    local plist="$mount_point/System/Library/CoreServices/SystemVersion.plist"
    [ -f "$plist" ] || { echo "FATAL: $plist missing" >&2; exit 1; }
    local got
    got=$(plutil -extract ProductBuildVersion raw -o - "$plist" 2>/dev/null \
        | tr -d '[:space:]')
    [ "$got" = "$want" ] || {
        echo "FATAL: $mount_point is build '$got', expected '$want'" >&2
        exit 1
    }
    echo "  verified: $mount_point  ProductBuildVersion=$got"
}

echo "--- [0/7] verify sources are 22F82 ---"
require_build "$OSDMG"   22F82
require_build "$CRYPTEX" 22F82
for f in usr/lib/dyld bin/echo bin/bash sbin/launchd \
    "System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer"; do
    [ -f "$OSDMG/$f" ] || { echo "FATAL: OS volume lacks $f" >&2; exit 1; }
done
for f in dyld_shared_cache_arm64e dyld_shared_cache_arm64e.01 \
         dyld_shared_cache_arm64e.map; do
    [ -f "$CRYPTEX/System/Library/dyld/$f" ] || {
        echo "FATAL: cryptex lacks $f" >&2; exit 1; }
done

mkdir -p "$STAGE"
# rsync exits 23 when a few root-only files can't be read. Logged + tolerable.
RSYNC()  { rsync -aEHx "$@" || { rc=$?; [ $rc -eq 23 ] || return $rc; }; }
RSYNCX() { rsync -aEH  "$@" || { rc=$?; [ $rc -eq 23 ] || return $rc; }; }

echo "--- [1/7] sealed OS volume root (top-level dirs + firmlinks) ---"
# The dmg root IS the sealed system volume: copy it whole so empty
# mountpoints (dev, Volumes, .vol, ...) and the etc/tmp/var symlinks land
# exactly as Apple ships them. -x keeps us on the dmg filesystem.
RSYNC --exclude='/System' --exclude='/usr' --exclude='/bin' --exclude='/sbin' \
    "$OSDMG/" "$STAGE/"

echo "--- [2/7] /System (SSV payload) ---"
RSYNC \
  --exclude='/Volumes' \
  --exclude='/Library/Caches' \
  --exclude='/Library/Assets' \
  --exclude='/Library/AssetsV2' \
  --exclude='/Library/PreinstalledAssets' \
  --exclude='/Library/PreinstalledAssetsV2' \
  --exclude='/Library/Speech' \
  "$OSDMG/System/" "$STAGE/System/"

echo "--- [3/7] OS cryptex -> System/Volumes/Preboot/Cryptexes/OS ---"
# The cryptex is 3.9GB and carries far more than the shared cache
# (iOSSupport, Frameworks/PrivateFrameworks resources, usr/lib/*). Ship it
# whole, matching the 15.6 builder's semantics.
mkdir -p "$STAGE/System/Volumes/Preboot/Cryptexes"
# x86_64 shared caches and the Rosetta AOT cache can never load on iOS —
# ~2.5GB of dead bytes. Everything else ships whole.
RSYNCX \
  --exclude='System/Library/dyld/dyld_shared_cache_x86_64*' \
  --exclude='System/Library/dyld/aot_shared_cache*' \
  "$CRYPTEX/" "$STAGE/System/Volumes/Preboot/Cryptexes/OS/"

echo "--- [4/7] /usr /bin /sbin ---"
RSYNC --exclude='/local' "$OSDMG/usr/"  "$STAGE/usr/"
RSYNC "$OSDMG/bin/"  "$STAGE/bin/"
RSYNC "$OSDMG/sbin/" "$STAGE/sbin/"

echo "--- [5/7] Templates/Data skeleton merge (private/etc, private/var, ...) ---"
# The sealed volume has an EMPTY private/. The data template supplies the
# real /private/etc closure, /var skeleton, /Users, /Library seeds.
RSYNCX "$OSDMG/System/Library/Templates/Data/" "$STAGE/"

echo "--- [6/7] symlinks per guide ---"
cd "$STAGE"
ln -sfn ../..                        System/Volumes/Data
ln -sfn private/etc                  etc
ln -sfn private/tmp                  tmp
ln -sfn private/var                  var
ln -sfn System/Volumes/Data/home     home
mkdir -p var/folders
ln -sfn /var/folders/zz              var/folders/zz
mkdir -p "$STAGE/Users/root" "$STAGE/private/tmp"

echo "--- [7/7] post-build verification + cache CDHash ledger ---"
for f in \
    usr/lib/dyld bin/echo bin/bash sbin/launchd private/etc \
    "System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer" \
    "System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e" \
    "System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.01"; do
    [ -e "$STAGE/$f" ] || { echo "FATAL: staged rootfs lacks $f" >&2; exit 1; }
done
plutil -extract ProductBuildVersion raw -o - \
    "$STAGE/System/Library/CoreServices/SystemVersion.plist"
"$SCRIPT_DIR/cache_cdhash.py" \
    "$STAGE/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e" \
    "$STAGE/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.01"
echo "=== build done: $(date) ==="
du -sh "$STAGE"
