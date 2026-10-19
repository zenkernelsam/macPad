#!/bin/bash
# D202 drain→v3 harness — host tests only. Links the committed
# d141/d169 candidates read-only. Nothing here touches a device,
# production source, sentinel, hook, or permit.
set -e
cd "$(dirname "$0")"

CC="clang -std=c11 -Wall -Wextra -Werror -O2 -g -DD141_TEST_HOOKS -DD202_TEST_HOOKS"
SRC="d202_drainser_test.c d202_drainser.c \
../d141-svctime-candidate/d141_svctime.c \
../d169-svcdecode-candidate/d169_svcdecode.c"

echo "=== ASan+UBSan ==="
$CC -fsanitize=address,undefined $SRC -o /tmp/d202_test_asan
/tmp/d202_test_asan

echo "=== TSan ==="
$CC -fsanitize=thread $SRC -o /tmp/d202_test_tsan
/tmp/d202_test_tsan

echo "=== arm64 objects ==="
clang -std=c11 -Wall -Wextra -Werror -O2 -target arm64-apple-darwin \
    -c d202_drainser.c -o /tmp/d202_drainser_arm64.o
file /tmp/d202_drainser_arm64.o
