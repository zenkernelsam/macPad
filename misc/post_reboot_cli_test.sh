#!/var/jb/usr/bin/bash
# post_reboot_cli_test.sh -- run ON THE DEVICE (iOS side, root) AFTER a full reboot.
#
# Why a reboot: the kernel's shared region is created once from the cache's
# declared size and then persists across processes, so the cache-header patch
# only takes effect after the region is recreated (= a reboot).
# Evidence/procedure: docs/porting/STATIC-cache-layout-exceeds-4gb-shared-region.md
#
# What it does, in order (all idempotent, mv-only, nothing deleted):
#   0. preflight: jbctl alive + every required file present
#   1. verify the 4 GB-layout cache patch is still in place (field values)
#   2. restore the volatile trustcache: the 4 macOS-side files (restore_env.sh)
#      PLUS every iOS-side helper this test needs (run_dbg_hold_v2, cachereg,
#      mountdevfs, chroot) PLUS the F1 dyld PLUS the two cache CDHashes.
#      This is essential: the jailbreak trustcache is wiped on every reboot and
#      restore_env.sh alone does not cover the helpers.
#   3. devfs: mkdir + mountdevfs, assert /dev/ptmx (schells/ptys need it)
#   4. cachereg holds on the four patched cache files (fds stay open)
#   5. deploy the signed F1 dyld, run the CLI witness
#      (/var/mobile/run_dbg_hold_v2 /var/jb/usr/bin/chroot $R /bin/echo HI)
#   6. restore the pristine dyld and verify SHA + inode
#
# Verdict: if the EXC_GUARD with code1 0x2ac75c000 is gone and HI prints, the
# 4 GB-layout fix works and the CLI milestone is reached.
set -u
R=/var/mnt/rootfs
CR="$R/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"
DST="$R/private/tmp/dsc"
PY=/var/jb/usr/bin/python3
JBCTL=/var/jb/basebin/jbctl
CDHASH_PY=/var/mobile/nm/cdhash_slices.py
LDID=/var/jb/usr/bin/ldid
F1=/var/mobile/dyld_f1_34790.bin
F1_SHA_EXPECT=14e2751b3f6e54c7fd617206c181192ed30773fdc7f27caccdcddc7d85c0797f
F1_CDHASH_KNOWN=cd023af61cd044d269a6bb89380c80f5b15979dc
EXPECT_DYLD_SHA=9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1
EXPECT_DYLD_INODE=245791518
CACHE_CDHASHES="2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e 8c7ba7e588b0edd43f7334e2de11688cd4732192"
HELPERS="/var/mobile/run_dbg_hold_v2 /var/mobile/cachereg /var/jb/usr/macOS/bin/mountdevfs /var/jb/usr/bin/chroot"
TAG="pr$$"
LOG=/var/mobile/post_reboot_cli_test.log
say() { echo "$@" | tee -a "$LOG"; }
sha() { "$PY" -c 'import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }
add_tc() { # add_tc <hex40> <label>
    [ -n "${1:-}" ] || return 0
    "$JBCTL" trustcache add "$1" >/dev/null 2>&1 || true
    local hit
    hit=$("$JBCTL" trustcache info 2>/dev/null | tr -c '[:alnum:]' ' ' | grep -oic "$1")
    say "    TC $1 hit=$hit  ($2)"
}
add_tc_file() { # add_tc_file <path> <label>  -- every slice via cdhash_slices.py, plus ldid -h
    local f="$1" lbl="$2" h
    [ -f "$f" ] || { say "    MISSING $f ($lbl)"; return 1; }
    for h in $("$PY" "$CDHASH_PY" "$f" 2>/dev/null | awk '{print $3}'); do add_tc "$h" "$lbl"; done
    h=$("$LDID" -h "$f" 2>/dev/null | sed -n 's/.*CDHash=\([0-9a-f]*\).*/\1/p' | head -1)
    [ -n "$h" ] && add_tc "$h" "$lbl(ldid)"
    return 0
}

say "########## post_reboot_cli_test $(date) ##########"

say "=== 0. preflight ==="
[ -x "$JBCTL" ] || { say "FATAL: $JBCTL missing -> re-jailbreak (Dopamine) first"; exit 10; }
"$JBCTL" trustcache info >/dev/null 2>&1 || { say "FATAL: jbctl trustcache unavailable -> re-jailbreak first"; exit 11; }
for f in "$F1" "$PY" "$CDHASH_PY" "$LDID" /var/mobile/restore_env.sh; do
    [ -e "$f" ] || { say "FATAL: missing $f"; exit 12; }
done
say "  ok: jbctl + F1 + python + cdhash tool + ldid + restore_env.sh present"

say "=== 1. cache patch state (expect size=0x100000000 subC=0 ddo=0x77080000; .01 maxend<=0x280000000) ==="
"$PY" - <<'PY' | tee -a "$LOG"
import struct,os
ok=True
for p in ('/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e',
          '/var/mnt/rootfs/private/tmp/dsc/dyld_shared_cache_arm64e',
          '/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dsc_main_orig'):
    try:
        d=open(p,'rb').read(0x240)
    except Exception as e:
        print('  MISSING %s (%s)'%(p,e)); ok=False; continue
    sz,sc,dd=struct.unpack_from('<Q',d,0xE8)[0],struct.unpack_from('<I',d,0x18C)[0],struct.unpack_from('<Q',d,0x1F0)[0]
    good = (sz==0x100000000 and sc==0 and dd==0x77080000)
    ok &= good
    print('  %-4s %-70s size=%#x subC=%d ddo=%#x inode=%d'%('OK' if good else 'BAD',p.replace('/var/mnt/rootfs',''),sz,sc,dd,os.stat(p).st_ino))
for p in ('/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.01',
          '/var/mnt/rootfs/private/tmp/dsc/dyld_shared_cache_arm64e.01'):
    try:
        d=open(p,'rb').read(0x400)
    except Exception as e:
        print('  MISSING %s (%s)'%(p,e)); ok=False; continue
    mo=struct.unpack_from('<I',d,0x10)[0]
    sizes=[struct.unpack_from('<Q',d,mo+i*32+8)[0] for i in range(7)]
    addrs=[struct.unpack_from('<Q',d,mo+i*32)[0] for i in range(7)]
    eff=max(addrs[i]+sizes[i] for i in range(7) if sizes[i])
    good = (sizes[3]==0 and sizes[4]==0 and sizes[5]==0 and sizes[6]==0 and eff<=0x280000000)
    ok &= good
    print('  %-4s %-70s sizes=%s effEnd=%#x inode=%d'%('OK' if good else 'BAD',p.replace('/var/mnt/rootfs',''),[hex(s) for s in sizes],eff,os.stat(p).st_ino))
print('  PATCH_STATE=%s'%('OK' if ok else 'BAD'))
PY

say "=== 2. trustcache restore (macOS side via restore_env.sh + helpers + F1 + cache cdhashes) ==="
RESTORE_NO_VERIFY=1 /var/jb/usr/bin/bash /var/mobile/restore_env.sh >/var/mobile/post_reboot_restore.log 2>&1
say "  restore_env rc=$? tail=$(tail -1 /var/mobile/post_reboot_restore.log)"
for h in $CACHE_CDHASHES; do add_tc "$h" "dyld shared cache (24G90)"; done
for f in $HELPERS; do add_tc_file "$f" "helper $(basename "$f")"; done
say "  F1 sha=$(sha "$F1") (expect $F1_SHA_EXPECT)"
add_tc "$F1_CDHASH_KNOWN" "F1 dyld (known cdhash)"
add_tc_file "$F1" "F1 dyld"

say "=== 3. devfs / ptmx ==="
[ -d "$R/dev" ] || { mkdir -p "$R/dev" && chown 0:0 "$R/dev" && chmod 0755 "$R/dev"; }
/var/jb/usr/macOS/bin/mountdevfs "$R/dev" >>"$LOG" 2>&1 || say "  mountdevfs rc=$?"
say "  ptmx: $(ls -l "$R/dev/ptmx" 2>&1)"

say "=== 4. cachereg holds on the patched caches ==="
i=0
for f in "$CR/dyld_shared_cache_arm64e" "$CR/dyld_shared_cache_arm64e.01" "$DST/dyld_shared_cache_arm64e" "$DST/dyld_shared_cache_arm64e.01"; do
    i=$((i+1)); lg="/var/mobile/post_reboot_cachereg_${i}.log"
    ( nohup /var/mobile/cachereg "$f" >"$lg" 2>&1 & )
    sleep 4
    say "  [$i] $(basename "$(dirname "$f")")/$(basename "$f"): $(tail -1 "$lg" 2>/dev/null)"
done

say "=== 5. deploy F1 + run the CLI witness ==="
say "  pre-deploy dyld sha=$(sha "$R/usr/lib/dyld")"
cp "$F1" "$R/usr/lib/.f1stage_$TAG" && chmod 755 "$R/usr/lib/.f1stage_$TAG"
mv "$R/usr/lib/dyld" "$R/usr/lib/.f1orig_$TAG" && mv "$R/usr/lib/.f1stage_$TAG" "$R/usr/lib/dyld"
say "  f1_sha=$(sha "$R/usr/lib/dyld")  (expect $F1_SHA_EXPECT)"
DYLD_PRINT_LIBRARIES=1 timeout 90 /var/mobile/run_dbg_hold_v2 /var/jb/usr/bin/chroot "$R" /bin/echo HI >/var/mobile/post_reboot_echo.out 2>/var/mobile/post_reboot_echo.raw
say "  RUN_RC=$?"
"$PY" - <<'PY' | tee -a "$LOG"
o=open('/var/mobile/post_reboot_echo.out','rb').read(); d=open('/var/mobile/post_reboot_echo.raw','rb').read()
print('  OUT bytes=%d repr=%r' % (len(o), o[:220]))
guard=0
for l in d.split(b'\n'):
    if l.startswith(b'[') and (b'exc]' in l or b'vm]' in l):
        print('   ', repr(l[:130]))
        if b'code1=0x2ac75c000' in l: guard+=1
    if b'libSystem.B.dylib' in l: print('    LIB', repr(l[:120]))
print('  GUARD_2ac75c000_HITS=%d' % guard)
print('  VERDICT=%s' % ('FIXED-please-verify-HI-printed' if guard==0 else 'STILL-BLOCKED-same-guard'))
PY

say "=== 6. restore pristine dyld ==="
if [ -e "$R/usr/lib/.f1orig_$TAG" ]; then
    mv "$R/usr/lib/dyld" "$R/usr/lib/.f1tested_$TAG" && mv "$R/usr/lib/.f1orig_$TAG" "$R/usr/lib/dyld"
fi
now_sha=$(sha "$R/usr/lib/dyld")
now_inode=$("$PY" -c 'import os;print(os.stat("/var/mnt/rootfs/usr/lib/dyld").st_ino)')
say "  restored sha=$now_sha (expect $EXPECT_DYLD_SHA)"
say "  restored inode=$now_inode (expect $EXPECT_DYLD_INODE)"
[ "$now_sha" = "$EXPECT_DYLD_SHA" ] && say "  DYLD_RESTORE=OK" || say "  DYLD_RESTORE=MISMATCH"
say "########## done; full log: $LOG ##########"
