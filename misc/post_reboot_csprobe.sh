#!/var/jb/usr/bin/bash
# post_reboot_csprobe.sh — run ON THE DEVICE (root) after reboot+re-jailbreak.
# Restores trustcache for the current private-path experiment, then runs the
# frozen-exception inspection: hold child at the KERN_CODESIGN_ERROR and
# kread the vnode/object/blob state via csprobe.py.
set -u
R=/var/mnt/rootfs
PY=/var/jb/usr/bin/python3
JBCTL=/var/jb/basebin/jbctl
LDID=/var/jb/usr/bin/ldid
LOG=/var/mobile/post_reboot_csprobe.log
CDHASH_PY=/var/mobile/nm/cdhash_slices.py
[ -f "$CDHASH_PY" ] || CDHASH_PY=/var/mobile/cdhash_slices.py
say() { echo "$@" | tee -a "$LOG"; }
add_tc() { "$JBCTL" trustcache add "$1" >/dev/null 2>&1 || true; say "  TC $1 hit=$($JBCTL trustcache info 2>/dev/null | tr -c '[:alnum:]' ' ' | grep -oic "$1") ($2)"; }
add_tc_file() {
    local f="$1" lbl="$2" h
    [ -f "$f" ] || { say "  MISSING $f ($lbl)"; return 1; }
    for h in $("$PY" "$CDHASH_PY" "$f" 2>/dev/null | awk '{print $3}'); do add_tc "$h" "$lbl"; done
    h=$("$LDID" -h "$f" 2>/dev/null | sed -n 's/.*CDHash=\([0-9a-f]*\).*/\1/p' | head -1)
    [ -n "$h" ] && add_tc "$h" "$lbl(ldid)"
    return 0
}

say "########## post_reboot_csprobe $(date) ##########"
[ -x "$JBCTL" ] || { say "FATAL: jbctl missing -> re-jailbreak"; exit 10; }
"$JBCTL" trustcache info >/dev/null 2>&1 || { say "FATAL: jbctl unavailable -> re-jailbreak"; exit 11; }

say "=== 1. trustcache: chroot-loaded Mach-Os via restore_env.sh ==="
RESTORE_NO_VERIFY=1 /var/jb/usr/bin/bash /var/mobile/restore_env.sh 2>&1 | tail -8 | tee -a "$LOG"

say "=== 2. trustcache: helpers + currently-deployed dyld ==="
for f in /var/mobile/run_dbg_hold_v2 /var/jb/usr/bin/chroot "$R/usr/lib/dyld"; do
    add_tc_file "$f" "$(basename "$f")"
done

say "=== 3. frozen-exception inspection ==="
[ -f /var/mobile/csprobe.py ] || { say "FATAL: /var/mobile/csprobe.py not deployed"; exit 13; }
: > /tmp/rdh_probe.out
( RUN_DBG_HOLD=30 /var/mobile/run_dbg_hold_v2 /var/jb/usr/bin/chroot "$R" /bin/echo TT >/tmp/rdh_probe.out 2>&1 & )
PID=""
for i in $(seq 1 25); do
    sleep 1
    PID=$(grep -a "spawned pid" /tmp/rdh_probe.out 2>/dev/null | grep -oE "[0-9]+" | head -1)
    [ -n "$PID" ] && grep -a "exception msg\|holding child" /tmp/rdh_probe.out >/dev/null 2>&1 && break
done
say "  child pid=$PID"
if [ -n "$PID" ]; then
    "$PY" /var/mobile/csprobe.py "$PID" 2>&1 | tee -a "$LOG"
fi
say "=== 4. runner output tail ==="
grep -aE "\[exc\]|\[vmext\]|\[pcmap\]|csops|SIG" /tmp/rdh_probe.out | head -20 | tee -a "$LOG"
say "########## done; log: $LOG ##########"
