#!/bin/bash
# Deploy the libSystem.B.dylib + libdyld.dylib shims into the chroot rootfs.
# Signs (ldid), adds to trustcache, replaces targets, chmod 755.
set -e
cd /Users/ciscohe/Desktop/macPad

deploy() {
  local LOCAL="$1"   # local file
  local NAME="$2"    # staging basename on device
  local REMOTE="$3"  # final remote path
  echo "===== deploy $LOCAL -> $REMOTE ====="
  sshpass -p cisco scp -P 2222 -o StrictHostKeyChecking=no \
    -o PreferredAuthentications=password -o PubkeyAuthentication=no \
    "$LOCAL" root@192.168.64.1:/var/mobile/${NAME}.bin
  bash tmp/dssh.sh "cd /var/mobile
cp -f ${NAME}.bin ${NAME}.s
ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist ${NAME}.s
ldid -h ${NAME}.s | sed -n 's/^CDHash=//p' > ${NAME}_hashes.txt
cat ${NAME}_hashes.txt
while read H; do /var/jb/basebin/jbctl trustcache add \$H && echo added \$H; done < ${NAME}_hashes.txt
rm -f ${REMOTE}
cp -f ${NAME}.s ${REMOTE}
chmod 755 ${REMOTE}
ls -la ${REMOTE}
while read H; do /var/jb/basebin/jbctl trustcache info | grep -i \${H:0:16} && echo OK \$H || echo WARN_not_listed \$H; done < ${NAME}_hashes.txt"
}

deploy tmp/shim/libdyld.dylib  shim_libdyld    /var/mnt/rootfs/usr/lib/system/libdyld.dylib
deploy tmp/shim/libSystem.B.dylib shim_libsystem /var/mnt/rootfs/usr/lib/libSystem.B.dylib
echo "deployed shims."
