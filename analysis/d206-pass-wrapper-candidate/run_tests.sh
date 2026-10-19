#!/bin/bash
# D206 typed PASS wrapper — host tests only. No hooks/permits/WS/sender.
set -e
cd "$(dirname "$0")"

CC="clang -std=c11 -Wall -Wextra -Werror -O2 -g"
SRC="d206_pass_wrapper_test.c d206_pass_wrapper.c \
../d141-svctime-candidate/d141_svctime.c"

echo "=== ASan+UBSan ==="
$CC -fsanitize=address,undefined $SRC -o /tmp/d206_test_asan
/tmp/d206_test_asan

echo "=== TSan ==="
$CC -fsanitize=thread $SRC -o /tmp/d206_test_tsan
/tmp/d206_test_tsan

echo "=== arm64 objects ==="
clang -std=c11 -Wall -Wextra -Werror -O2 -target arm64-apple-darwin \
    -c d206_pass_wrapper.c -o /tmp/d206_pass_wrapper_arm64.o
file /tmp/d206_pass_wrapper_arm64.o
