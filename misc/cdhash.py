#!/usr/bin/env python3
# cdhash.py — print cdhash (hex) of a Mach-O or fat Mach-O's arm64 slice.
# cdhash = hash(CodeDirectory blob) truncated to 20 bytes, using the
# directory's own hash type (sha1=1, sha256=2, sha256-truncated=3, sha384=4).
import struct, sys, hashlib

CSMAGIC_EMBEDDED_SIGNATURE = 0xfade0cc0
CSMAGIC_CODEDIRECTORY    = 0xfade0c02
LC_CODE_SIGNATURE        = 0x1d

def cdhash_of_cd(cd):
    magic, length, version = struct.unpack(">III", cd[0:12])
    if magic != CSMAGIC_CODEDIRECTORY:
        return None
    # CodeDirectory header: hashSize at 36, hashType at 37 — NOT offset 34
    # (34 sits inside codeLimit). Reading 34 forced every sha256-typed CD
    # down the sha1 fallback, producing hashes AMFI never matches.
    hashType = cd[37]
    blob = cd[:length]
    if hashType == 4:
        h = hashlib.sha384(blob).digest()
    elif hashType == 1:
        h = hashlib.sha1(blob).digest()
    else:  # 2 = sha256, 3 = sha256-truncated, 0 defaults to sha256-era CDs
        h = hashlib.sha256(blob).digest()
    return h[:20].hex()

def sigs_from_macho(d):
    out = []
    magic = d[0:4]
    if magic != b"\xcf\xfa\xed\xfe" and magic != b"\xce\xfa\xed\xfe":
        return out
    e = "<" if magic == b"\xcf\xfa\xed\xfe" else ">"
    ncmds = struct.unpack(e + "I", d[16:20])[0]
    off = 32
    for _ in range(ncmds):
        cmd, sz = struct.unpack(e + "II", d[off:off+8])
        if cmd == LC_CODE_SIGNATURE:
            dataoff, datasz = struct.unpack(e + "II", d[off+8:off+16])
            blob = d[dataoff:dataoff+datasz]
            if struct.unpack(">I", blob[0:4])[0] == CSMAGIC_EMBEDDED_SIGNATURE:
                cnt = struct.unpack(">I", blob[8:12])[0]
                for i in range(cnt):
                    btype, boff = struct.unpack(">II", blob[12+i*8:20+i*8])
                    h = cdhash_of_cd(blob[boff:])
                    if h: out.append(h)
        off += sz
    return out

def main(p):
    d = open(p, "rb").read()
    if d[:4] == b"\xca\xfe\xba\xbe":            # fat
        n = struct.unpack(">I", d[4:8])[0]
        for i in range(n):
            ct, st, so, sz, al = struct.unpack(">IIIII", d[8+i*20:28+i*20])
            if ct == 0x100000c:
                for h in sigs_from_macho(d[so:so+sz]):
                    print(h)
                return
    else:
        for h in sigs_from_macho(d):
            print(h)

for p in sys.argv[1:]:
    main(p)
