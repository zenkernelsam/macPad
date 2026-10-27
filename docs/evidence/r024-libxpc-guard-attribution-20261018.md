# R024 — exact 22F82 libxpc guard attribution: acquisition blocked

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-024-frontend-to-worker.md`. Read-only device/local
scope: no package/source/build/Git change, no WindowServer generation, no
policy/TCC/trustcache change. This round attempted the authorized read-only
acquisition and stopped at the missing-input boundary. No remote command was
executed and no device file was read or written.

## 1. Access attempt and result

Checked the credential variable names only, without printing values:

```text
MACWS_DEVICE          unset
MACWS_DEVICE_PORT     unset
MACWS_SSH_PASSWORD    unset
```

`~/.ssh/config` contains exactly one configured host alias; no identity key
files are present under `~/.ssh`. `sshpass` exists, but no password was
available in the environment. `iproxy` is not installed and no local USB
forward listener was open.

Bounded, non-interactive key-auth reachability checks against the single
configured host:

| Port | Result | Exit |
| --- | --- | --- |
| 22 | connect timeout (Operation timed out) | 255 |
| 2222 | Host is down | 255 |
| 22222 | Host is down | 255 |

The checks used `BatchMode=yes`, `StrictHostKeyChecking=no` and
`UserKnownHostsFile=/dev/null`; no host key or credential was written, and no
login occurred. The exact host address is deliberately not recorded here.

**Status:** the "already reachable iPad" is not reachable from this worker's
environment, and the authorized credential/endpoint variables are absent.
The exact acquisition cannot proceed.

## 2. Local fallback inventory (unchanged from R023)

The local 22F82 material was re-checked:

- `analysis/r011-xpc-crash/cache-map-head.txt` is a 42-line head that stops at
  the libxpc `__TEXT 0x180136000 -> 0x18017A000` row. It does not contain the
  image owning cache offset `0x3CEB78`.
- The only exact 22F82 libxpc bytes are the 256-byte
  `libxpc-bootstrap-entry-180ae1140.bin` at cache file
  `0x145140`-`0x145240` (image offset `0xf140`). The guard cluster
  `0x13BE00`-`0x141B00` is absent.
- The frame-0 bytes at cache `0x3CEB78 ± 0x100` are absent.
- The crash report is a lightweight corpse and carries no `os_reason`
  reason string.
- `analysis/ios163cache/dyld_shared_cache_arm64e*` is the iOS 16.3 cache, not
  the 22F82 macOS cache, and is not used.
- No full 22F82 `dyld_shared_cache_arm64e.map` or cache file is present
  locally.

No new 22F82 bytes, map rows, image ownership, or reason string were added in
this round.

## 3. Exact result that still stands (R023, local only)

```text
libxpc UUID        F3F19227-FF8F-389C-A094-6F4C16E458AF
__TEXT             unslid 0x180136000 -> 0x18017A000, fileoff 0x136000,
                   size 0x44000, text SHA-256 af2bbe7c…0db5
runtime slide      0x99c000
guard report       imageIndex 0 = shared cache base 0x180000000;
                   frame offsets are cache-relative/file offsets
frame 0            0x3CEB78: outside libxpc, owning image unresolved
frame 1            0x13BF8C -> libxpc image offset 0x5F8C
frame 2            0x141928 -> libxpc image offset 0xB928
frame 3            0x1417B0 -> libxpc image offset 0xB7B0
frame 4            0x1410E4 -> libxpc image offset 0xB0E4
frame 5            0x140C84 -> libxpc image offset 0xAC84
```

These are offset mappings only. No exact guard disassembly, function name,
call-site, or reason-string construction is available. R022's 15.6 handler
body is not used as a 22F82 fact, and frame 0 is not assigned to libxpc.

## 4. Exact missing-input manifest for a future read-only acquisition

All cache offsets below are main-cache file offsets (equal to the guard
report's cache-relative `imageOffset`), not runtime addresses.

1. **Reachable endpoint and credential in the worker environment.**
   `MACWS_DEVICE` (user@host), `MACWS_DEVICE_PORT`, and either a working
   `MACWS_SSH_PASSWORD` or a key-based login must be present for
   `DevinWatchdog_mac/macpad_ssh.py`. The endpoint must be reachable without
   starting a WindowServer generation. No credential value belongs in this
   repository.
2. **Full 22F82 map or frame-0 rows.**
   `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.map`
   (874,394 B, expected SHA-256 `4c817de2…2088`). Minimum: the image header
   and segment rows whose unslid range contains `0x1803CEB78`, including the
   image name/UUID.
3. **Exact libxpc bytes.**
   Preferred full `__TEXT` `0x136000`-`0x17A000` (278,528 B), verify SHA-256
   `af2bbe7c88c8be38aedef36d80410db39c4376cc1f9ebd0b0cf2b052582b0db5`.
   Minimum focused range `0x13BE00`-`0x141B00` (23,552 B), covering all five
   resolved guard frames with context.
4. **Frame-0 bytes.**
   After (2) identifies the owning image/segment, read
   `0x3CEB78 ± 0x100` and that image's Mach-O header/`LC_UUID` so the bytes
   can be attributed to an exact UUID/build.
5. **Reason string.**
   A non-lightweight crash report, ReportCrash/launchd line, or device
   `log`/`oslog` entry for the same 18:24:44 event that preserves the
   `os_reason` reason string; or exact libxpc literal/`__cstring` bytes
   reached from the fault call in (3). Do not infer the message from 15.6.
6. **Optional function table.**
   Per-image function starts/symbols for libxpc image offsets
   `0xAC84`-`0xB928` to name frames 2-5. The R011 cache-wide `LC_SYMTAB`
   values are not a substitute.
7. **Optional full chain.** Map rows and exact bytes for frames 6+
   (`0x250400`, `0x25F97C`, `0x7521684`, `0x7503B00`, ...) if the complete
   caller chain is required; they are outside libxpc.

## 5. Limits

- No device login, file read, or remote command was performed. The only
  device-facing actions were three bounded TCP/key-auth connection attempts
  that failed before authentication.
- No new artifact exists; this note records the blocked acquisition and the
  exact manifest.
- No claim is made about the misuse message, benignity, or a fix. The
  `LIBXPC`/reason-9 tuple and the frame 1-5 offset mapping remain the exact
  local results; frame 0 and the reason string remain unresolved.

(handover to codex now)
