set -e
export PATH=/var/jb/usr/bin:/var/jb/usr/sbin:/usr/bin:/usr/sbin:/bin:/sbin
LIB=/var/jb/usr/macOS/lib/libmachook.dylib
LIB64=/var/jb/usr/macOS/lib/libmachook_arm64.dylib
RLIB=/var/mnt/rootfs/usr/local/lib/libmachook.dylib
RLIB64=/var/mnt/rootfs/usr/local/lib/libmachook_arm64.dylib
ENT=/var/jb/usr/macOS/bin/entitlements.plist

# sign each thin slice twice (ldid+lipo first-sign Invalid-Page workaround)
ldid -S$ENT -M /tmp/mh_e.dylib; ldid -S$ENT -M /tmp/mh_e.dylib
ldid -S$ENT -M /tmp/mh_a.dylib; ldid -S$ENT -M /tmp/mh_a.dylib

# fresh inodes into both install locations
rm -f "$LIB" "$LIB64" "$RLIB" "$RLIB64"
install -m755 /tmp/mh_e.dylib "$LIB"
install -m755 /tmp/mh_a.dylib "$LIB64"
install -m755 /tmp/mh_e.dylib "$RLIB"
install -m755 /tmp/mh_a.dylib "$RLIB64"

# trustcache: readd all four deployed files
python3 /var/jb/usr/macOS/bin/macws_boot_trust.py --readd \
    "$LIB" "$LIB64" "$RLIB" "$RLIB64"

# baseline
/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI

# install new daemon plist + updated gui script (fresh inodes)
install -m644 /tmp/com.macwsguide.tccd-system.plist \
    /var/jb/usr/macOS/LaunchDaemons/com.macwsguide.tccd-system.plist.new
mv /var/jb/usr/macOS/LaunchDaemons/com.macwsguide.tccd-system.plist.new \
   /var/jb/usr/macOS/LaunchDaemons/com.macwsguide.tccd-system.plist
install -m755 /tmp/macos_gui.sh /var/jb/usr/macOS/bin/macos_gui.sh.new
mv /var/jb/usr/macOS/bin/macos_gui.sh.new /var/jb/usr/macOS/bin/macos_gui.sh

# verify mapping string landed in deployed slices
strings "$RLIB" | grep -c 'macosbooter.tccd.system' || true
echo DEPLOY-OK
