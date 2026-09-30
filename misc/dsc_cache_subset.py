#!/usr/bin/env python3
"""dsc_cache_subset.py - compute a closed dylib subset for a shared cache rebuild.

Why: the macOS 15.6.1 shared cache spans ~4.77 GB, which does not fit the iPad's
4 GB shared region, and the cache's own metadata (functionVariantInfoAddr,
dylibsPBLSetAddr, programTrieAddr) points into the .01 tail that lives beyond the
region, so a full cache can never be mapped there.  Rebuilding a *smaller* cache
from a closed subset of dylibs is the only in-version fix.

Usage:
    dsc_cache_subset.py closure <seeds...> --search DIR [--search DIR ...]
                                 [--include-extra PATH ...] --out LIST
    dsc_cache_subset.py deps    <macho> ...

`closure` walks LC_LOAD_DYLIB transitively: a dependency is satisfied by a file
found under one of the --search dirs (looked up by install name, e.g.
/usr/lib/libSystem.B.dylib -> DIR/usr/lib/libSystem.B.dylib).  Unresolved
dependencies are reported so the search dirs can be extended.
"""
import os
import struct
import sys

LC_LOAD_DYLIB = 0xC
LC_ID_DYLIB = 0xD
LC_LOAD_WEAK_DYLIB = 0x18 | 0x80000000
LC_REEXPORT_DYLIB = 0x1F | 0x80000000
LC_LAZY_LOAD_DYLIB = 0x20
LC_LOAD_UPWARD_DYLIB = 0x23 | 0x80000000


def macho_slices(data):
    """Yield (offset, size) for each thin slice (fat-aware)."""
    magic_be = struct.unpack_from(">I", data, 0)[0]
    if magic_be in (0xCAFEBABE, 0xCAFEBABF):
        n = struct.unpack_from(">I", data, 4)[0]
        for i in range(n):
            _ct, _cs, off, size, _al = struct.unpack_from(">IIIII", data, 8 + 20 * i)
            yield off, size
    else:
        yield 0, len(data)


def load_commands(data, offset):
    magic = struct.unpack_from("<I", data, offset)[0]
    if magic not in (0xFEEDFACF, 0xFEEDFACE):
        return
    ncmds = struct.unpack_from("<I", data, offset + 16)[0]
    c = offset + (32 if magic == 0xFEEDFACF else 28)
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, c)
        yield cmd, c, size
        c += size


def install_name(data, offset):
    for cmd, c, size in load_commands(data, offset):
        if cmd == LC_ID_DYLIB:
            name_off = struct.unpack_from("<I", data, c + 8)[0]
            end = data.find(b"\0", c + name_off)
            return data[c + name_off:end].decode("utf-8", "replace")
    return None


def deps(data, offset):
    out = []
    for cmd, c, size in load_commands(data, offset):
        if cmd in (LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB,
                   LC_LAZY_LOAD_DYLIB, LC_LOAD_UPWARD_DYLIB):
            name_off = struct.unpack_from("<I", data, c + 8)[0]
            end = data.find(b"\0", c + name_off)
            out.append(data[c + name_off:end].decode("utf-8", "replace"))
    return out


def read_deps(path):
    with open(path, "rb") as fh:
        data = fh.read()
    all_deps = []
    for off, size in macho_slices(data):
        all_deps.extend(deps(data, off))
    return all_deps


def resolve(name, search_dirs):
    rel = name.lstrip("/")
    for d in search_dirs:
        p = os.path.join(d, rel)
        if os.path.isfile(p):
            return p
    return None


def main():
    if len(sys.argv) < 4 or sys.argv[1] not in ("closure", "deps"):
        print(__doc__)
        return 2

    mode = sys.argv[1]
    args = sys.argv[2:]
    seeds, search_dirs, extra, out_path = [], [], [], None
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--search":
            i += 1
            search_dirs.append(os.path.abspath(args[i]))
        elif a == "--include-extra":
            i += 1
            extra.append(args[i])
        elif a == "--out":
            i += 1
            out_path = args[i]
        else:
            seeds.append(a)
        i += 1

    if mode == "deps":
        for s in seeds:
            print("%s ->" % s)
            for d in read_deps(s):
                print("    " + d)
        return 0

    if not search_dirs:
        print("closure needs at least one --search DIR", file=sys.stderr)
        return 2

    for e in extra:
        if not os.path.isfile(e):
            print("extra path not found: %s" % e, file=sys.stderr)
            return 2

    names = set()
    queue = []
    for s in seeds:
        if os.path.isfile(s):
            with open(s, "rb") as fh:
                data = fh.read()
            queue.append(s)          # seed file itself is part of the subset
            for off, _size in macho_slices(data):
                queue.extend(deps(data, off))
        else:
            queue.append(s)          # install name
    for e in extra:
        queue.append(e)

    unresolved, missing_files = set(), set()
    while queue:
        item = queue.pop()
        if item.startswith("/"):
            path = resolve(item, search_dirs)
            if path is None:
                unresolved.add(item)
                continue
        else:
            path = item if os.path.isfile(item) else None
            if path is None:
                missing_files.add(item)
                continue
        name = install_name_path = item if item.startswith("/") else None
        if item.startswith("/"):
            name = item
        else:
            with open(path, "rb") as fh:
                data = fh.read()
            name = install_name(data, 0) or path
        if (path, name) in names:
            continue
        names.add((path, name))
        for d in read_deps(path):
            if d.startswith("/") and resolve(d, search_dirs) is None:
                unresolved.add(d)
            else:
                queue.append(d)

    lines = sorted({"%s\t%s" % (name, path) for path, name in names})
    if out_path:
        with open(out_path, "w") as fh:
            fh.write("\n".join(lines) + "\n")
    print("resolved dylibs: %d" % len(lines))
    if unresolved:
        print("UNRESOLVED (add --search dirs or --include-extra): %d" % len(unresolved))
        for u in sorted(unresolved)[:40]:
            print("   " + u)
    if missing_files:
        print("missing seed files: %s" % sorted(missing_files)[:10])
    if out_path:
        print("wrote %s" % out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
