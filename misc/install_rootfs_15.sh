#!/bin/sh
# install_rootfs_15.sh — install the macOS 15.6.1 chroot rootfs on the iPad.
# Needs bash (process substitution); if invoked via a plain sh, re-exec.
if [ -z "${BASH_VERSION:-}" ]; then
    for b in /var/jb/usr/bin/bash /bin/bash /usr/bin/bash; do
        [ -x "$b" ] && exec "$b" "$0" "$@"
    done
    echo "FAIL: bash not found — apt install bash" >&2; exit 1
fi
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
# Helper scripts (arm64ify/exec_to_dylib) may live either next to this script
# (e.g. downloaded together into a Filza-visible folder) or in the repo clone.
SELF_DIR=$(cd "$(dirname "$0")" 2>/dev/null && pwd)
if [ -f "$SELF_DIR/arm64ify_macho.py" ]; then
    MISC_DIR="$SELF_DIR"
else
    MISC_DIR="/var/jb/var/mobile/MacWSBootingGuide/misc"
fi
PY=/var/jb/usr/bin/python3

echo "=== [1/7] preflight ==="
BUILD=$(sysctl -n kern.osversion)
echo "kernel build: $BUILD (expected 20D47 for iPadOS 16.3)"
[ "$BUILD" = "20D47" ] || echo "WARN: not 20D47 — iOS-side patches are gated on 16.3"
# /var/mnt does not exist on a device that never had a rootfs — create it
# (Data volume, harmless) so df and later steps have their anchor.
mkdir -p /var/mnt
# df + awk may both be absent in minimal bootstrap environments — parse in
# pure bash (df -kP: one line per fs, POSIX layout, no wrapping).
AVAIL=0
while read -r fs blocks used avail rest; do
    case "$avail" in ''|*[!0-9]*) ;; *) AVAIL=$avail ;; esac
done < <(df -kP /var/mnt 2>/dev/null)
echo "free on /var/mnt: $((AVAIL/1024)) MB"
[ "$AVAIL" -gt 26214400 ] || { echo "FAIL: need ~25GB free"; exit 1; }
if [ -d "$ROOTFS" ]; then
    echo "existing rootfs found — will be kept as $BAK"
else
    echo "no existing rootfs — fresh install (harvest falls back to /var/jb ElleKit)"
fi
# Check every external tool this script and the postinst chain need, in one
# pass — a minimal bootstrap is missing several and whack-a-mole is slow.
MISSING_TOOLS=""
for t in /var/jb/usr/bin/python3 /var/jb/usr/bin/ldid /var/jb/usr/bin/grep \
         /var/jb/usr/bin/jbctl /var/jb/usr/bin/uicache \
         /var/jb/usr/local/bin/mount_bindfs \
         tar strings cut timeout realpath; do
    if [ "${t#/}" != "$t" ]; then
        [ -x "$t" ] || MISSING_TOOLS="$MISSING_TOOLS $t"
    else
        command -v "$t" >/dev/null 2>&1 || MISSING_TOOLS="$MISSING_TOOLS $t"
    fi
done
if [ -n "$MISSING_TOOLS" ]; then
    echo "FAIL: missing tools:$MISSING_TOOLS"
    echo "  -> fix with: sudo apt install -y python3 ldid coreutils grep uikittools tar binutils findutils"
    echo "  -> mount_bindfs ships inside the deb (dpkg -i installs it)"
    exit 1
fi
[ -f "$MISC_DIR/arm64ify_macho.py" ] || { echo "FAIL: arm64ify_macho.py not found next to this script or in the repo clone"; exit 1; }
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
# plutil may not be installed on a minimal bootstrap — use python3 (already
# a hard dep) to read the build version.
RB=$("$PY" -c 'import plistlib,sys; print(plistlib.load(open(sys.argv[1],"rb"))["ProductBuildVersion"])' \
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
    if ! "$PY" "$MISC_DIR/arm64ify_macho.py" --check "$b" | grep -qw arm64; then
        "$PY" "$MISC_DIR/arm64ify_macho.py" "$b"
        /var/jb/usr/bin/ldid -S "$b"
        /var/jb/usr/bin/ldid -S "$b"   # second pass: settled __LINKEDIT
    else
        echo "already arm64: $b"
    fi
done
# bash is the chroot smoke-test exec; convert too — proven harmless pattern.
"$PY" "$MISC_DIR/arm64ify_macho.py" --check "$NEW/bin/bash" | grep -qw arm64 || {
    "$PY" "$MISC_DIR/arm64ify_macho.py" "$NEW/bin/bash"
    /var/jb/usr/bin/ldid -S "$NEW/bin/bash"
}

# launchservicesd runs via the entitled iOS shim dlopening a converted
# dylib. The repo ships the 13.4 conversion — regenerate from THIS rootfs's
# binary so postinst's [ ! -e ] guard keeps the 15.6.1 build.
LSD="$NEW/System/Library/CoreServices"
if [ ! -e "$LSD/launchservicesd.dylib" ]; then
    "$PY" "$MISC_DIR/exec_to_dylib.py" "$LSD/launchservicesd" \
        "$LSD/launchservicesd.dylib"
    /var/jb/usr/bin/ldid -S "$LSD/launchservicesd.dylib"
fi

echo "=== [5/7] swap rootfs ==="
for m in $(mount | grep -oE "on $ROOTFS/[^ ]*" | cut -d' ' -f2); do
    echo "umount $m"; umount "$m" || umount -f "$m" || true
done
rm -rf "$BAK.old"
if [ -d "$ROOTFS" ]; then
    [ -d "$BAK" ] && mv "$BAK" "$BAK.old"
    mv "$ROOTFS" "$BAK"
    echo "old rootfs -> $BAK (delete after 15.6.1 is proven)"
else
    BAK=""
    echo "fresh install — nothing to back up"
fi
mv "$NEW" "$ROOTFS"

echo "=== [5.5/7] harvest iOS-side injections ==="
# These are not macOS files — the 13.4 rootfs had them baked in at setup and
# postinst expects them present (System/Tweaks/TweakLoader.dylib,
# CydiaSubstrate, systemhook). Sources, in order: the old rootfs backup,
# then the ElleKit copies already on this device under /var/jb.
harvest() {
    rel="$1"; jb_src="${2:-}"
    if [ -e "$ROOTFS/$rel" ]; then
        echo "  already present: $rel"; return
    fi
    if [ -n "$BAK" ] && [ -e "$BAK/$rel" ]; then
        mkdir -p "$ROOTFS/$(dirname "$rel")"
        cp -a "$BAK/$rel" "$ROOTFS/$rel" && echo "  harvested $rel (old rootfs)"; return
    fi
    if [ -n "$jb_src" ] && [ -e "$jb_src" ]; then
        mkdir -p "$ROOTFS/$(dirname "$rel")"
        cp -a "$jb_src" "$ROOTFS/$rel" && echo "  harvested $rel (/var/jb)"; return
    fi
    echo "  MISSING: $rel"
}
harvest "System/Tweaks/TweakLoader.dylib" "/var/jb/usr/lib/TweakLoader.dylib"
harvest "usr/lib/systemhook.dylib" "/var/jb/usr/lib/systemhook.dylib"
for cs in /var/jb/Library/Frameworks/CydiaSubstrate.framework \
          /var/jb/usr/lib/CydiaSubstrate.framework; do
    [ -e "$cs" ] && { harvest "System/Library/Frameworks/CydiaSubstrate.framework" "$cs"; break; }
done
[ -e "$ROOTFS/System/Library/Frameworks/CydiaSubstrate.framework" ] || \
    echo "  MISSING: CydiaSubstrate.framework (install ElleKit/Substitute compat)"
for p in \
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
    harvest "$p"
done
# A fresh 15.6.1 staging tree has no master.passwd (root-only in Templates).
# Synthesize the stock macOS skeleton — DirectoryService/dslocal is the real
# user store; this file only needs to exist for legacy lookups.
if [ ! -f "$ROOTFS/private/etc/master.passwd" ]; then
    cat > "$ROOTFS/private/etc/master.passwd" <<'EOF'
##
# User Database
#
# This file is the authoritative user database for BSD tools. macOS's real
# user store is OpenDirectory (dslocal); these are the stock system entries.
##
nobody:*:-2:-2::0:0:Unprivileged User:/var/empty:/usr/bin/false
root:*:0:0::0:0:System Administrator:/var/root:/bin/sh
daemon:*:1:1::0:0:System Services:/var/root:/usr/bin/false
_uucp:*:4:4::0:0:Unix to Unix Copy Protocol:/var/spool/uucp:/usr/sbin/uucico
_taskgated:*:13:13::0:0:Task Gate Daemon:/var/empty:/usr/bin/false
_networkd:*:24:24::0:0:Network Services:/var/networkd:/usr/bin/false
_installassistant:*:25:25::0:0:Install Assistant:/var/empty:/usr/bin/false
_lp:*:26:26::0:0:Printing Services:/var/spool/cups:/usr/bin/false
_postfix:*:27:27::0:0:Postfix Mail Server:/var/spool/postfix:/usr/bin/false
_scsd:*:31:31::0:0:Service Configuration Service:/var/empty:/usr/bin/false
_ces:*:32:32::0:0:Certificate Enrollment Service:/var/empty:/usr/bin/false
_mcxalr:*:54:54::0:0:MCX AppLaunch:/var/empty:/usr/bin/false
_appleevents:*:55:55::0:0:AppleEvents Daemon:/var/empty:/usr/bin/false
_geod:*:56:56::0:0:Geo Services Daemon:/var/db/geod:/usr/bin/false
_serialnumberd:*:58:58::0:0:Serial Number Daemon:/var/empty:/usr/bin/false
_devdocs:*:59:59::0:0:Developer Documentation:/var/empty:/usr/bin/false
_sandbox:*:60:60::0:0:Seatbelt:/var/empty:/usr/bin/false
_mdnsresponder:*:65:65::0:0:mDNSResponder:/var/empty:/usr/bin/false
_ard:*:67:67::0:0:Apple Remote Desktop:/var/empty:/usr/bin/false
_www:*:70:70::0:0:World Wide Web Server:/Library/WebServer:/usr/bin/false
_eppc:*:71:71::0:0:Apple Events User:/var/empty:/usr/bin/false
_cvs:*:72:72::0:0:CVS Server:/var/empty:/usr/bin/false
_svn:*:73:73::0:0:SVN Server:/var/empty:/usr/bin/false
_mysql:*:74:74::0:0:MySQL Server:/var/empty:/usr/bin/false
_sshd:*:75:75::0:0:sshd Privilege separation:/var/empty:/usr/bin/false
_qtss:*:76:76::0:0:QuickTime Streaming Server:/var/empty:/usr/bin/false
_cyrus:*:77:6::0:0:Cyrus Administrator:/var/imap:/usr/bin/false
_mailman:*:78:78::0:0:MailMan ListServer:/var/empty:/usr/bin/false
_appserver:*:79:79::0:0:Application Server:/var/empty:/usr/bin/false
_clamav:*:82:82::0:0:ClamAV Daemon:/var/virusmails:/usr/bin/false
_amavisd:*:83:83::0:0:AMaViS Daemon:/var/virusmails:/usr/bin/false
_jabber:*:84:84::0:0:Jabber User:/var/empty:/usr/bin/false
_appowner:*:87:87::0:0:Application Owner:/var/empty:/usr/bin/false
_windowserver:*:88:88::0:0:WindowServer:/var/empty:/usr/bin/false
_spotlight:*:89:89::0:0:Spotlight:/var/empty:/usr/bin/false
_tokend:*:91:91::0:0:Token Daemon:/var/empty:/usr/bin/false
_securityagent:*:92:92::0:0:SecurityAgent:/var/db/securityagent:/usr/bin/false
_calendar:*:93:93::0:0:Calendar:/var/empty:/usr/bin/false
_teamsserver:*:94:94::0:0:TeamsServer:/var/teamsserver:/usr/bin/false
_update_sharing:*:95:-2::0:0:Update Sharing:/var/empty:/usr/bin/false
_installer:*:96:-2::0:0:Installer:/var/empty:/usr/bin/false
_atsserver:*:97:97::0:0:ATS Server:/var/empty:/usr/bin/false
_ftp:*:98:-2::0:0:FTP Daemon:/var/empty:/usr/bin/false
_unknown:*:99:99::0:0:Unknown User:/var/empty:/usr/bin/false
_softwareupdate:*:200:200::0:0:Software Update:/var/db/softwareupdate:/usr/bin/false
_coreaudiod:*:202:202::0:0:Core Audio Daemon:/var/empty:/usr/bin/false
_screensaver:*:203:203::0:0:Screensaver:/var/empty:/usr/bin/false
_locationd:*:205:205::0:0:Location Daemon:/var/db/locationd:/usr/bin/false
_trustevaluationagent:*:208:208::0:0:Trust Evaluation Agent:/var/empty:/usr/bin/false
_timezone:*:210:210::0:0:AutoTimeZoneDaemon:/var/empty:/usr/bin/false
_lda:*:211:211::0:0:Local Delivery Agent:/var/empty:/usr/bin/false
_cvmsroot:*:212:212::0:0:CVMS Root:/var/empty:/usr/bin/false
_usbmuxd:*:213:213::0:0:iPhone OS Device Helper:/var/db/lockdown:/usr/bin/false
_dovecot:*:214:6::0:0:Dovecot Administrator:/var/empty:/usr/bin/false
_dpaudio:*:215:215::0:0:DP Audio:/var/empty:/usr/bin/false
_postgres:*:216:216::0:0:PostgreSQL Server:/var/empty:/usr/bin/false
_krbgt:*:217:-2::0:0:Kerberos Ticket Granting Ticket:/var/empty:/usr/bin/false
_krbgkrbtgt:*:218:-2::0:0:Kerberos KRB-GKRBTGT:/var/empty:/usr/bin/false
_krbadmin:*:219:-2::0:0:Kerberos Admin Service:/var/empty:/usr/bin/false
_krbchangepw:*:220:-2::0:0:Kerberos Change Password Service:/var/empty:/usr/bin/false
_krbkdc:*:221:-2::0:0:Kerberos KDC:/var/empty:/usr/bin/false
_krbkadmin:*:222:-2::0:0:Kerberos KAdmin:/var/empty:/usr/bin/false
_iconservice:*:228:228::0:0:IconService:/var/empty:/usr/bin/false
_distnote:*:241:241::0:0:DistNote:/var/empty:/usr/bin/false
_astris:*:245:245::0:0:Astris Services:/var/db/astris:/usr/bin/false
_krbfast:*:246:-2::0:0:Kerberos FAST Account:/var/empty:/usr/bin/false
_gamecontrollerd:*:247:247::0:0:Game Controller Daemon:/var/empty:/usr/bin/false
_mbsetupuser:*:248:248::0:0:Setup User:/var/setup:/bin/bash
_ondemand:*:249:249::0:0:On Demand Resource Daemon:/var/db/ondemand:/usr/bin/false
_xserverdocs:*:251:251::0:0:WWW Server Docs:/var/empty:/usr/bin/false
_wwwproxy:*:252:252::0:0:WWW Proxy:/var/db/wwwproxy:/usr/bin/false
_mobileasset:*:253:253::0:0:MobileAsset User:/var/ma:/usr/bin/false
_findmydevice:*:254:254::0:0:Find My Device Daemon:/var/db/findmydevice:/usr/bin/false
_datadetectors:*:257:257::0:0:DataDetectors:/var/db/datadetectors:/usr/bin/false
_captiveagent:*:258:258::0:0:captiveagent:/var/empty:/usr/bin/false
_ctkd:*:259:259::0:0:ctkd Account:/var/empty:/usr/bin/false
_applepay:*:260:260::0:0:applepay Account:/var/db/applepay:/usr/bin/false
_hidd:*:261:261::0:0:HID Service User:/var/db/hidd:/usr/bin/false
_cmiodalassistants:*:262:262::0:0:CoreMedia IO Assistants User:/var/db/cmiodalassistants:/usr/bin/false
_analyticsd:*:263:263::0:0:Analytics Daemon:/var/db/analyticsd:/usr/bin/false
_fpsd:*:265:265::0:0:FPS Daemon:/var/db/fpsd:/usr/bin/false
_timed:*:266:266::0:0:Time Sync Daemon:/var/db/timed:/usr/bin/false
_nearbyd:*:268:268::0:0:Proximity and Ranging Daemon:/var/db/nearbyd:/usr/bin/false
_reportmemoryexception:*:269:269::0:0:ReportMemoryException:/var/db/reportmemoryexception:/usr/bin/false
_driverkit:*:270:270::0:0:DriverKit:/var/empty:/usr/bin/false
EOF
    chmod 600 "$ROOTFS/private/etc/master.passwd"
    echo "  synthesized private/etc/master.passwd (stock skeleton)"
fi

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
echo "Next (deb already installed the tweak — no on-device build needed):"
echo "  bash /var/jb/usr/macOS/bin/macos_gui.sh start coexist"
echo "  sudo oslog | grep 'AMFI\|debugbydcmmc\|WindowSer\|MTL\|Metal'"
echo "If dpkg earlier left com.kdt.macosbooter half-configured, re-run:"
echo "  sudo dpkg -i com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb"
