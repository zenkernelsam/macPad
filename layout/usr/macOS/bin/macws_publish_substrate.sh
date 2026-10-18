# No shebang: this jailbreak's AMFI blocks execve of shebang scripts. Both
# package postinstall entry points invoke this file through bash explicitly.
#
# Publish the package-owned macOS CydiaSubstrate provider to the chroot dyld
# path with a fresh inode, exact LC_ID_DYLIB/platform/slice checks, and strict
# trustcache admission for both arm slices.  The native iOS substrate under
# /var/jb/Library/Frameworks is a different owner and is never touched.
set -eu

ROOTFS=/var/mnt/rootfs
SOURCE_DIR=/var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework
ENTITLEMENTS=/var/jb/usr/macOS/bin/entitlements.plist
PYTHON=/var/jb/usr/bin/python3
LIPO=/var/jb/usr/bin/lipo
LDID=/var/jb/usr/bin/ldid
JBCTL=/var/jb/usr/bin/jbctl
VERIFIER=/var/jb/usr/macOS/bin/macws_provider_contract.py

[ -f "$VERIFIER" ] || {
    echo "ERROR: provider verifier is missing: $VERIFIER" >&2
    exit 1
}
[ -x "$PYTHON" ] || {
    echo "ERROR: provider python is missing: $PYTHON" >&2
    exit 1
}

exec "$PYTHON" "$VERIFIER" publish \
    --source-dir "$SOURCE_DIR" \
    --rootfs "$ROOTFS" \
    --entitlements "$ENTITLEMENTS" \
    --lipo "$LIPO" \
    --ldid "$LDID" \
    --jbctl "$JBCTL"
