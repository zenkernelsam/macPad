#!/bin/sh
# D141/D143/D150b isolated svctime prototype — host-side tests + arm64 build.
# ASan+UBSan run, TSan run (if supported), real arm64 object compile.
set -e
cd "$(dirname "$0")"
OUT=/tmp/d141_svctime_build
mkdir -p "$OUT"

echo "=== ASan+UBSan ==="
clang -std=c11 -Wall -Wextra -Werror -O2 -pthread \
    -fsanitize=address,undefined -DD141_TEST_HOOKS= \
    -o "$OUT/d141_test" d141_svctime.c d141_svctime_test.c
"$OUT/d141_test"

echo "=== TSan ==="
if clang -std=c11 -Wall -Wextra -Werror -O2 -pthread \
    -fsanitize=thread -DD141_TEST_HOOKS= \
    -o "$OUT/d141_test_tsan" d141_svctime.c d141_svctime_test.c 2>/dev/null; then
    "$OUT/d141_test_tsan"
else
    echo "TSan unavailable on this toolchain — skipped"
fi

echo "=== arm64 object ==="
clang -std=c11 -Wall -Wextra -Werror -O2 -arch arm64 \
    -c d141_svctime.c -o "$OUT/d141_svctime_arm64.o"
echo "arm64 object: $(file -b "$OUT/d141_svctime_arm64.o")"
