#!/usr/bin/env python3
"""Read-only, non-recursive device inventory for the Ventura handoff.

Run this on the iPad, then copy the JSONL file back over SSH.  It intentionally
looks only at directory entries in the listed roots; it never walks the
shared macOS runtime or computes hashes of large cache files.
"""
import argparse
import hashlib
import json
import os
import stat
import subprocess
import time

DEFAULT_ROOTS = (
    "/var/mobile",
    "/var/jb/var/mobile",
    "/var/jb/tmp",
    "/var/mnt/rootfs/private/tmp",
)


def digest(path, limit):
    h = hashlib.sha256()
    remaining = limit
    try:
        with open(path, "rb") as stream:
            while remaining:
                chunk = stream.read(min(1024 * 1024, remaining))
                if not chunk:
                    break
                h.update(chunk)
                remaining -= len(chunk)
    except OSError as exc:
        return "error:" + str(exc)
    return h.hexdigest() if remaining > 0 else "truncated"


def entry(path, hash_limit):
    try:
        info = os.lstat(path)
    except OSError as exc:
        return {"path": path, "error": str(exc)}
    item = {
        "path": path,
        "name": os.path.basename(path),
        "inode": info.st_ino,
        "mode": oct(info.st_mode & 0o7777),
        "type": stat.S_IFMT(info.st_mode),
        "size": info.st_size,
        "uid": info.st_uid,
        "gid": info.st_gid,
        "mtime_ns": info.st_mtime_ns,
    }
    if stat.S_ISREG(info.st_mode) and info.st_size <= hash_limit:
        item["sha256"] = digest(path, hash_limit)
    return item


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--hash-limit", type=int, default=128 * 1024)
    args = parser.parse_args()
    started = time.monotonic()
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as out:
        def emit(value):
            out.write(json.dumps(value, sort_keys=True) + "\n")

        emit({"kind": "meta", "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
              "roots": list(DEFAULT_ROOTS), "recursive": False,
              "hash_limit": args.hash_limit})
        count = 0
        for root in DEFAULT_ROOTS:
            if not os.path.isdir(root):
                emit({"kind": "root", "root": root, "exists": False})
                continue
            emit({"kind": "root", "root": root, "exists": True})
            try:
                names = sorted(os.listdir(root))
            except OSError as exc:
                emit({"kind": "root_error", "root": root, "error": str(exc)})
                continue
            for name in names:
                emit(entry(os.path.join(root, name), args.hash_limit))
                count += 1
        try:
            proc = subprocess.run(
                ["ps", "-axo", "pid,ppid,state,etime,command"],
                capture_output=True, text=True, timeout=3, check=False,
            )
            emit({"kind": "processes", "returncode": proc.returncode,
                  "text": proc.stdout})
        except (OSError, subprocess.SubprocessError) as exc:
            emit({"kind": "processes", "error": str(exc)})
        emit({"kind": "summary", "entries": count,
              "elapsed": round(time.monotonic() - started, 3)})


if __name__ == "__main__":
    main()
