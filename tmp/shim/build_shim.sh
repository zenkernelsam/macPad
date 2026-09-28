#!/bin/bash
# Build the two shims:
#   1) /usr/lib/system/libdyld.dylib  (satisfies dyld's libdyld gate)
#   2) /usr/lib/libSystem.B.dylib     (echo's 10 imports; depends on libdyld)
set -x
S=/Users/ciscohe/Desktop/macPad/tmp/shim
SDKROOT=$(xcrun --show-sdk-path)
echo "SDK=$SDKROOT"

# ---- 1) libdyld shim ----
for ARCH in arm64e arm64; do
  clang -x c++ -arch $ARCH -isysroot "$SDKROOT" -mmacosx-version-min=13.0 \
    -Wl,-platform_version,macos,13.0,13.0 -dynamiclib \
    -install_name /usr/lib/system/libdyld.dylib \
    -current_version 1351.0.0 -compatibility_version 1.0.0 \
    -fno-rtti -fno-exceptions -fno-builtin -fno-stack-protector \
    -nostdlib++ \
    -Wl,-no_adhoc_codesign \
    -o /tmp/libdyld.${ARCH}.dylib "$S/libdyld_shim.cpp" 2>&1 | tail -8
  echo "=== libdyld $ARCH rc=${PIPESTATUS[0]} ==="
done
lipo -create -output "$S/libdyld.dylib" /tmp/libdyld.arm64e.dylib /tmp/libdyld.arm64.dylib 2>&1

# ---- 2) libSystem shim (depends on libdyld) ----
for ARCH in arm64e arm64; do
  clang -arch $ARCH -isysroot "$SDKROOT" -mmacosx-version-min=13.0 \
    -Wl,-platform_version,macos,13.0,13.0 -dynamiclib \
    -install_name /tmp/shim/libSystem.B.tmp.dylib \
    -current_version 1351.0.0 -compatibility_version 1.0.0 \
    -fno-builtin -ffreestanding -fno-stack-protector \
    -Wl,-no_adhoc_codesign \
    -o /tmp/libSystem.B.${ARCH}.dylib "$S/libSystem_shim.c" "$S/libdyld.dylib" 2>&1 | tail -8
  echo "=== libSystem $ARCH rc=${PIPESTATUS[0]} ==="
  install_name_tool -id /usr/lib/libSystem.B.dylib /tmp/libSystem.B.${ARCH}.dylib 2>&1
done
lipo -create -output "$S/libSystem.B.dylib" /tmp/libSystem.B.arm64e.dylib /tmp/libSystem.B.arm64.dylib 2>&1

# ---- 3) msh — self-contained shell, forced dep on shim libSystem ----
# (static MH_EXECUTE gets exec-veto'd (SIGKILL) under the chroot; must go via dyld)
for ARCH in arm64e arm64; do
  clang -arch $ARCH -isysroot "$SDKROOT" -mmacosx-version-min=13.0 \
    -Wl,-platform_version,macos,13.0,13.0 \
    -Wl,-e,__start -Wl,-no_adhoc_codesign -Wl,-u,_exit \
    -fno-builtin -ffreestanding -fno-stack-protector -nostdlib \
    -o /tmp/msh.${ARCH} "$S/msh.c" "$S/libSystem.B.dylib" 2>&1 | tail -8
  echo "=== msh $ARCH rc=${PIPESTATUS[0]} ==="
done
lipo -create -output "$S/msh" /tmp/msh.arm64e /tmp/msh.arm64 2>&1

echo "======== libSystem.B.dylib otool -L ========"
otool -L "$S/libSystem.B.dylib"
echo "======== libSystem.B.dylib nm -gU ========"
nm -gU "$S/libSystem.B.dylib" 2>/dev/null | grep -E ' T | D | S ' | head -20
echo "======== libSystem.B.dylib nm -u ========"
nm -u "$S/libSystem.B.dylib" 2>&1 | head
echo "======== libdyld.dylib sections ========"
otool -l "$S/libdyld.dylib" | grep -B1 -A5 -E 'sectname __helper|sectname __dyld_apis'
echo "======== libdyld.dylib install name + exports ========"
otool -D "$S/libdyld.dylib"
nm -gU "$S/libdyld.dylib" 2>/dev/null | grep -iE 'anchor|progname|NXArg|environ'
ls -la "$S/libSystem.B.dylib" "$S/libdyld.dylib"
