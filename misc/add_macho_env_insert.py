#!/usr/bin/env python3
"""Add LC_ENVIRONMENT_DYLD_INSERT_LIBRARIES to ARM64 slices in-place.

Same discipline as add_macho_load_dylib.py: the command is written only
into existing zero-filled header padding; binaries without room are
refused so chained fixups/exports/code-signature offsets never move.
The payload is the literal string dyld expects, e.g.
"DYLD_INSERT_LIBRARIES=/usr/local/lib/libmachook_arm64.dylib".

EXPERIMENTAL — negative result on Ventura 22F82 dyld (1066.8):
2026-10-07 a signed+trusted /bin/echo carrying this command still hit
the os_variant brk trap under bare chroot (insert never applied). Keep
for diagnosis on other builds; the working mechanism for that milestone
is LC_LOAD_DYLIB on the interposer via add_macho_load_dylib.py.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


FAT_MAGIC = 0xCAFEBABE
MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
LC_SEGMENT_64 = 0x19
LC_ENVIRONMENT_DYLD_INSERT_LIBRARIES = 0x27


def aligned(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def slices(data: bytearray) -> list[tuple[int, int, int, int]]:
    if len(data) < 8:
        raise ValueError("file is too small")
    magic_be = struct.unpack_from(">I", data, 0)[0]
    if magic_be == FAT_MAGIC:
        count = struct.unpack_from(">I", data, 4)[0]
        if len(data) < 8 + count * 20:
            raise ValueError("truncated fat header")
        result = []
        for index in range(count):
            cpu, subtype, offset, size, _align = struct.unpack_from(
                ">IIIII", data, 8 + index * 20
            )
            if offset + size > len(data):
                raise ValueError(f"fat slice {index} exceeds file")
            result.append((cpu, subtype & 0xFFFFFF, offset, size))
        return result
    cpu = struct.unpack_from("<I", data, 4)[0]
    subtype = struct.unpack_from("<I", data, 8)[0]
    return [(cpu, subtype & 0xFFFFFF, 0, len(data))]


def add_to_slice(data: bytearray, base: int, size: int, payload: str) -> bool:
    if size < 32 or struct.unpack_from("<I", data, base)[0] != MH_MAGIC_64:
        raise ValueError(f"ARM64 slice at {base:#x} is not 64-bit Mach-O")
    ncmds, sizeofcmds = struct.unpack_from("<II", data, base + 16)
    command_offset = base + 32
    commands_end = command_offset + sizeofcmds
    slice_end = base + size
    if commands_end > slice_end:
        raise ValueError("load commands exceed slice")

    first_section = slice_end
    cursor = command_offset
    for index in range(ncmds):
        if cursor + 8 > commands_end:
            raise ValueError(f"truncated load command {index}")
        command, command_size = struct.unpack_from("<II", data, cursor)
        if command_size < 8 or cursor + command_size > commands_end:
            raise ValueError(f"invalid load command {index}")
        if command == LC_ENVIRONMENT_DYLD_INSERT_LIBRARIES and command_size >= 16:
            existing = c_string(
                data, cursor + 12, cursor + command_size)
            if existing == payload:
                return False
            raise ValueError(
                "slice already carries LC_ENVIRONMENT_DYLD_INSERT_LIBRARIES "
                f"({existing!r}); refusing to add a second")
        if command == LC_SEGMENT_64 and command_size >= 72:
            section_count = struct.unpack_from("<I", data, cursor + 64)[0]
            if 72 + section_count * 80 > command_size:
                raise ValueError(f"invalid section table in command {index}")
            for section_index in range(section_count):
                section = cursor + 72 + section_index * 80
                file_offset = struct.unpack_from("<I", data, section + 48)[0]
                if file_offset:
                    first_section = min(first_section, base + file_offset)
        cursor += command_size
    if cursor != commands_end:
        raise ValueError("sizeofcmds does not match load-command traversal")

    encoded = payload.encode("utf-8") + b"\0"
    # cmd(4) + cmdsize(4) + lc_str offset(4) + string
    command_size = aligned(12 + len(encoded), 8)
    new_end = commands_end + command_size
    if new_end > first_section:
        available = first_section - commands_end
        raise ValueError(
            f"need {command_size} bytes of header padding, have {available}"
        )
    if any(data[commands_end:new_end]):
        raise ValueError("prospective load-command padding is not zero-filled")

    command = struct.pack(
        "<III", LC_ENVIRONMENT_DYLD_INSERT_LIBRARIES, command_size, 12
    )
    command += encoded
    command += b"\0" * (command_size - len(command))
    data[commands_end:new_end] = command
    struct.pack_into("<II", data, base + 16, ncmds + 1,
                     sizeofcmds + command_size)
    return True


def c_string(data: bytearray, start: int, limit: int) -> str:
    end = data.find(b"\0", start, limit)
    if end < 0:
        raise ValueError("unterminated load-command string")
    return bytes(data[start:end]).decode("utf-8", errors="strict")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("payload",
        help="e.g. DYLD_INSERT_LIBRARIES=/usr/local/lib/libmachook_arm64.dylib")
    parser.add_argument("--arm64e", action="store_true",
        help="also patch arm64e slices (subtype 2); default arm64-only "
             "(subtypes 0/1) so each arch can carry its own dylib path")
    arguments = parser.parse_args()

    path = Path(arguments.binary)
    data = bytearray(path.read_bytes())
    arm_slices = 0
    changed = 0
    for cpu, subtype, offset, size in slices(data):
        if cpu != CPU_TYPE_ARM64:
            continue
        if subtype == 2 and not arguments.arm64e:
            continue
        arm_slices += 1
        if add_to_slice(data, offset, size, arguments.payload):
            changed += 1
    if not arm_slices:
        raise SystemExit("no matching ARM64 Mach-O slice found")
    if changed:
        temporary = path.with_name(path.name + ".macws-env-insert.tmp")
        temporary.write_bytes(data)
        temporary.chmod(path.stat().st_mode)
        temporary.replace(path)
    print(f"{path}: ARM64 slices={arm_slices} modified={changed} "
          f"payload={arguments.payload}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
