#!/bin/bash
# uncache_batch.sh — route D smoke test: extract -> uncache -> uniquify ID -> dlopen.
#
# Why this exists: after the slide-info-v5 patches (misc/dyldextractor-2.2.2-slideinfo5.patch
# and misc/uncache-slideinfo5.patch) a macOS 15.6.1 cache image can be turned into a
# genuinely loadable dylib. This script runs that chain over a list of images and prints
# one line per image so the success rate is measurable.
#
# Uniquifying LC_ID_DYLIB is what makes the dlopen test meaningful: without it dyld
# dedupes by install name and silently returns the copy already loaded from the system
# cache, so a "success" would prove nothing.
#
# Requirements:
#   - a dyldextractor install WITH the v5 patch (see misc/dyldextractor-2.2.2-slideinfo5.patch)
#   - an uncache.py WITH the v5 patch (see misc/uncache-slideinfo5.patch)
#   - the ipsw-a2sb binary and the cache's .a2s index (both from the VirtualMacOniPad fork)
#
# Usage:
#   DEX_PY=/path/to/venv/bin          \
#   UNCACHE=/path/to/uncache.py       \
#   IPSW_A2SB=/path/to/toolchain/bin/ipsw-a2sb \
#   DSC=/path/to/dyld_shared_cache_arm64e      \
#   misc/uncache_batch.sh /usr/lib/libc++.1.dylib /usr/lib/libpcap.A.dylib ...
#
# Notes / known limitations found by running it:
#   - /usr/lib/libobjc.A.dylib is listed by `dyldex -l -f libobjc` but cannot be extracted
#     ("Unable to find image") - a dyldextractor limitation, unrelated to v5.
#   - /usr/lib/libSystem.B.dylib is a tiny umbrella stub and libSystem is already loaded in
#     every process, so a host dlopen of a renamed copy is not a valid test (it gets killed).
set -u

: "${DEX_PY:=/Users/ciscohe/Desktop/macPad/tmp/dscvenv/bin}"
: "${UNCACHE:=/tmp/uncache_v5.py}"
: "${IPSW_A2SB:=$HOME/Desktop/VirtualMacOniPad/VirtualMac/build/toolchain/bin/ipsw-a2sb}"
: "${DSC:=/Users/ciscohe/Desktop/macPad/analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e}"
: "${WORK:=/tmp/uncache_batch}"

mkdir -p "$WORK/dex" "$WORK/out"
for P in "$@"; do
	B=$(basename "$P"); S="${B%%.dylib}"

	if ! "$DEX_PY/dyldex" -e "$P" -o "$WORK/dex/$B" "$DSC" >/dev/null 2>&1 \
	   || [ ! -s "$WORK/dex/$B" ]; then
		echo "EXTRACT_FAIL|$P|"
		continue
	fi

	U=$(env VZ_IPSW="$IPSW_A2SB" VZ_MAC=1 "$DEX_PY/python" "$UNCACHE" \
	        "$DSC" "$S" "$WORK/dex/$B" "$WORK/out/$B" 2>&1 | grep -v pkg_resources)
	if ! echo "$U" | grep -q '^wrote '; then
		echo "UNCACHE_FAIL|$P|$(echo "$U" | grep -iE 'error|SystemExit' | head -1 | cut -c1-100)"
		continue
	fi
	RB=$(echo "$U" | grep -oE '^[0-9]+ rebases' | head -1)
	BD=$(echo "$U" | grep -oE '[0-9]+ binds' | head -1)

	D="$WORK/uniq_$B"
	python3 - "$D" "$WORK/out/$B" "$B" <<'PYEOF'
import struct, sys
dst, src, tag = sys.argv[1], sys.argv[2], sys.argv[3]
b = bytearray(open(src, 'rb').read())
n = struct.unpack_from('<I', b, 16)[0]; o = 32
for _ in range(n):
    c, s = struct.unpack_from('<II', b, o)
    if c == 0xD:                      # LC_ID_DYLIB (NOT LC_LOAD_DYLIB == 0xC)
        p = o + struct.unpack_from('<I', b, o + 8)[0]
        old = bytes(b[p:b.index(b'\0', p)]); new = ('/tmp/UB_' + tag).encode()
        b[p:p + len(old) + 1] = new + b'\0' * (len(old) + 1 - len(new))
        break
    o += s
open(dst, 'wb').write(bytes(b))
PYEOF
	codesign -s - --force "$D" >/dev/null 2>&1
	if python3 -c "import ctypes; ctypes.CDLL('$D')" >/dev/null 2>&1; then R=LOAD_OK; else R=LOAD_FAIL; fi
	echo "$R|$P|$RB $BD size=$(stat -f%z "$WORK/out/$B" 2>/dev/null)"
done
