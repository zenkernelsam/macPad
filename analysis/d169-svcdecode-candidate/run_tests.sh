#!/bin/sh
# D169 svcdecode — offline decoder candidate. ASan+UBSan run, TSan run
# (harmless — decoder is single-threaded), arm64 object compile.
set -e
cd "$(dirname "$0")"
OUT=/tmp/d169_svcdecode_build
mkdir -p "$OUT"

echo "=== ASan+UBSan ==="
clang -std=c11 -Wall -Wextra -Werror -O2 \
    -fsanitize=address,undefined -I.. \
    -o "$OUT/d169_test" d169_svcdecode.c d169_svcdecode_test.c
"$OUT/d169_test"

echo "=== TSan ==="
if clang -std=c11 -Wall -Wextra -Werror -O2 \
    -fsanitize=thread -I.. \
    -o "$OUT/d169_test_tsan" d169_svcdecode.c d169_svcdecode_test.c 2>/dev/null; then
    "$OUT/d169_test_tsan"
else
    echo "TSan unavailable on this toolchain — skipped"
fi

echo "=== arm64 object ==="
clang -std=c11 -Wall -Wextra -Werror -O2 -arch arm64 -I.. \
    -c d169_svcdecode.c -o "$OUT/d169_svcdecode_arm64.o"
echo "arm64 object: $(file -b "$OUT/d169_svcdecode_arm64.o")"
