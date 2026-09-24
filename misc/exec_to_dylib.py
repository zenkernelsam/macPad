#!/usr/bin/env python3
"""exec_to_dylib.py — convert a macOS system EXECUTABLE to a loadable DYLIB.

Used for daemons that must be dlopen'ed by the entitled iOS-side shim
(launchservicesd): iOS launchd spawns the shim, the shim calls
dlopen_entry_point("@loader_path/<name>.dylib") which reads LC_MAIN
entryoff and jumps in. Requires the target to keep LC_MAIN — it does.

Recipe (matches the project's proven 13.4 launchservicesd.dylib):
  1. extract the ARM64/E slice from the fat file (or keep thin input)
  2. filetype MH_EXECUTE(2) -> MH_DYLIB(6)
  3. append LC_ID_DYLIB into the zero padding after the load commands,
     provided at least cmdsize bytes of slack exist before the next
     non-zero byte (launchservicesd 15.6.1 has exactly 56)
  4. caller re-signs with ldid -S and registers the CDHash

Run on the iPad: /var/jb/usr/bin/python3 exec_to_dylib.py \
    /System/Library/CoreServices/launchservicesd \
    /System/Library/CoreServices/launchservicesd.dylib
"""

from __future__ import annotations

import struct
import sys

FAT_MAGIC = 0xCAFEBABE
MH_MAGIC_64 = 0xFEEDFACF
MH_EXECUTE = 2
MH_DYLIB = 6
CPU_TYPE_ARM64 = 0x0100000C
CPU_SUBTYPE_ARM64E = 2
LC_ID_DYLIB = 0x0D
DYLIB_NAME = b"launchservicesd_arm64e.dylib\0"


class FormatError(RuntimeError):
    pass


def arm64e_slice(data: bytes) -> bytes:
    if len(data) >= 8:
        magic, ncpu = struct.unpack_from(">II", data, 0)
        if magic == FAT_MAGIC and 0 < ncpu <= 64:
            for i in range(ncpu):
                ct, cs, off, sz, _al = struct.unpack_from(
                    ">IIIII", data, 8 + i * 20)
                if ct == CPU_TYPE_ARM64 and (cs & 0xFFFFFF) == CPU_SUBTYPE_ARM64E:
                    return data[off:off + sz]
            raise FormatError("no arm64e slice")
    return data


def convert(data: bytes) -> bytes:
    s = bytearray(arm64e_slice(data))
    magic, _ct, _cs, ftype, ncmds, sizeofcmds, _flags, _res = \
        struct.unpack_from("<8I", s, 0)
    if magic != MH_MAGIC_64:
        raise FormatError("slice is not a 64-bit Mach-O")
    if ftype != MH_EXECUTE:
        raise FormatError(f"filetype {ftype}, expected EXECUTE")

    end = 32 + sizeofcmds
    slack = 0
    for i in range(end, min(end + 4096, len(s))):
        if s[i] == 0:
            slack += 1
            continue
        break

    cmdsize = (24 + len(DYLIB_NAME) + 7) & -8
    if slack < cmdsize:
        raise FormatError(
            f"only {slack}B slack after load commands, need {cmdsize}B")

    # append LC_ID_DYLIB into the padding
    struct.pack_into("<II", s, end, LC_ID_DYLIB, cmdsize)
    struct.pack_into("<II", s, end + 8, 24, 2)  # name offset, timestamp
    struct.pack_into("<II", s, end + 16, 0x10000, 0x10000)  # cur/compat 1.0.0
    s[end + 24:end + 24 + len(DYLIB_NAME)] = DYLIB_NAME

    # header: filetype, ncmds, sizeofcmds
    struct.pack_into("<I", s, 12, MH_DYLIB)
    struct.pack_into("<I", s, 16, ncmds + 1)
    struct.pack_into("<I", s, 20, sizeofcmds + cmdsize)
    return bytes(s)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    try:
        with open(src, "rb") as fh:
            out = convert(fh.read())
        with open(dst, "wb") as fh:
            fh.write(out)
        print(f"{dst}: EXECUTE->DYLIB ok ({len(out)} bytes)")
        return 0
    except (FormatError, OSError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
