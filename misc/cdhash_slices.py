#!/usr/bin/env python3
# cdhash_slices.py <macho> [...] — print the *ldid-compatible* cdhash (40 hex) of
# EVERY slice of each Mach-O (fat or thin). This is the value to feed
# `jbctl trustcache add`. It matches `ldid -h`'s "CDHash=" line, i.e.
# sha256 over the CodeDirectory blob only (length taken from the CD header),
# NOT over the whole superblob and NOT the whole file.
#
# Why this exists: the trustcache is in-memory and is WIPED on every reboot.
# After a reboot every patched/re-signed Mach-O the chroot loads (dyld, the
# libSystem/libdyld disk shims, echo, ...) must be re-registered or AMFI kills
# the process with "code signature invalid (errno=1)". Fat files carry one
# cdhash PER SLICE and dyld may pick a non-first slice (observed: slice1 at
# 0x18000), so register all of them.
import sys, struct, hashlib


def cdhashes(blob, base):
    """Yield cdhash hex for the slice whose thin Mach-O header is at `base`."""
    n = struct.unpack_from('<I', blob, base + 16)[0]
    c = base + 32
    cs = None
    for _ in range(n):
        cmd, sz = struct.unpack_from('<II', blob, c)
        if cmd == 0x1d:                       # LC_CODE_SIGNATURE
            cs = struct.unpack_from('<II', blob, c + 8)
        c += sz
    if not cs:
        return
    b = blob[base + cs[0]:base + cs[0] + cs[1]]
    cnt = struct.unpack_from('>I', b, 8)[0]
    for i in range(cnt):
        t = struct.unpack_from('>I', b, 12 + i * 8)[0]
        o = struct.unpack_from('>I', b, 16 + i * 8)[0]
        if t == 0:                            # slot 0 = CodeDirectory
            cdlen = struct.unpack_from('>I', b, o + 4)[0]
            yield hashlib.sha256(b[o:o + cdlen]).hexdigest()[:40]


def main():
    for path in sys.argv[1:]:
        try:
            d = open(path, 'rb').read()
        except OSError as e:
            print("%s\tERROR\t%s" % (path, e), file=sys.stderr); continue
        if d[:4] in (b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'):
            n = struct.unpack_from('>I', d, 4)[0]
            for i in range(n):
                ct, cs, off, size, al = struct.unpack_from('>IIIII', d, 8 + 20 * i)
                for h in cdhashes(d, off):
                    print("%s\tslice%d\t%s" % (path, i, h))
        else:
            for h in cdhashes(d, 0):
                print("%s\tthin\t%s" % (path, h))


if __name__ == '__main__':
    main()
