#!/var/jb/usr/bin/bash
# apply_4gb_layout_patch.sh [apply|restore] -- run ON THE DEVICE (iOS side), root.
#
# Applies (or restores) the "make the 15.6.1 cache layout fit inside the 4 GB
# iOS shared region" patch set.  Rationale + evidence:
#   docs/porting/STATIC-cache-layout-exceeds-4gb-shared-region.md
#
# Patch set (per file, always via a NEW INODE so the kernel cannot serve stale
# pages/blobs; the project learned that lesson the hard way):
#   main cache  : sharedRegionSize(0xE8)=0x100000000, subCacheArrayCount(0x18C)=0,
#                 dynamicDataOffset(0x1F0)=0x77080000  (VM 0x1f7080000, in-region hole)
#   sub cache   : mapping[2].size -> (0x280000000 - mapping[2].address) so its end is
#                 exactly the region end, and mapping[3..6].size -> 0
#
# Files touched:
#   $CR/dyld_shared_cache_arm64e, $DST/dyld_shared_cache_arm64e, $CR/dsc_main_orig   (main)
#   $CR/dyld_shared_cache_arm64e.01, $DST/dyld_shared_cache_arm64e.01               (sub)
#
# The kernel's shared region is created once (from the cache's declared size) and
# persists across processes, so this patch only takes effect after a REBOOT.
set -u
MODE="${1:-apply}"
R=/var/mnt/rootfs
CR="$R/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld"
DST="$R/private/tmp/dsc"
PY=/var/jb/usr/bin/python3
TAG=v8

MAINS=("$CR/dyld_shared_cache_arm64e" "$DST/dyld_shared_cache_arm64e" "$CR/dsc_main_orig")
SUBS=("$CR/dyld_shared_cache_arm64e.01" "$DST/dyld_shared_cache_arm64e.01")

patch_main() { "$PY" - "$1" "$2" <<'PY'
import struct,sys
p,out=sys.argv[1],sys.argv[2]
f=open(p,'r+b')
f.seek(0xE8); f.write(struct.pack('<Q',0x100000000))
f.seek(0x18C); f.write(struct.pack('<I',0))
f.seek(0x1F0); f.write(struct.pack('<Q',0x77080000))
f.flush(); f.close()
d=open(p,'rb').read(0x230)
print('  patched %-58s size=%#x subC=%d ddo=%#x' % (out, struct.unpack_from('<Q',d,0xE8)[0],
      struct.unpack_from('<I',d,0x18C)[0], struct.unpack_from('<Q',d,0x1F0)[0]))
PY
}

patch_sub() { "$PY" - "$1" "$2" <<'PY'
import struct,sys
p,out=sys.argv[1],sys.argv[2]
h=open(p,'rb').read(0x400); mo=struct.unpack_from('<I',h,0x10)[0]
a2=struct.unpack_from('<Q',h,mo+2*32)[0]
f=open(p,'r+b')
f.seek(mo+2*32+8); f.write(struct.pack('<Q',0x280000000-a2))
for i in (3,4,5,6):
    f.seek(mo+i*32+8); f.write(struct.pack('<Q',0))
f.flush(); f.close()
d=open(p,'rb').read(0x400)
sizes=[struct.unpack_from('<Q',d,mo+i*32+8)[0] for i in range(7)]
ends=[struct.unpack_from('<Q',d,mo+i*32)[0]+sizes[i] for i in range(7)]
print('  patched %-58s sizes=%s maxend=%#x' % (out, [hex(s) for s in sizes], max(ends)))
PY
}

swap_in() {   # $1 = target path ; $2 = patcher function
    local tgt="$1" patcher="$2" d b s
    d="$(dirname "$tgt")"; b=".$TAG.orig.$(basename "$tgt")"; s=".$TAG.stage.$(basename "$tgt")"
    [ -e "$d/$b" ] && { echo "  SKIP (already swapped): $tgt"; return 0; }
    cp "$tgt" "$d/$s" || { echo "  CPFAIL $tgt"; return 1; }
    "$patcher" "$d/$s" "${tgt#$R}"
    chmod 755 "$d/$s"; chown 0:0 "$d/$s"
    mv "$tgt" "$d/$b" && mv "$d/$s" "$tgt" || { echo "  MVFAIL $tgt"; return 1; }
    echo "  deployed inode=$("$PY" -c "import os,sys;print(os.stat(sys.argv[1]).st_ino)" "$tgt")  $tgt"
    ( nohup /var/mobile/cachereg "$tgt" >"/var/mobile/cachereg_${TAG}_$(basename "$tgt").log" 2>&1 & )
    sleep 2; echo "  cachereg: $(tail -1 "/var/mobile/cachereg_${TAG}_$(basename "$tgt").log" 2>/dev/null)"
}

restore_one() {
    local tgt="$1" d b
    d="$(dirname "$tgt")"; b=".$TAG.orig.$(basename "$tgt")"
    [ -e "$d/$b" ] || { echo "  nothing to restore: $tgt"; return 0; }
    mv "$tgt" "$d/.$TAG.patched.$(basename "$tgt")" && mv "$d/$b" "$tgt" || { echo "  MVFAIL $tgt"; return 1; }
    echo "  restored inode=$("$PY" -c "import os,sys;print(os.stat(sys.argv[1]).st_ino)" "$tgt")  $tgt"
}

case "$MODE" in
apply)
    echo "=== apply 4GB-layout patch (new-inode swaps) ==="
    for f in "${MAINS[@]}"; do swap_in "$f" patch_main; done
    for f in "${SUBS[@]}";  do swap_in "$f" patch_sub;  done
    echo "=== NOTE: takes effect only after a REBOOT (region is recreated then) ==="
    ;;
restore)
    echo "=== restore originals ==="
    for f in "${MAINS[@]}" "${SUBS[@]}"; do restore_one "$f"; done
    ;;
*)
    echo "usage: $0 [apply|restore]"; exit 2;;
esac
