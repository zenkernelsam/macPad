#!/bin/bash
# post_reboot_final2.sh — hardened macOS-cache post-reboot experiment.
# Run ON THE DEVICE as root:  bash /var/mobile/post_reboot_final2.sh
#
# == CORRECTED RECIPE (see docs/porting/CONSOLIDATED-2026-09-29.md §8) ==
#   * macOS cache  -> plain dyld (dyld_plat: crossarch+plataccept ONLY).
#                     DO NOT touch sf_slide (0x20000000 is valid) and DO NOT
#                     strip VM_PROT_SLIDE — both are disproven/self-harm.
#   * cachereg attaches the natural cs_blob. NEVER run set_blob_cov on macOS
#     (full-file coverage => engine EINVAL, proven Sep 28/29).
#   * Region state is checked with /tmp/scheck (check_np ONLY — sprobe itself
#     no dyld involved) so the check itself cannot consume the clean region.
#
# Ordering rationale: baseline/restore_env run BEFORE cachereg -> their dyld
# execs either find no cache (chroot has no /private/preboot, no
# com.apple.dyld dir) or get a clean setup-time EINVAL that commits nothing.
# The FIRST cache-mapping attempt that can succeed is the instrumented one
# in step 6, after blobs are attached.

PAC=/var/mobile
R=/var/mnt/rootfs
LD=/var/jb/usr/bin/ldid
JB=/var/jb/basebin/jbctl
PY=/var/jb/usr/bin/python3
ENT=/var/jb/usr/macOS/bin/entitlements.plist
TO=/var/jb/usr/bin/timeout
MCD_DEV=$R/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld
MCD_CHR=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld

hr(){ echo; echo "===== $* ====="; }
tc(){ for H in $($PY $PAC/nm/cdhash_slices.py "$1" 2>/dev/null | awk '{print $3}'); do
        $JB trustcache add "$H" >/dev/null 2>&1
      done; }
deploy(){
  $LD -Hsha256 -S$ENT "$1" 2>/dev/null
  tc "$1"
  rm -f $R/usr/lib/dyld
  cp "$1" $R/usr/lib/dyld
  chmod 755 $R/usr/lib/dyld
  echo "  dyld <- $(basename $1)  md5=$(md5sum $R/usr/lib/dyld | cut -c1-12)"
}
r(){  # r <tag> <cmd...>  — DYLD_SHARED_CACHE_DIR pinned to the macOS cryptex dir
  t=$1; shift
  /var/jb/usr/bin/env -i PATH=/usr/bin:/bin DYLD_SHARED_CACHE_DIR=$MCD_CHR \
    $TO 25 "$@" >/tmp/f_$t.out 2>/tmp/f_$t.err
  rc=$?
  nmap=$(grep -ac "Using mapping in dyld cache" /tmp/f_$t.err 2>/dev/null)
  nnl=$(grep -ac "not loaded" /tmp/f_$t.err 2>/dev/null)
  echo "  [$t] rc=$rc  Using-map=$nmap  notloaded=$nnl  out=$(head -c40 /tmp/f_$t.out|tr -d '\n')"
  grep -a "not loaded\|wrong platform\|Reason:\|Symbol not found\|Library not loaded\|re-using existing" /tmp/f_$t.err 2>/dev/null | head -3 | cut -c1-110
  return $rc
}

# ============ 0 dependency check ============
hr "0 依赖检查"
ok=1
for f in $LD $JB $ENT $PY $TO \
         $PAC/cachereg $PAC/restore_env.sh $PAC/nm/cdhash_slices.py \
         $PAC/dyld_probe_noC.bin $PAC/dyld_plat.bin \
         $R/tmp/scheck \
         $MCD_DEV/dyld_shared_cache_arm64e $MCD_DEV/dyld_shared_cache_arm64e.01; do
  if [ -e "$f" ]; then echo "  ok $f"; else echo "  MISS $f"; ok=0; fi
done
[ $ok -eq 0 ] && { echo "FATAL: missing deps"; exit 1; }

# scheck needs TC entries for its own exec inside chroot
tc $R/tmp/scheck

# ============ 1 environment ============
hr "1 trustcache + 基线 dyld (restore_env; 自检 HELLO 无 env=不灌缓存)"
RESTORE_NO_VERIFY=1 bash $PAC/restore_env.sh $PAC/dyld_probe_noC.bin 2>&1 | tail -10
# (run_nocskill kernel-slide scan is broken right after this reboot;
#  its HELLO self-check cannot work — verification happens in step 3)

# ============ 2 region state (freestanding probe, no dyld) ============
hr "2 clean region 验证 (scheck: 纯 check_np,dyld 找不到缓存=不灌)"
/var/jb/usr/bin/env -i PATH=/usr/bin:/bin $TO 15 /var/jb/usr/bin/chroot $R /tmp/scheck   >/tmp/f_sp.out 2>/tmp/f_sp.err
rc=$?
SP=$(grep -a "scheck ret=" /tmp/f_sp.out /tmp/f_sp.err 2>/dev/null | head -1)
echo "  scheck rc=$rc  $SP"
case "$SP" in
  *"ret=-12"*) echo "  -> region EMPTY (clean) ✓ 干净实验窗口";;
  *"ret=0"*)   echo "  -> !! region 已 populated,base 见上 — 后续复用已灌内容";;
  *"ret=-22"*) echo "  -> !! 无 region(异常,chroot exec 未建区)";;
  *)           echo "  -> ?? 输出无法解析,看 /tmp/f_sp.out";;
esac

# ============ 3 baseline (no env) ============
hr "3 基线 echo (无 env,验证 rootfs 本身;不得消耗 region)"
$TO 20 /var/jb/usr/bin/chroot $R /bin/echo BASE_OK >/tmp/f_b.out 2>/tmp/f_b.err
echo "  baseline rc=$? out=$(cat /tmp/f_b.out)"

# ============ 4 cachereg ============
hr "4 cachereg: 注册 macOS 缓存自然 blob"
killall -9 cachereg 2>/dev/null; sleep 0.3
grep -a . /tmp/cr.log 2>/dev/null && mv /tmp/cr.log /tmp/cr.log.old
( /var/jb/usr/bin/env -i $PAC/cachereg \
    $MCD_DEV/dyld_shared_cache_arm64e \
    $MCD_DEV/dyld_shared_cache_arm64e.01 \
    >/tmp/cr.log 2>&1 & )
sleep 3
echo "  REG lines: $(grep -ac '^REG' /tmp/cr.log)"
grep -a "^REG\|READY\|ERR" /tmp/cr.log | head -8
grep -aq "READY" /tmp/cr.log || echo "  !! cachereg 未 READY — blob 可能没挂上,536 多半 EINVAL"

# NOTE: NO scheck/exec between here and step 6. After blobs are attached,
# ANY chroot exec whose dyld finds the /System/Library/dyld symlink can do
# a REAL populate — we want the first successful populate to be the
# instrumented dyld_plat test below, not an un-instrumented probe.

# ============ 6 dyld_plat + macOS cache ============
hr "6 部署 dyld_plat + 正式映射测试 (首个能成功的 populate)"
deploy $PAC/dyld_plat.bin
r e1 /var/jb/usr/bin/chroot $R /bin/echo HELLO
r e2 /var/jb/usr/bin/chroot $R /bin/echo HELLO
r e3 /var/jb/usr/bin/chroot $R /bin/echo HELLO

hr "6b cat / ls / sh -c"
r c1 /var/jb/usr/bin/chroot $R /bin/cat /etc/hosts
r l1 /var/jb/usr/bin/chroot $R /bin/ls /bin
r s1 /var/jb/usr/bin/chroot $R /bin/sh -c "echo IN_SH"

# ============ 7 libSystem source ============
hr "7 libSystem 来源 (期望 D161E41A=macOS 缓存; B90391D8=shim 为坏)"
$TO 25 /var/jb/usr/bin/env -i PATH=/usr/bin:/bin DYLD_SHARED_CACHE_DIR=$MCD_CHR DYLD_PRINT_LIBRARIES=1 \
  /var/jb/usr/bin/chroot $R /bin/cat /etc/hosts >/dev/null 2>/tmp/f_pc.err
echo "  cat rc=$?"
grep -a "libSystem.B.dylib" /tmp/f_pc.err | head -3 | cut -c1-120

# ============ 8 rollback ============
hr "8 收尾:回滚基线 dyld_probe_noC"
deploy $PAC/dyld_probe_noC.bin
$TO 20 /var/jb/usr/bin/chroot $R /bin/echo FINAL_OK 2>/dev/null || echo "  (收尾自检失败)"
hr "done — 证据在 /tmp/f_*.err /tmp/f_*.out /tmp/cr.log"
