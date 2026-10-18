"""Minimal Mach-O fixtures for provider staging/package tests.

These are header-only images. They exercise the real Theos/write/read contract
(the same LC_BUILD_VERSION, LC_ID_DYLIB and fat-arch fields as the build) and
are accepted by the host lipo for slice merging; they are never installed.
"""
from __future__ import annotations

import struct
from pathlib import Path
from typing import Iterable, Tuple

FAT_MAGIC = 0xCAFEBABE
MH_MAGIC_64 = 0xFEEDFACF
MH_DYLIB = 6
LC_SEGMENT_64 = 0x19
LC_ID_DYLIB = 0x0D
LC_BUILD_VERSION = 0x32

CPU_TYPE_ARM64 = 0x0100000C
ARM64_SUBTYPE = 0
ARM64E_SUBTYPE = 2
INSTALL_NAME = "/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate"

ALIGN = 12
SLICE_SIZE = 0x1000


def _version(major: int, minor: int, patch: int) -> int:
    return (major << 16) | (minor << 8) | patch


def thin_bytes(arch: str = "arm64", platform: int = 1,
               minos: Tuple[int, int, int] = (13, 0, 0),
               install_name: str = INSTALL_NAME,
               filetype: int = MH_DYLIB) -> bytes:
    subtype = ARM64E_SUBTYPE if arch == "arm64e" else ARM64_SUBTYPE
    segment = struct.pack("<II16sQQQQiiII", LC_SEGMENT_64, 72,
                          b"__TEXT\0\0\0\0\0\0\0\0\0\0", 0, SLICE_SIZE,
                          0, SLICE_SIZE, 5, 5, 0, 0)
    raw_name = install_name.encode("utf-8") + b"\0"
    while (24 + len(raw_name)) % 8:
        raw_name += b"\0"
    dylib = struct.pack("<IIIIII", LC_ID_DYLIB, 24 + len(raw_name),
                        24, 0, 0, 0) + raw_name
    build = struct.pack("<IIIIII", LC_BUILD_VERSION, 24, platform,
                        _version(*minos), _version(13, 0, 0), 0)
    commands = segment + dylib + build
    header = struct.pack("<IiiIIIII", MH_MAGIC_64, CPU_TYPE_ARM64, subtype,
                         filetype, 3, len(commands), 0, 0)
    data = header + commands
    if len(data) > SLICE_SIZE:
        raise ValueError("fixture load commands exceed slice size")
    return data + b"\0" * (SLICE_SIZE - len(data))


def write_thin(path: Path, **kwargs) -> Path:
    path.write_bytes(thin_bytes(**kwargs))
    return path


def fat_bytes(slices: Iterable[bytes]) -> bytes:
    slices = list(slices)
    header_size = 8 + 20 * len(slices)
    offsets = []
    cursor = header_size
    for data in slices:
        cursor = (cursor + (1 << ALIGN) - 1) & ~((1 << ALIGN) - 1)
        offsets.append(cursor)
        cursor += len(data)
    output = bytearray(cursor)
    struct.pack_into(">II", output, 0, FAT_MAGIC, len(slices))
    for index, data in enumerate(slices):
        if data[0:4] == struct.pack("<I", MH_MAGIC_64):
            cputype, subtype = struct.unpack_from("<ii", data, 4)
        else:
            raise ValueError("fixture slice is not a little-endian Mach-O")
        struct.pack_into(">IIIII", output, 8 + 20 * index, cputype, subtype,
                         offsets[index], len(data), ALIGN)
        output[offsets[index]:offsets[index] + len(data)] = data
    return bytes(output)


def write_fat(path: Path, slices: Iterable[bytes]) -> Path:
    path.write_bytes(fat_bytes(slices))
    return path


def write_bundle(directory: Path, platform: int = 1,
                 minos: Tuple[int, int, int] = (13, 0, 0),
                 install_name: str = INSTALL_NAME,
                 include_arm64: bool = True,
                 include_arm64e: bool = True) -> Path:
    """Create the staged fat + thin companion layout used by the package."""
    directory.mkdir(parents=True, exist_ok=True)
    slices = []
    if include_arm64:
        arm64 = thin_bytes("arm64", platform, minos, install_name)
        (directory / "CydiaSubstrate.arm64").write_bytes(arm64)
        slices.append(arm64)
    if include_arm64e:
        arm64e = thin_bytes("arm64e", platform, minos, install_name)
        (directory / "CydiaSubstrate.arm64e").write_bytes(arm64e)
        slices.append(arm64e)
    if slices:
        write_fat(directory / "CydiaSubstrate", slices)
    else:
        (directory / "CydiaSubstrate").write_bytes(b"")
    return directory
