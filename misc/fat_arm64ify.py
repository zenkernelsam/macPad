#!/usr/bin/env python3
# fat_arm64ify.py — convert every arm64e slice (cpusubtype 0x80000002/3) in a
# fat Mach-O to arm64 (cpusubtype 0). Equivalent of set_to_arm64.py per-slice.
# After conversion the file MUST be re-signed (ldid) + cdhash re-added.
import struct, sys

def fix(path):
    d = bytearray(open(path, "rb").read())
    if d[:4] == b"\xcf\xfa\xed\xfe":          # thin
        if struct.unpack("<I", d[8:12])[0] & 0x7fffffff == 2:
            d[8:12] = struct.pack("<I", 0)
            print(f"{path}: thin arm64e->arm64")
        else:
            print(f"{path}: thin subtype=0x{struct.unpack('<I',d[8:12])[0]:x} (skip)")
            return
    elif d[:4] == b"\xca\xfe\xba\xbe":        # fat BE
        n = struct.unpack(">I", d[4:8])[0]
        for i in range(n):
            off = 8 + i*20
            ct, st, so, sz, al = struct.unpack(">IIIII", d[off:off+20])
            if ct == 0x100000c and st & 0x7fffffff == 2:   # arm64e slice
                d[so+8:so+12] = b"\x00\x00\x00\x00"        # cpusubtype=0 (arm64 ALL)
                print(f"{path}: slice[{i}] off=0x{so:x} arm64e->arm64")
    else:
        print(f"{path}: not macho"); return
    open(path, "wb").write(d)

for p in sys.argv[1:]:
    fix(p)
