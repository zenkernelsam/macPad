# R023 — exact-build libxpc guard frame attribution

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-023-to-frontend.md`. Read-only local work only: no
SSH/device, build, dependency, source/ABI/TCC change or Git write. R022's
15.6 libxpc handler body is **not** used as a fact here.

Inputs: `analysis/r011-xpc-crash/` (cache map head, cache-read JSON, the
256-byte libxpc entry capture and its disassembly), the R010/R012/R013 crash
reports, `docs/evidence/r011-xpc-bootstrap-crash-attribution-20261018.md`,
`docs/evidence/r017-xpc-bootstrap-local-audit-20261018.md` and R022.

## 1. Exact 22F82 libxpc identity and mapping

From `analysis/r011-xpc-crash/cache-read.json` (cache UUID
`BE59BB71-7B69-3EFC-86A8-5625A077AE2E`, cache header SHA-256
`b78ba265…3c63`; map SHA-256 `4c817de2…2088`, size 874,394 B):

```text
image UUID        F3F19227-FF8F-389C-A094-6F4C16E458AF
id dylib          /usr/lib/system/libxpc.dylib
image_fileoff     1269760 = 0x136000
__TEXT vmaddr     0x180136000
__TEXT fileoff    0x136000
__TEXT vmsize     0x44000 (278,528 B)
__TEXT range      0x180136000 -> 0x18017A000   (cache-map-head.txt:42)
runtime slide     0x99c000
__TEXT SHA-256    af2bbe7c88c8be38aedef36d80410db39c4376cc1f9ebd0b0cf2b052582b0db5
```

The main cache mapping 0 is fileoff 0 / address `0x180000000`, so for these
guard reports **cache-relative offset = main-cache file offset**. The libxpc
image-relative offset is:

```text
libxpc_image_offset = cache_relative_offset - 0x136000
```

The `_xpc_bootstrap_services` function is unrelated to the guard frames:
image offset `0xf140`, cache file offset `0x145140`, runtime address
`0x180ae1140` (slide applied). The only exact libxpc bytes stored locally are
256 bytes at cache file `0x145140`-`0x145240`
(`libxpc-bootstrap-entry-180ae1140.bin`, SHA-256
`b6c7c5f257a34e5bae662e9fe60485de9b2cafb08e39848b786936c5395c6796`),
already disassembled in R011.

## 2. Guard report imageIndex semantics

In `ExcUserFault_codesign-2026-10-18-182444.ips` and the preserved R012 raw
reports, `usedImages[0]` is the 22F82 shared cache:

```text
base 0x180000000, size 0x40000000 (1 GB),
uuid BE59BB71-7B69-3EFC-86A8-5625A077AE2E
```

All common guard-prefix `frames[].imageOffset` values are relative to this
cache image, not to the libxpc image. They must not be confused with the
`_xpc_bootstrap_services` crash's `imageOffset 0xf140`, where imageIndex 0 was
libxpc itself in a different process/report.

## 3. Common guard frame prefix resolved against the exact libxpc range

The identical prefix in the codesign report and the R012 `pkd`,
`SystemUIServer` and `OSXvnc-server` reports begins:

```text
frame 0  cache 0x3CEB78
frame 1  cache 0x13BF8C
frame 2  cache 0x141928
frame 3  cache 0x1417B0
frame 4  cache 0x1410E4
frame 5  cache 0x140C84
```

Using the exact `__TEXT` range `0x136000 -> 0x17A000`:

| Frame | Cache-relative | In libxpc `__TEXT`? | libxpc image offset |
| --- | --- | --- | --- |
| 0 | `0x3CEB78` | **No** (0x254B78 past `__TEXT` end) | unresolved |
| 1 | `0x13BF8C` | Yes | `0x5F8C` |
| 2 | `0x141928` | Yes | `0xB928` |
| 3 | `0x1417B0` | Yes | `0xB7B0` |
| 4 | `0x1410E4` | Yes | `0xB0E4` |
| 5 | `0x140C84` | Yes | `0xAC84` |

Therefore only frames 1-5 of the "common guard frame prefix" resolve into the
exact 22F82 libxpc image. **Frame 0 is not libxpc** and cannot be identified
from the available map head; its owning image is unresolved. Frames 6+
(`0x7521684`, `0x7503B00`, `0x250400`, `0x25F97C`, ...) are also outside
libxpc and are not resolved here.

The exact libxpc function bodies at image offsets `0x5F8C` and
`0xAC84`-`0xB928` are **not** in the local capture. The available
`0xf140`-`0xf240` bytes do not overlap any guard frame, so no exact
disassembly of the guard path is possible from current bytes.

## 4. Reason-string path

The crash report is a lightweight corpse and does not preserve the
`os_reason` reason string/payload. Because the exact 22F82 libxpc bytes at the
guard frames are missing, this round cannot:

- name the exact 22F82 function(s) containing `0xAC84`-`0xB928`;
- show the call that invokes the fault path;
- identify the literal or construction of the reason string passed to the
  kernel.

The exact report tuple (`EXC_GUARD`/`GUARD_TYPE_USER`, namespace 7,
reason 9, `LIBXPC`/`XPC_EXIT_REASON_FAULT`) is unchanged from R022. The
function-level explanation for 22F82 remains unproven. R022's 15.6
`__XPC_MISUSE_FAULT` body is not restated as a 22F82 fact.

## 5. Minimal exact inputs for a future backend read

All ranges are main-cache file offsets (equal to the crash report's
cache-relative `imageOffset`), not runtime addresses.

1. **Full 22F82 `.map`** (device path
   `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.map`,
   874,394 B, expected SHA-256 `4c817de2…2088`) or at minimum the image rows
   covering cache `0x3CEB78` and `0x136000`-`0x17A000`. The local
   `cache-map-head.txt` stops after the libxpc `__TEXT` row and cannot name
   frame 0's image.
2. **Exact libxpc bytes** for the guard cluster. Preferred: full `__TEXT`
   `0x136000`-`0x17A000` (278,528 B), verify SHA-256
   `af2bbe7c88c8be38aedef36d80410db39c4376cc1f9ebd0b0cf2b052582b0db5`.
   Minimum focused read: `0x13BE00`-`0x141B00` (23,552 B), which covers all
   five resolved frames with at least 0x100 bytes of context on each side.
3. **Frame 0 bytes** once the map identifies its image/segment: ±0x100 around
   cache `0x3CEB78`, plus the owning image UUID/name.
4. **Reason string**: either the exact 22F82 libxpc literal/`__cstring` range
   reached from the fault call in (2), or a non-lightweight report / launchd /
   device `log`/`oslog` line that preserves the `os_reason` reason string
   around 18:24:44. Do not infer the message from 15.6.
5. Optional but useful: exact libxpc symbol/function starts for image offsets
   `0xAC84`-`0xB928`; the R011 cache-wide `LC_SYMTAB` values are not a
   substitute for the per-image function table.
6. If the full caller chain is required, map rows and bytes for frames 6+
   (`0x250400`, `0x25F97C`, `0x7521684`, `0x7503B00`, ...); they are outside
   libxpc and are not part of the guard handler.

## 6. Limits

- No exact 22F82 guard bytes are local; the only guard-frame attribution is
  the image-relative arithmetic in §3.
- Frame 0's image is unresolved; do not describe it as libxpc or guess a
  library.
- No misuse message, benignity, or fixed behavior is claimed.
- No device/SSH, no build, no source/ABI/TCC change, no Git write.
- Cache-relative offsets and libxpc image offsets are kept distinct
  throughout; runtime addresses are not used.

(handover to codex now)
