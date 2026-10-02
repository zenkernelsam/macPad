#!/bin/bash
# post_reboot_tc_probe.sh — trustcache state probe for the 24G90 (15.6.1) cache
# pair, per UPSTREAM-MERGE-AND-CLUES-2026-10-02.md section 4/5 step 1.
#
# Run ON THE DEVICE as root, immediately after re-jailbreak and BEFORE any
# chroot start or restore_env:
#   /var/jb/usr/bin/bash /var/mobile/post_reboot_tc_probe.sh
#
# Phase 0 records whether the 24G90 cache CDHashes survived reboot.
# Phase 1 runs `macos_gui.sh production` (the shipped cold-start trust gate)
# and re-checks — this is the runtime test of the suspected missing 24G90
# branch in macos_gui.sh's trust-restore call.
#
# NOTE: jbctl prints UPPERCASE hex; the greps below are case-insensitive.
set -u
JB=/var/jb/basebin/jbctl
H1=2b9cccd5
H2=8c7ba7e5

probe() {
    echo "--- trustcache query: $1 ---"
    $JB trustcache info | grep -i -e "$H1" -e "$H2" || echo "  ABSENT ($1)"
}

echo "=== PHASE 0: cold-boot trustcache state (before macos_gui.sh) ==="
probe "post-reboot"

echo "=== PHASE 1: macos_gui.sh production (shipped restore path) ==="
if [ -x /var/mnt/rootfs/usr/macOS/bin/macos_gui.sh ] || \
   [ -f /var/mnt/rootfs/usr/macOS/bin/macos_gui.sh ]; then
    bash /var/mnt/rootfs/usr/macOS/bin/macos_gui.sh production \
        2>&1 | tail -30
elif [ -f /var/mobile/macos_gui.sh ]; then
    bash /var/mobile/macos_gui.sh production 2>&1 | tail -30
else
    echo "macos_gui.sh not found — skipping phase 1"
fi

echo "=== PHASE 1b: trustcache state AFTER macos_gui.sh ==="
probe "post-macos_gui"

echo "=== verdict ==="
echo "absent in phase0 AND absent after macos_gui => missing-24G90-branch confirmed"
echo "absent in phase0 AND present after => gap exists but macos_gui covers it"
echo "present in phase0 => TC not the current blocker; proceed to csprobe work"
