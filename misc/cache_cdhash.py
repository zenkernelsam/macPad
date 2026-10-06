#!/usr/bin/env python3
# cache_cdhash.py <dyld_shared_cache_*> [...] — print the AMFI/trustcache
# CDHash (40 hex) of each dyld shared cache file.
#
# Method (verified 2026-10-06 against `codesign -vvv -d` on the stock
# UniversalMac_13.4.1_22F82 cryptex):
#   1. The cache is NOT a Mach-O — it starts with `dyld_v1  arm64e\0`.
#      Its code signature lives at `codeSignatureOffset`/`codeSignatureSize`,
#      two little-endian u64 at header offsets 0x28/0x30.
#   2. The signature blob is a CS_SuperBlob (0xfade0cc0). Index slot 0 is the
#      primary CodeDirectory (0xfade0c02).
#   3. cdhash = hash(entire CodeDirectory blob)[:20], using the algorithm in
#      the CD's own hashType byte at offset 37 (1=sha1, 2=sha256,
#      3=sha256-truncated, 4=sha384).
#
# Use this to derive the literal pairs for postinst.sh / macos_gui.sh /
# DEBIAN/postinst whenever a NEW rootfs build is staged. Never reuse a pair
# across builds: each build's cache has a different CodeDirectory.
import struct
import sys
import hashlib

_SUPERBLOB = 0xFADE0CC0
_CODEDIR = 0xFADE0C02
_HASHFN = {1: hashlib.sha1, 2: hashlib.sha256, 3: hashlib.sha256,
           4: hashlib.sha384}


def cache_cdhash(path):
    with open(path, "rb") as f:
        hdr = f.read(0x40)
        if not hdr[:8] == b"dyld_v1 ":
            return None, "not a dyld shared cache (bad magic %r)" % hdr[:8]
        cs_off, cs_size = struct.unpack_from("<QQ", hdr, 0x28)
        f.seek(cs_off)
        blob = f.read(cs_size)
    if len(blob) < 12 or struct.unpack_from(">I", blob, 0)[0] != _SUPERBLOB:
        return None, "no CS_SuperBlob at codeSignatureOffset"
    count = struct.unpack_from(">I", blob, 8)[0]
    for i in range(count):
        btype, boff = struct.unpack_from(">II", blob, 12 + i * 8)
        if btype != 0 or boff + 8 > len(blob):
            continue
        magic, length = struct.unpack_from(">II", blob, boff)
        if magic != _CODEDIR or boff + length > len(blob):
            continue
        cd = blob[boff:boff + length]
        fn = _HASHFN.get(cd[37], hashlib.sha256)
        return fn(cd).hexdigest()[:40], None
    return None, "no CodeDirectory slot 0 in superblob"


def main():
    rc = 0
    for path in sys.argv[1:]:
        h, err = cache_cdhash(path)
        if err:
            print("%s\tERROR\t%s" % (path, err), file=sys.stderr)
            rc = 1
        else:
            print("%s\t%s" % (path, h))
    return rc


if __name__ == "__main__":
    sys.exit(main())
