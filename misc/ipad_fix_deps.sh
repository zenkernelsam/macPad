#!/bin/sh
# ipad_fix_deps.sh — one-shot iPad fixer for the macPad install.
# POSIX-only on purpose: this script must run on a minimal bootstrap where
# bash itself may not be installed yet (Filza "execute" uses /bin/sh).
#
# Two jobs:
#   1. (with --uninstall) remove the half-configured com.kdt.macosbooter deb
#      so Sileo/apt stop queueing other packages behind its failed postinst.
#   2. Install the apt dependencies the toolchain needs, one package at a
#      time so a missing repo name never aborts the rest; then create
#      fallback shims for tools no repo carries (strings, chflags).
#
# Usage (as root, in any terminal — Filza/NewTerm/SSH all fine):
#   sudo bash ipad_fix_deps.sh                # just install deps + shims
#   sudo bash ipad_fix_deps.sh --uninstall    # remove deb first, then deps
#   sudo bash ipad_fix_deps.sh --uninstall-only
set -u

export PATH="/var/jb/usr/bin:/var/jb/usr/sbin:/var/jb/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin:$PATH"
PKG=com.kdt.macosbooter
MODE=deps
[ "${1:-}" = "--uninstall" ] && MODE=all
[ "${1:-}" = "--uninstall-only" ] && MODE=uninstall

if [ "$MODE" != "deps" ]; then
    echo "=== removing $PKG (clears Sileo's blocked queue) ==="
    # Stop any running macPad launchd jobs / processes so nothing holds files.
    for p in /var/jb/usr/macOS/LaunchDaemons/*.plist \
             /var/jb/Library/LaunchDaemons/com.macwsguide.*.plist; do
        [ -f "$p" ] && launchctl unload "$p" 2>/dev/null
    done
    pkill -f WindowServer 2>/dev/null; pkill -f OSXvnc 2>/dev/null
    pkill -f macws 2>/dev/null; pkill -f launchservicesd 2>/dev/null
    sleep 1
    dpkg -r "$PKG" 2>/dev/null || dpkg -P "$PKG" 2>/dev/null || \
        dpkg --force-all -P "$PKG" 2>/dev/null || {
            echo "WARN: dpkg removal failed — trying status-file repair"
        }
    # Clear any other half-configured packages so apt is usable again.
    dpkg --configure -a 2>/dev/null || true
    apt-get -f install -y 2>/dev/null || true
    dpkg -l "$PKG" 2>/dev/null | grep -q "^.i\|^iU" \
        && echo "still present: $PKG" || echo "$PKG removed / clean"
    [ "$MODE" = "uninstall-only" ] && exit 0
fi

echo "=== installing apt dependencies (one by one; a bad name only skips itself) ==="
apt update 2>/dev/null || apt-get update 2>/dev/null || true
# name -> what it provides on the device
for pkg in python3 ldid coreutils grep gawk findutils tar binutils uikittools file-cmds; do
    if apt install -y "$pkg" 2>/dev/null || apt-get install -y "$pkg" 2>/dev/null; then
        echo "  ok: $pkg"
    else
        echo "  SKIP: $pkg (not in your repos — see shims below)"
    fi
done

echo "=== fallback shims for tools with no package ==="
# strings(1): postinst uses `ldid -q <macho> | strings` to read the embedded
# requirements XML. binutils may be absent on this repo — a 4-char printable
# run extractor is equivalent for that use.
if ! command -v strings >/dev/null 2>&1; then
    cat > /var/jb/usr/local/bin/strings <<'PYEOF'
#!/bin/sh
exec /var/jb/usr/bin/python3 -c '
import sys, re
data = sys.stdin.buffer.read() if len(sys.argv) < 2 else open(sys.argv[1], "rb").read()
for m in re.finditer(rb"[ -~]{4,}", data):
    print(m.group().decode("ascii", "replace"))
' "$@"
PYEOF
    chmod 755 /var/jb/usr/local/bin/strings
    echo "  shimmed: /var/jb/usr/local/bin/strings (python3)"
fi

# chflags(1): only used to set the `restricted` flag on two QuickLook appex.
# Stock iOS ships /usr/bin/chflags — link it into the jb prefix if the
# procursus copy is absent.
if [ ! -x /var/jb/usr/bin/chflags ] && [ -x /usr/bin/chflags ]; then
    mkdir -p /var/jb/usr/bin
    ln -sf /usr/bin/chflags /var/jb/usr/bin/chflags
    echo "  linked: /var/jb/usr/bin/chflags -> /usr/bin/chflags"
fi
# Same trick for any other tool that exists at the system path.
for t in lipo plutil; do
    [ -x "/var/jb/usr/bin/$t" ] || {
        for c in "/usr/bin/$t" "/usr/sbin/$t" "/bin/$t" "/sbin/$t"; do
            if [ -x "$c" ]; then
                mkdir -p /var/jb/usr/bin
                ln -sf "$c" "/var/jb/usr/bin/$t"
                echo "  linked: /var/jb/usr/bin/$t -> $c"
                break
            fi
        done
    }
done

echo
echo "=== verification ==="
FAIL=0
for t in /var/jb/usr/bin/python3 /var/jb/usr/bin/ldid /var/jb/usr/bin/grep \
         /var/jb/usr/bin/jbctl; do
    if [ -x "$t" ]; then echo "  ok $t"; else echo "  MISSING $t"; FAIL=1; fi
done
for t in tar strings cut timeout realpath chflags; do
    if command -v "$t" >/dev/null 2>&1; then echo "  ok $t"; else echo "  missing $t (may still be fine)"; fi
done
echo
if [ "$FAIL" = 0 ]; then
    echo "ALL CRITICAL TOOLS PRESENT — now run:"
    echo "  sudo dpkg -i com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb"
    echo "  sudo bash install_rootfs_15.sh macos-15.6.1-rootfs.tar"
else
    echo "install the MISSING items above (apt), then re-run this script."
fi
