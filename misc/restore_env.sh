#!/bin/bash
# restore_env.sh [dyld_src.bin] — re-establish the chroot HELLO environment after
# a device reboot. Run ON THE DEVICE as root.
#
# Background (why a reboot breaks it):
#   * run_nocskill used a HARD-CODED kernel text slide (0x158B4000) -> after any
#     reboot KASLR moves the kernel -> find_proc() reads garbage -> the launcher
#     dies with "[!] proc not found".  FIXED in misc/run_nocskill.c (it now scans
#     for the kernel Mach-O header); rebuild + redeploy it on the host.
#   * the jailbreak trustcache lives in memory and is WIPED on reboot -> every
#     patched/re-signed Mach-O the chroot loads gets AMFI-killed with
#     "code signature invalid (errno=1)". Re-register their cdhashes here.
#   * the chroot's rootfs itself (/var/mnt/rootfs) is a plain directory on the
#     Data volume and survives reboot untouched. Nothing to re-mount for HELLO.
set -u
JB=/var/jb/basebin/jbctl
LD=/var/jb/usr/bin/ldid
PY=/var/jb/usr/bin/python3
R=/var/mnt/rootfs
ENT=/var/jb/usr/macOS/bin/entitlements.plist
SRC="${1:-}"

# 1) optional: (re)deploy a dyld binary into the chroot with a fresh inode.
if [ -n "$SRC" ]; then
    [ -f "$SRC" ] || { echo "NO SRC $SRC"; exit 1; }
    $LD -Hsha256 -S$ENT "$SRC" 2>&1 | tail -1
    for H in $($PY /var/mobile/nm/cdhash_slices.py "$SRC" 2>/dev/null | awk '{print $3}'); do
        $JB trustcache add "$H" >/dev/null 2>&1 || true
    done
    rm -f "$R/usr/lib/dyld" && cp "$SRC" "$R/usr/lib/dyld" && chmod 755 "$R/usr/lib/dyld"
    echo "deployed dyld md5=$($PY -c "import hashlib;print(hashlib.md5(open('$R/usr/lib/dyld','rb').read()).hexdigest())")"
fi

# 2) re-register trustcache for every Mach-O the HELLO path loads.
for f in "$R/usr/lib/dyld" "$R/usr/lib/libSystem.B.dylib" \
         "$R/usr/lib/system/libdyld.dylib" "$R/bin/echo"; do
    [ -f "$f" ] || { echo "MISSING $f"; continue; }
    for H in $($PY /var/mobile/nm/cdhash_slices.py "$f" 2>/dev/null | awk '{print $3}'); do
        $JB trustcache add "$H" >/dev/null 2>&1 || true
        echo "  TC $H hit=$($JB trustcache info 2>/dev/null | grep -ic $H)  ($f)"
    done
done

# 3) verify.
echo "=== HELLO x3 ==="
for i in 1 2 3; do
    /var/mobile/run_nocskill /var/jb/usr/bin/env -i PATH=/usr/bin:/bin \
        /var/jb/usr/bin/chroot "$R" /bin/echo HELLO 2>&1 | tail -2
    echo "---"
done
