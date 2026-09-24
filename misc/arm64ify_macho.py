#!/usr/bin/env python3
"""arm64ify_macho.py — relabel a Mach-O's ARM64/E slice as ARM64/ALL.

Why: the iPadOS kernel will only exec the chroot's macOS binaries as
ARM64/ALL (the 13.4 rootfs ran WindowServer this way; the upstream guide
documents the same cpusubtype edit for "Installer Progress" and
WindowServer).  macOS 15.x ships every system executable as fat
x86_64+arm64e with no ARM64/ALL slice, so the ARM64/E slice's subtype
fields are relabelled in place — the code bytes are kept byte-for-byte
(PAC instructions still execute under the ARM64 personality on A12+).

Same convention as ensure_objc_trampolines_arm64.py: CPU_SUBTYPE_ARM64_ALL
is written as literal 0 (no pointer-auth capability bits), into BOTH the
fat-arch table entry and the thin Mach-O header's cpusubtype field.

Handles: fat files (arm64e slice relabelled in place), thin arm64e files.
Run on the iPad:  /var/jb/usr/bin/python3 arm64ify_macho.py <file> [more...]
Add --check to only report architectures without modifying.
"""

from __future__ import annotations

import os
import stat
import struct
import sys

FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF
MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
CPU_SUBTYPE_MASK = 0x00FFFFFF
CPU_SUBTYPE_CAPABILITY = 0xFF000000
CPU_SUBTYPE_ARM64_ALL = 0
CPU_SUBTYPE_ARM64E = 2


class FormatError(RuntimeError):
    pass


def _fat_entry_size(magic: int) -> int:
    return 32 if magic == FAT_MAGIC_64 else 20


def arch_names(data: bytes) -> list[str]:
    """Best-effort arch list for reporting."""
    out: list[str] = []
    if len(data) >= 8:
        magic, count = struct.unpack_from(">II", data, 0)
        if magic in (FAT_MAGIC, FAT_MAGIC_64) and 0 < count <= 64:
            step = _fat_entry_size(magic)
            for i in range(count):
                cputype, subtype = struct.unpack_from(
                    ">II", data, 8 + i * step)
                if cputype == CPU_TYPE_ARM64:
                    base = subtype & CPU_SUBTYPE_MASK
                    out.append("arm64e" if base == CPU_SUBTYPE_ARM64E
                               else "arm64")
                else:
                    out.append(f"cpu{cputype:#x}")
            return out
    if len(data) >= 12:
        magic, cputype, subtype = struct.unpack_from("<III", data, 0)
        if magic == MH_MAGIC_64 and cputype == CPU_TYPE_ARM64:
            base = subtype & CPU_SUBTYPE_MASK
            return ["arm64e" if base == CPU_SUBTYPE_ARM64E else "arm64"]
    return out


def arm64ify(data: bytes) -> bytes:
    buf = bytearray(data)
    changed = False

    def relabel_thin(offset: int) -> bool:
        if len(buf) < offset + 12:
            return False
        magic, cputype, subtype = struct.unpack_from("<III", buf, offset)
        if (magic != MH_MAGIC_64 or cputype != CPU_TYPE_ARM64 or
                (subtype & CPU_SUBTYPE_MASK) != CPU_SUBTYPE_ARM64E):
            return False
        struct.pack_into("<I", buf, offset + 8, CPU_SUBTYPE_ARM64_ALL)
        return True

    if len(buf) >= 8:
        magic, count = struct.unpack_from(">II", buf, 0)
        if magic in (FAT_MAGIC, FAT_MAGIC_64) and 0 < count <= 64:
            step = _fat_entry_size(magic)
            if 8 + count * step > len(buf):
                raise FormatError("fat arch table overruns file")
            for i in range(count):
                entry = 8 + i * step
                cputype, subtype, offset = struct.unpack_from(
                    ">III", buf, entry)
                if (cputype != CPU_TYPE_ARM64 or
                        (subtype & CPU_SUBTYPE_MASK) != CPU_SUBTYPE_ARM64E):
                    continue
                if not relabel_thin(offset):
                    raise FormatError(
                        f"fat entry {i} claims arm64e but slice is not")
                # fat_arch cpusubtype: same convention — literal ARM64/ALL.
                struct.pack_into(">I", buf, entry + 4,
                                 CPU_SUBTYPE_ARM64_ALL)
                changed = True
            if not changed:
                raise FormatError("fat file has no ARM64/E slice")
            return bytes(buf)

    if relabel_thin(0):
        return bytes(buf)
    raise FormatError("no ARM64/E slice found")


def main() -> int:
    args = sys.argv[1:]
    check = "--check" in args
    paths = [a for a in args if a != "--check"]
    if not paths:
        print(__doc__)
        return 2
    rc = 0
    for path in paths:
        try:
            with open(path, "rb") as fh:
                data = fh.read()
            names = arch_names(data)
            if check:
                print(f"{path}: {' '.join(names)}")
                continue
            new = arm64ify(data)
            st = os.stat(path, follow_symlinks=True)
            tmp = f"{path}.macws-arm64ify-{os.getpid()}"
            try:
                with open(tmp, "xb") as fh:
                    fh.write(new)
                    fh.flush()
                    os.fsync(fh.fileno())
                os.chmod(tmp, stat.S_IMODE(st.st_mode))
                try:
                    os.chown(tmp, st.st_uid, st.st_gid)
                except PermissionError:
                    pass
                os.replace(tmp, path)
            finally:
                try:
                    os.unlink(tmp)
                except FileNotFoundError:
                    pass
            print(f"{path}: {' '.join(names)} -> {' '.join(arch_names(new))}")
        except (FormatError, OSError) as exc:
            print(f"{path}: {exc}", file=sys.stderr)
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
