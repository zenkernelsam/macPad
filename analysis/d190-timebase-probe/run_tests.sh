#!/bin/bash
# D190 candidate test runner — host tests only; the sender binary is
# compiled (arm64 object + binary attempt) but NEVER invoked.
set -e
cd "$(dirname "$0")"

CC="clang -std=c11 -Wall -Wextra -Werror -O2 -g -DD190_TEST_HOOKS"
SRC="d190_probe_test.c d190_probe.c ../d169-svcdecode-candidate/d169_svcdecode.c"

# harmless fixture dylib for the F1 dlsym C-name test
clang -std=c11 -O2 -dynamiclib d190_fixture.c -o /tmp/d190_fixture.dylib

echo "=== ASan+UBSan ==="
$CC -fsanitize=address,undefined $SRC -o /tmp/d190_test_asan
/tmp/d190_test_asan

echo "=== TSan ==="
$CC -fsanitize=thread $SRC -o /tmp/d190_test_tsan
/tmp/d190_test_tsan

echo "=== arm64 objects ==="
clang -std=c11 -Wall -Wextra -Werror -O2 -arch arm64 \
    -c d190_probe.c -o /tmp/d190_probe_arm64.o
clang -std=c11 -Wall -Wextra -Werror -O2 -arch arm64 \
    -c d190_main.c -o /tmp/d190_main_arm64.o
file /tmp/d190_probe_arm64.o /tmp/d190_main_arm64.o | sed 's|/tmp/||'

echo "=== arm64 binary link attempt (never run) ==="
if clang -std=c11 -O2 -arch arm64 /tmp/d190_probe_arm64.o \
        /tmp/d190_main_arm64.o -o /tmp/d190_probe_arm64 2>/tmp/d190_link_err; then
    file /tmp/d190_probe_arm64 | sed 's|/tmp/||'
else
    echo "arm64 link not feasible on this host:"
    cat /tmp/d190_link_err
fi
