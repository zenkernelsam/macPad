#!/var/jb/usr/bin/bash
# post_reboot_cli_test.sh -- run ON THE DEVICE (iOS side) after a FULL REBOOT.
#
# Why: the kernel's shared region is created once from the cache's declared size
# and then persists across processes.  Patching the cache files therefore has no
# effect until the region is recreated, i.e. until a reboot
# (docs/porting/STATIC-cache-layout-exceeds-4gb-shared-region.md  SS10).
#
# Preconditions (must already be true on disk BEFORE the reboot):
#   * both main-cache copies patched with:
#       sharedRegionSize(0xE8) = 0x100000000
#       subCacheArrayCount(0x18C) = 0
#       dynamicDataOffset(0x1F0)  = 0x77080000
#     at: $R/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e
#         $R/private/tmp/dsc/dyld_shared_cache_arm64e
#   * both .01 copies patched: m2 size -> so its end == 0x280000000, m3..m6 size -> 0
#   * /var/mobile/dyld_f1_34790.bin (signed F1 dyld, SHA 14e2751b..., CDHash cd023af6...)
#   * /var/mobile/dyld_f1v2_backup_orig.bin (pristine copy of the original dyld)
#
# This script re-establishes the volatile state (trustcache, cachereg fds, devfs),
# deploys F1, runs the CLI witness, then restores the pristine dyld and verifies it.
set -u
R=/var/mnt/rootfs
CR="$R/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"
DST="$R/private/tmp/dsc"
PY=/var/jb/usr/bin/python3
JBCTL=/var/jb/basebin/jbctl
EXPECT_DYLD=9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1
LOG=/var/mobile/post_reboot_cli_test.log
sha() { "$PY" -c 'import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }
say() { echo "$@" | tee -a "$LOG"; }

say "=== 1. cache patch state ==="
"$PY" - <<'PY' | tee -a "$LOG"
import struct
for p in ('/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e',
          '/var/mnt/rootfs/private/tmp/dsc/dyld_shared_cache_arm64e'):
    d=open(p,'rb').read(0x240)
    print('  MAIN %-70s size=%#x subC=%d ddo=%#x' % (p.split('/dyld/')[-1], struct.unpack_from('<Q',d,0xE8)[0],
          struct.unpack_from('<I',d,0x18C)[0], struct.unpack_from('<Q',d,0x1F0)[0]))
for p in ('/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.01',
          '/var/mnt/rootfs/private/tmp/dsc/dyld_shared_cache_arm64e.01'):
    d=open(p,'rb').read(0x400); mo=struct.unpack_from('<I',d,0x10)[0]
    sizes=[struct.unpack_from('<Q',d,mo+i*32+8)[0] for i in range(7)]
    ends=[struct.unpack_from('<Q',d,mo+i*32)[0]+sizes[i] for i in range(7)]
    print('  .01  %-70s sizes=%s maxend=%#x' % (p.split('/dyld/')[-1], [hex(s) for s in sizes], max(ends)))
PY

say "=== 2. dyld state ==="
say "  dyld_sha=$(sha "$R/usr/lib/dyld")  (expected $EXPECT_DYLD before F1)"

say "=== 3. volatile state: trustcache + devfs ==="
RESTORE_NO_VERIFY=1 /var/jb/usr/bin/bash /var/mobile/restore_env.sh >/var/mobile/pre_reboot_restore.log 2>&1
say "  restore_env rc=$? (tail: $(tail -1 /var/mobile/pre_reboot_restore.log))"
if [ ! -d "$R/dev" ]; then mkdir -p "$R/dev" && chown 0:0 "$R/dev" && chmod 0755 "$R/dev"; fi
/var/jb/usr/macOS/bin/mountdevfs "$R/dev" >>"$LOG" 2>&1 || true
say "  ptmx: $(ls -l "$R/dev/ptmx" 2>&1)"

say "=== 4. cachereg the HOLD of the patched caches (fds must stay open) ==="
for f in "$CR/dyld_shared_cache_arm64e" "$CR/dyld_shared_cache_arm64e.01" "$DST/dyld_shared_cache_arm64e" "$DST/dyld_shared_cache_arm64e.01"; do
    ( nohup /var/mobile/cachereg "$f" >"/var/mobile/cachereg_post_$(basename $(dirname $f)).log" 2>&1 & )
    sleep 2
    say "  $(basename "$f"): $(tail -1 "/var/mobile/cachereg_post_$(basename $(dirname $f)).log" 2>/dev/null)"
done

say "=== 5. deploy F1 dyld + run CLI witness ==="
cp /var/mobile/dyld_f1_34790.bin "$R/usr/lib/.f1stage_pr" && chmod 755 "$R/usr/lib/.f1stage_pr"
mv "$R/usr/lib/dyld" "$R/usr/lib/.f1orig_pr" && mv "$R/usr/lib/.f1stage_pr" "$R/usr/lib/dyld"
say "  f1_sha=$(sha "$R/usr/lib/dyld")"
DYLD_PRINT_LIBRARIES=1 timeout 90 /var/mobile/run_dbg_hold_v2 /var/jb/usr/bin/chroot "$R" /bin/echo HI >/var/mobile/post_reboot_echo.out 2>/var/mobile/post_reboot_echo.raw
say "  RUN_RC=$?"
"$PY" - <<'PY' | tee -a "$LOG"
o=open('/var/mobile/post_reboot_echo.out','rb').read(); d=open('/var/mobile/post_reboot_echo.raw','rb').read()
print('  OUT bytes=%d repr=%r' % (len(o), o[:200]))
for l in d.split(b'\n'):
    if l.startswith(b'[') and (b'exc]' in l or b'vm]' in l): print('   ', repr(l[:130]))
    if b'libSystem.B.dylib' in l: print('    LIB', repr(l[:120]))
PY

say "=== 6. restore pristine dyld ==="
mv "$R/usr/lib/dyld" "$R/usr/lib/.f1tested_pr" && mv "$R/usr/lib/.f1orig_pr" "$R/usr/lib/dyld"
say "  restored_sha=$(sha "$R/usr/lib/dyld")  (must equal $EXPECT_DYLD)"
say "=== done; full log: $LOG ==="
