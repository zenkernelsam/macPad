#!/usr/bin/env python3
"""Stage and publish the exact macOS CydiaSubstrate provider.

The provider must be the same two-slice artifact that libmachook links.  This
module verifies the Mach-O contract instead of trusting a filename: arm64 and
arm64e slices, macOS 13 platform, dylib filetype and the exact chroot
LC_ID_DYLIB.  It also reconstructs the fat payload from package-staged thin
companions because Procursus dpkg can thin a fat payload during unpack.

Read-only inspection is used by the package contract.  ``stage`` is a bounded
build step; ``publish`` is the postinstall transaction and never creates a
shadow rootfs or touches the native /var/jb/Library substrate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

FAT_MAGIC = 0xCAFEBABE
FAT_MAGIC_64 = 0xCAFEBABF
MH_MAGIC_64 = 0xFEEDFACF
MH_CIGAM_64 = 0xCFFAEDFE
MH_DYLIB = 6

CPU_TYPE_ARM64 = 0x0100000C
CPU_SUBTYPE_MASK = 0x00FFFFFF
ARM64_SUBTYPE = 0
ARM64E_SUBTYPE = 2

LC_ID_DYLIB = 0x0D
LC_BUILD_VERSION = 0x32
LC_VERSION_MIN_MACOSX = 0x24
LC_VERSION_MIN_IPHONEOS = 0x25

PLATFORM_MACOS = 1
PLATFORM_IOS = 2

PROVIDER_NAME = "CydiaSubstrate"
FRAMEWORK_NAME = "CydiaSubstrate.framework"
REQUIRED_ARCHS = ("arm64", "arm64e")
INSTALL_NAME = "/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate"
PACKAGE_PROVIDER_DIR = "/var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework"
CHROOT_PROVIDER_DIR = "/System/Library/Frameworks/CydiaSubstrate.framework"

ARCH_NAMES = {
    (CPU_TYPE_ARM64, ARM64_SUBTYPE): "arm64",
    (CPU_TYPE_ARM64, ARM64E_SUBTYPE): "arm64e",
    (0x01000007, 3): "x86_64",
}


class ProviderError(ValueError):
    pass


@dataclass(frozen=True)
class SliceInfo:
    arch: str
    cputype: int
    cpusubtype: int
    offset: int
    size: int
    filetype: int
    platform: int
    minos: Tuple[int, int, int]
    install_name: Optional[str]
    sha256: str

    def describe(self) -> str:
        version = "%d.%d.%d" % self.minos
        return "%s platform=%d minos=%s install_name=%s" % (
            self.arch, self.platform, version, self.install_name)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _arch_name(cputype: int, cpusubtype: int) -> str:
    return ARCH_NAMES.get(
        (cputype, cpusubtype & CPU_SUBTYPE_MASK),
        "cpu%x_%x" % (cputype & 0xFFFFFFFF, cpusubtype & CPU_SUBTYPE_MASK))


def _decode_version(value: int) -> Tuple[int, int, int]:
    return ((value >> 16) & 0xFFFF, (value >> 8) & 0xFF, value & 0xFF)


def _parse_thin(data: bytes, offset: int, size: int, label: str) -> SliceInfo:
    if size < 32:
        raise ProviderError("%s: slice is too small" % label)
    little = struct.unpack_from("<I", data, offset)[0]
    if little == MH_MAGIC_64:
        endian = "<"
    elif struct.unpack_from(">I", data, offset)[0] == MH_MAGIC_64:
        endian = ">"
    else:
        raise ProviderError("%s: not a 64-bit Mach-O slice" % label)
    (magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds,
     _flags, _reserved) = struct.unpack_from(endian + "IiiIIIII", data, offset)
    if filetype != MH_DYLIB:
        raise ProviderError("%s: filetype %d is not MH_DYLIB" % (label, filetype))
    if ncmds > 1024 or sizeofcmds > size - 32:
        raise ProviderError("%s: invalid load-command bounds" % label)
    cmd_offset = offset + 32
    cmd_end = cmd_offset + sizeofcmds
    platform = None
    minos = None
    install_name = None
    for _ in range(ncmds):
        if cmd_offset + 8 > cmd_end:
            raise ProviderError("%s: truncated load command" % label)
        cmd, cmdsize = struct.unpack_from(endian + "II", data, cmd_offset)
        if cmdsize < 8 or cmd_offset + cmdsize > cmd_end:
            raise ProviderError("%s: invalid load command size" % label)
        if cmd == LC_BUILD_VERSION:
            if cmdsize < 24:
                raise ProviderError("%s: short LC_BUILD_VERSION" % label)
            build_platform, raw_minos, _sdk, ntools = struct.unpack_from(
                endian + "IIII", data, cmd_offset + 8)
            if ntools and 24 + ntools * 8 > cmdsize:
                raise ProviderError("%s: invalid LC_BUILD_VERSION tools" % label)
            platform = build_platform
            minos = _decode_version(raw_minos)
        elif cmd == LC_VERSION_MIN_IPHONEOS:
            platform = PLATFORM_IOS
            minos = _decode_version(
                struct.unpack_from(endian + "I", data, cmd_offset + 8)[0])
        elif cmd == LC_VERSION_MIN_MACOSX:
            platform = PLATFORM_MACOS
            minos = _decode_version(
                struct.unpack_from(endian + "I", data, cmd_offset + 8)[0])
        elif cmd == LC_ID_DYLIB:
            if cmdsize < 24:
                raise ProviderError("%s: short LC_ID_DYLIB" % label)
            name_offset = struct.unpack_from(
                endian + "I", data, cmd_offset + 8)[0]
            if name_offset < 24 or name_offset >= cmdsize:
                raise ProviderError("%s: invalid LC_ID_DYLIB name offset" % label)
            raw = data[cmd_offset + name_offset:cmd_offset + cmdsize]
            nul = raw.find(b"\0")
            if nul < 0:
                raise ProviderError("%s: unterminated LC_ID_DYLIB" % label)
            install_name = raw[:nul].decode("utf-8", "strict")
        cmd_offset += cmdsize
    if platform is None or minos is None:
        raise ProviderError("%s: no LC_BUILD_VERSION/LC_VERSION_MIN" % label)
    slice_bytes = data[offset:offset + size]
    return SliceInfo(
        arch=_arch_name(cputype, cpusubtype),
        cputype=cputype,
        cpusubtype=cpusubtype,
        offset=offset,
        size=size,
        filetype=filetype,
        platform=platform,
        minos=minos,
        install_name=install_name,
        sha256=sha256_bytes(slice_bytes),
    )


def inspect_path(path: Path) -> List[SliceInfo]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ProviderError("%s: cannot read provider: %s" % (path, error))
    if len(data) < 8:
        raise ProviderError("%s: not a Mach-O file" % path)
    if struct.unpack_from(">I", data, 0)[0] in (FAT_MAGIC, FAT_MAGIC_64):
        is64 = struct.unpack_from(">I", data, 0)[0] == FAT_MAGIC_64
        count = struct.unpack_from(">I", data, 4)[0]
        if count == 0 or count > 8:
            raise ProviderError("%s: invalid fat architecture count" % path)
        entry_size = 32 if is64 else 20
        if 8 + count * entry_size > len(data):
            raise ProviderError("%s: truncated fat header" % path)
        slices = []
        for index in range(count):
            base = 8 + index * entry_size
            if is64:
                cputype, cpusubtype, offset, size, _align, _res = \
                    struct.unpack_from(">IIQQII", data, base)
            else:
                cputype, cpusubtype, offset, size, _align = \
                    struct.unpack_from(">IIIII", data, base)
            if offset + size > len(data) or size < 32:
                raise ProviderError("%s: fat slice out of bounds" % path)
            info = _parse_thin(data, offset, size,
                               "%s[%s]" % (path, _arch_name(cputype, cpusubtype)))
            if (info.cputype, info.cpusubtype & CPU_SUBTYPE_MASK) != \
                    (cputype, cpusubtype & CPU_SUBTYPE_MASK):
                raise ProviderError("%s: fat table and slice disagree" % path)
            slices.append(info)
        return slices
    return [_parse_thin(data, 0, len(data), str(path))]


def _check_slice(info: SliceInfo, label: str, expected_arch: Optional[str] = None) -> None:
    if expected_arch is not None and info.arch != expected_arch:
        raise ProviderError("%s: expected %s slice, found %s" %
                            (label, expected_arch, info.arch))
    if info.arch not in REQUIRED_ARCHS:
        raise ProviderError("%s: unsupported architecture %s" %
                            (label, info.arch))
    if info.platform != PLATFORM_MACOS:
        raise ProviderError("%s: platform %d is not macOS" %
                            (label, info.platform))
    if info.minos[0] != 13:
        raise ProviderError("%s: minimum macOS is %d.%d.%d, expected 13.x" %
                            (label, info.minos[0], info.minos[1], info.minos[2]))
    if info.install_name != INSTALL_NAME:
        raise ProviderError("%s: LC_ID_DYLIB is %r, expected %r" %
                            (label, info.install_name, INSTALL_NAME))


def verify_fat(path: Path) -> List[SliceInfo]:
    slices = inspect_path(path)
    if sorted(slice_info.arch for slice_info in slices) != sorted(REQUIRED_ARCHS):
        raise ProviderError("%s: provider must contain exactly arm64 and arm64e" % path)
    for info in slices:
        _check_slice(info, str(path), info.arch)
    if len({info.arch for info in slices}) != len(slices):
        raise ProviderError("%s: duplicate provider architecture" % path)
    return slices


def verify_thin(path: Path, expected_arch: str) -> SliceInfo:
    slices = inspect_path(path)
    if len(slices) != 1:
        raise ProviderError("%s: expected one thin slice" % path)
    _check_slice(slices[0], str(path), expected_arch)
    return slices[0]


def _provider_paths(directory: Path) -> Dict[str, Path]:
    return {
        "main": directory / PROVIDER_NAME,
        "arm64": directory / (PROVIDER_NAME + ".arm64"),
        "arm64e": directory / (PROVIDER_NAME + ".arm64e"),
    }


def verify_bundle(directory: Path, strict_slice_hashes: bool = False) -> Dict[str, object]:
    paths = _provider_paths(directory)
    for label, path in paths.items():
        if not path.exists():
            raise ProviderError("missing provider payload %s" % path)
        if path.is_symlink() or not path.is_file():
            raise ProviderError("provider payload is not a regular file: %s" % path)
    main_slices = inspect_path(paths["main"])
    companions = {
        "arm64": verify_thin(paths["arm64"], "arm64"),
        "arm64e": verify_thin(paths["arm64e"], "arm64e"),
    }
    main_by_arch = {info.arch: info for info in main_slices}
    for arch, companion in companions.items():
        if arch in main_by_arch:
            main = main_by_arch[arch]
            _check_slice(main, str(paths["main"]), arch)
            if strict_slice_hashes and main.sha256 != companion.sha256:
                raise ProviderError(
                    "%s: %s slice differs from thin companion" %
                    (paths["main"], arch))
    if set(main_by_arch) - set(REQUIRED_ARCHS):
        raise ProviderError("%s: unsupported slice in provider" %
                            paths["main"])
    if not main_by_arch:
        raise ProviderError("%s: provider has no arm slices" % paths["main"])
    return {
        "directory": str(directory),
        "main": str(paths["main"]),
        "main_sha256": sha256_file(paths["main"]),
        "companions": {
            arch: {
                "path": str(paths[arch]),
                "sha256": sha256_file(paths[arch]),
                "slice_sha256": companions[arch].sha256,
            }
            for arch in REQUIRED_ARCHS
        },
    }


def _run(argv: List[str]) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(argv, text=True, capture_output=True)
    except OSError as error:
        raise ProviderError("cannot run %s: %s" % (argv[0], error))


def _require_success(label: str, result: subprocess.CompletedProcess) -> None:
    if result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        raise ProviderError("%s failed (exit %d): %s" %
                            (label, result.returncode, detail[:400]))


def _ldid_cdhash(ldid: Path, arch: str, path: Path) -> str:
    result = _run([str(ldid), "-arch", arch, "-h", str(path)])
    _require_success("ldid -h %s" % arch, result)
    for line in result.stdout.splitlines():
        if line.startswith("CDHash="):
            value = line.split("=", 1)[1].strip()
            if value:
                return value
    raise ProviderError("ldid produced no CDHash for %s %s" % (arch, path))


def _trust_hashes(jbctl: Path, hashes: Iterable[str], path: Path) -> None:
    hashes = list(hashes)
    for digest in hashes:
        result = _run([str(jbctl), "trustcache", "add", digest])
        info = _run([str(jbctl), "trustcache", "info"])
        _require_success("jbctl trustcache info", info)
        live = digest.lower() in info.stdout.lower()
        if result.returncode != 0 and not live:
            detail = (result.stderr or result.stdout or "").strip()
            raise ProviderError(
                "mandatory trustcache add failed for %s [%s]: %s" %
                (path, digest, detail[:300]))
        if not live:
            raise ProviderError(
                "CDHash is not live in the trustcache for %s: %s" %
                (path, digest))


def stage(binary: Path, destination: Path, lipo: Path) -> Dict[str, object]:
    verify_fat(binary)
    destination.mkdir(parents=True, exist_ok=True)
    paths = _provider_paths(destination)
    for path in paths.values():
        if path.exists() or path.is_symlink():
            path.unlink()
    # Copy the exact built image; companions are derived from it below.
    paths["main"].write_bytes(binary.read_bytes())
    for arch in REQUIRED_ARCHS:
        result = _run([str(lipo), str(binary), "-thin", arch,
                       "-output", str(paths[arch])])
        _require_success("lipo -thin %s" % arch, result)
        verify_thin(paths[arch], arch)
    for path in paths.values():
        path.chmod(0o755)
    report = verify_bundle(destination, strict_slice_hashes=True)
    report["build"] = {
        "path": str(binary),
        "sha256": sha256_file(binary),
        "slices": [info.describe() for info in inspect_path(binary)],
    }
    return report


def publish(source_dir: Path, rootfs: Path, entitlements: Path,
            lipo: Path, ldid: Path, jbctl: Path) -> Dict[str, object]:
    rootfs = Path(rootfs)
    if not (rootfs / "System/Library").is_dir():
        raise ProviderError("rootfs is not mounted: %s" % rootfs)
    target_dir = rootfs / CHROOT_PROVIDER_DIR.lstrip("/")
    if target_dir.is_symlink():
        raise ProviderError("provider target directory is a symlink: %s" %
                            target_dir)
    if not target_dir.is_dir():
        raise ProviderError("provider target directory is missing: %s" %
                            target_dir)
    target = target_dir / PROVIDER_NAME
    if target.is_symlink():
        raise ProviderError("provider target is a symlink: %s" % target)
    if not entitlements.is_file():
        raise ProviderError("missing project entitlements: %s" % entitlements)
    bundle = verify_bundle(Path(source_dir), strict_slice_hashes=False)
    paths = _provider_paths(Path(source_dir))
    temporary = target_dir / (".%s.new-%d" % (PROVIDER_NAME, os.getpid()))
    if temporary.exists() or temporary.is_symlink():
        temporary.unlink()
    try:
        result = _run([str(lipo), "-create", str(paths["arm64"]),
                       str(paths["arm64e"]), "-output", str(temporary)])
        _require_success("lipo -create", result)
        verify_fat(temporary)
        for _ in range(2):
            result = _run([str(ldid), "-S%s" % entitlements, "-M",
                           str(temporary)])
            _require_success("ldid signing", result)
        slices = verify_fat(temporary)
        hashes = [_ldid_cdhash(ldid, arch, temporary)
                  for arch in REQUIRED_ARCHS]
        if len(set(hashes)) != len(hashes):
            raise ProviderError("ldid returned duplicate provider CDHashes")
        _trust_hashes(jbctl, hashes, temporary)
        final_sha = sha256_file(temporary)
        os.replace(str(temporary), str(target))
        verify_fat(target)
        if sha256_file(target) != final_sha:
            raise ProviderError("provider changed during atomic publish")
    finally:
        if temporary.exists() or temporary.is_symlink():
            temporary.unlink()
    return {
        "target": str(target),
        "sha256": final_sha,
        "cdhashes": dict(zip(REQUIRED_ARCHS, hashes)),
        "slices": [info.describe() for info in slices],
        "source": bundle,
    }


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="action", required=True)

    verify_build = subparsers.add_parser("verify-build")
    verify_build.add_argument("--binary", required=True, type=Path)

    verify_bundle_parser = subparsers.add_parser("verify-bundle")
    verify_bundle_parser.add_argument("--directory", required=True, type=Path)

    stage_parser = subparsers.add_parser("stage")
    stage_parser.add_argument("--binary", required=True, type=Path)
    stage_parser.add_argument("--destination", required=True, type=Path)
    stage_parser.add_argument("--lipo", required=True, type=Path)

    publish_parser = subparsers.add_parser("publish")
    publish_parser.add_argument("--source-dir", required=True, type=Path)
    publish_parser.add_argument("--rootfs", required=True, type=Path)
    publish_parser.add_argument("--entitlements", required=True, type=Path)
    publish_parser.add_argument("--lipo", required=True, type=Path)
    publish_parser.add_argument("--ldid", required=True, type=Path)
    publish_parser.add_argument("--jbctl", required=True, type=Path)

    args = parser.parse_args(argv)
    try:
        if args.action == "verify-build":
            slices = verify_fat(args.binary)
            report = {
                "binary": str(args.binary),
                "sha256": sha256_file(args.binary),
                "slices": [info.describe() for info in slices],
            }
        elif args.action == "verify-bundle":
            report = verify_bundle(args.directory, strict_slice_hashes=True)
        elif args.action == "stage":
            report = stage(args.binary, args.destination, args.lipo)
        else:
            report = publish(args.source_dir, args.rootfs, args.entitlements,
                             args.lipo, args.ldid, args.jbctl)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    except ProviderError as error:
        print("ERROR: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
