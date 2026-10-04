#!/usr/bin/env python3
"""Bounded, read-only inventory for macOS 15 experiments on the iPad.

The script is intentionally local-first: run it on the device in the
background, then copy the JSONL log back over SSH. It never deletes files and
never recursively walks the whole /var/jb/usr/macOS tree.
"""
import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import time

DEFAULT_ROOTS = (
    "/var/mobile",
    "/var/jb/var/mobile",
    "/tmp",
    "/var/tmp",
    "/var/jb/tmp",
    "/var/mnt/rootfs/private/tmp",
    "/var/mnt/rootfs/var/db/macws/boot-trust",
)
NAME_RE = re.compile(
    r"(?:dyld|kernel|fmt13|pmap|pvn|pvdiag|csprobe|pagewalk|run_dbg|"
    r"run_nocskill|vadiff|triage|mpriv|mregions|sprobe|srteardown|sr536|"
    r"cache_page|exec_fault|sysmain|typeab|nullab|sizeab|owner_typefix|"
    r"24g90|dsc|trust)",
    re.IGNORECASE,
)


def digest(path, limit):
    if limit <= 0:
        return None
    h = hashlib.sha256()
    try:
        with open(path, "rb") as stream:
            remaining = limit
            while remaining:
                chunk = stream.read(min(1024 * 1024, remaining))
                if not chunk:
                    break
                h.update(chunk)
                remaining -= len(chunk)
        return h.hexdigest() if remaining > 0 else "truncated"
    except OSError as exc:
        return "error:" + str(exc)


def record(path, method, hash_limit):
    try:
        info = os.lstat(path)
    except OSError as exc:
        return {"path": path, "method": method, "error": str(exc)}
    if not stat.S_ISREG(info.st_mode):
        return None
    item = {
        "path": path,
        "method": method,
        "inode": info.st_ino,
        "size": info.st_size,
        "mode": oct(info.st_mode & 0o7777),
        "uid": info.st_uid,
        "gid": info.st_gid,
        "mtime_ns": info.st_mtime_ns,
    }
    if info.st_size <= hash_limit:
        item["sha256"] = digest(path, hash_limit)
    return item


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--max-files", type=int, default=4000)
    parser.add_argument("--hash-limit", type=int, default=128 * 1024 * 1024)
    args = parser.parse_args()
    started = time.monotonic()
    deadline = started + max(1.0, args.seconds)
    count = 0
    truncated = False
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as out:
        def emit(item):
            if item is not None:
                out.write(json.dumps(item, ensure_ascii=False, sort_keys=True) + "\n")

        emit({"kind": "meta", "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
              "seconds": args.seconds, "max_files": args.max_files,
              "roots": list(DEFAULT_ROOTS)})
        for root in DEFAULT_ROOTS:
            if time.monotonic() >= deadline or count >= args.max_files:
                truncated = True
                break
            if not os.path.isdir(root):
                emit({"kind": "root", "root": root, "exists": False})
                continue
            # Avoid broad runtime traversal: only inspect matching names under
            # the small, explicitly listed roots.
            for directory, dirs, names in os.walk(root, topdown=True, followlinks=False):
                dirs[:] = [d for d in dirs if d not in (".git", "__pycache__")]
                for name in names:
                    if time.monotonic() >= deadline or count >= args.max_files:
                        truncated = True
                        break
                    path = os.path.join(directory, name)
                    if not NAME_RE.search(name) and not NAME_RE.search(path):
                        continue
                    emit(record(path, "bounded-tree", args.hash_limit))
                    count += 1
                if truncated:
                    break
            if truncated:
                break
        try:
            processes = subprocess.run(
                ["ps", "-axo", "pid,ppid,state,etime,comm"],
                capture_output=True, text=True, timeout=3, check=False,
            ).stdout
            emit({"kind": "processes", "text": processes})
        except (OSError, subprocess.SubprocessError) as exc:
            emit({"kind": "processes", "error": str(exc)})
        emit({"kind": "summary", "files": count, "truncated": truncated,
              "elapsed": round(time.monotonic() - started, 3)})


if __name__ == "__main__":
    main()
