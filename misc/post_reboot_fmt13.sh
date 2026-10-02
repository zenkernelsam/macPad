#!/bin/bash
# post_reboot_fmt13.sh — full restore + fmt13 kernel patch + CLI milestone test.
# Run ON THE DEVICE as root after a reboot+rejailbreak:
#   /var/mobile/post_reboot_fmt13.sh
#
# Steps:
#   1. restore_env.sh         — trustcache for dyld/libSystem/libdyld/echo
#   2. cache cdhashes         — main + .01 subcache into jb trustcache
#   3. helper cdhashes        — run_dbg_hold_v2 (+ any /var/mobile helpers)
#   4. fmt13_patch.py         — install format-13 fixup handler in kernel
#   5. frozen run             — child held at first exception for probing
#   6. live run               — the actual milestone witness: /bin/echo HI
set -u
JB=/var/jb/basebin/jbctl
PY=/var/jb/usr/bin/python3
R=/var/mnt/rootfs

echo "=== 1. restore_env (dyld+libs+echo) ==="
RESTORE_NO_VERIFY=1 bash /var/mobile/restore_env.sh

echo "=== 2. cache cdhashes ==="
for f in "$R/macdsc/dyld_shared_cache_arm64e" \
         "$R/macdsc/dyld_shared_cache_arm64e.01"; do
    [ -f "$f" ] || { echo "  skip (missing) $f"; continue; }
    for H in $($PY /var/mobile/nm/cdhash_slices.py "$f" 2>/dev/null | awk '{print $3}'); do
        $JB trustcache add "$H" >/dev/null 2>&1 || true
        echo "  TC $H ($f)"
    done
done

echo "=== 3. helper cdhashes ==="
for f in /var/mobile/run_dbg_hold_v2 /var/mobile/run_dbg /var/mobile/csprobe2.py; do
    [ -f "$f" ] || continue
    case "$f" in
        *.py) echo "  skip script $f";;
        *)  for H in $($PY /var/mobile/nm/cdhash_slices.py "$f" 2>/dev/null | awk '{print $3}'); do
                $JB trustcache add "$H" >/dev/null 2>&1 || true
                echo "  TC $H ($f)"
            done;;
    esac
done

echo "=== 4. fmt13 kernel patch ==="
$PY /var/mobile/fmt13_patch.py || { echo "PATCH FAILED — aborting"; exit 1; }

echo "=== 5. milestone: /bin/echo HI ==="
/var/mobile/run_dbg_hold_v2 /var/jb/usr/bin/chroot "$R" /bin/echo HI
