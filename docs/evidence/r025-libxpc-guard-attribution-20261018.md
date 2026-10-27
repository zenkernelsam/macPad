# R025 — exact 22F82 libxpc guard attribution

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-025-to-frontend.md`. Read-only device/local work:
no package/source/build/Git change, no WindowServer generation, no
policy/TCC/trustcache change, no remote file write. The SSH endpoint and
credential were used only in the temporary process environment; neither is
recorded here.

Device: iPad13,11, iPadOS 16.3 (20D47), kernel
`xnu-8792.82.2~1/RELEASE_ARM64_T8103`. All acquisition commands exited 0.

## 1. Acquired exact artifacts

| Artifact | Size | SHA-256 |
| --- | --- | --- |
| full `dyld_shared_cache_arm64e.map` | 874,394 B | `4c817de2e92565feaa07e544cb7921f08e53badd2fccee877e5d99b155d32088` |
| libxpc `__TEXT` `0x136000`-`0x17A000` | 278,528 B | `af2bbe7c88c8be38aedef36d80410db39c4376cc1f9ebd0b0cf2b052582b0db5` |
| frame-0 cache block `0x3CE000`-`0x3CF000` | 4,096 B | `5a34a937fb253f0b9290ee42dd414fe0c1a8a8aa6d5d7172a14709c082373696` |
| libsystem_kernel header at `0x3BF000` | 4,096 B | `e03416bf2045cb92d9a35dcda4603e7e6159b3d78e19a118b47c37f6869c078a` |
| device iOS launchd | 540,752 B | `793c051be531606f00f091bade3068a0934e716a4865d32e4704d264dd49fa4f` |

The libxpc text hash matches the expected
`af2bbe7c…0db5`. Raw files and the exact combined disassembly are preserved
under `analysis/r025-libxpc-guard/` (gitignored):
`dyld_shared_cache_arm64e.map`, `libxpc-text-0x136000-0x17a000.bin`,
`frame0-block-0x3ce000.bin`, `libsystem_kernel-header-0x3bf000.bin`,
`launchd-ios-20D47.bin`, `launchd-ios.disasm.txt`,
`disasm-exact-guard.txt`
(SHA-256 `e9909ace6afcc7e78a8164fe8e7399fb34908ef01cc763da76889b37ab10cd3f`).

## 2. Exact image ownership and offset convention

The guard report lists `usedImages[0]` as the shared cache
(`base 0x180000000`, UUID `BE59BB71-…`), so its `frames[].imageOffset` values
are **cache-relative** (main-cache file offsets; mapping 0 has fileoff 0).

```text
libxpc:              __TEXT 0x180136000-0x18017A000, fileoff 0x136000,
                     image offset = cache offset - 0x136000
libsystem_kernel:    __TEXT 0x1803BF000-0x1803F8FE8, fileoff 0x3BF000,
                     image offset = cache offset - 0x3BF000
```

Frame 0's owning image was identified from the full map:

```text
/usr/lib/system/libsystem_kernel.dylib
UUID 34A49B54-82B2-37A1-9314-F6A4A2BB3FF8
```

| Frame | Cache offset | Image | Image offset |
| --- | --- | --- | --- |
| 0 | `0x3CEB78` | libsystem_kernel | `0xFB78` |
| 1 | `0x13BF8C` | libxpc | `0x5F8C` |
| 2 | `0x141928` | libxpc | `0xB928` |
| 3 | `0x1417B0` | libxpc | `0xB7B0` |
| 4 | `0x1410E4` | libxpc | `0xB0E4` |
| 5 | `0x140C84` | libxpc | `0xAC84` |

Frame 0 is therefore **not** libxpc; R023's "outside libxpc, owner unresolved"
is now resolved to libsystem_kernel.

## 3. Exact frame-0 bytes: syscall 529 return

Exact libsystem_kernel bytes around frame 0:

```text
img+0xFB70  mov x16, #0x211
img+0xFB74  svc #0x80
img+0xFB78  b.lo ...        <== report frame 0
```

`0x211` is 529 decimal. Local XNU `8792.81.2` `bsd/kern/syscalls.master:837`
defines syscall 529 as `os_fault_with_payload`. Frame 0 is the return address
immediately after the `os_fault_with_payload` syscall in the
libsystem_kernel stub. The device kernel is `8792.82.2`; the exact report
tuple and the exact stub bytes agree on syscall 529.

## 4. Exact libxpc fault path and reason-string source

Frame 1 is the return address of the only direct call to the fault function:

```text
libxpc img+0x5E9C function:
  img+0x5F70  ldr  x0, [sp, #8]              ; reply dictionary
  img+0x5F74  cbz  x0, img+0x5F8C
  img+0x5F78  adr  x1, "xpc-fault"
  img+0x5F80  bl   img+0x5954                ; dictionary value getter
  img+0x5F84  cbz  x0, img+0x5F8C
  img+0x5F88  bl   img+0x1D5A8               ; fault function, x0 = value
  img+0x5F8C                                  ; report frame 1
```

The exact fault function `img+0x1D5A8` (libxpc UUID
`F3F19227-FF8F-389C-A094-6F4C16E458AF`) does:

```text
img+0x1D5C8  mov  x19, x0                    ; reason string
img+0x1D640  adr  x6, "%s (backtrace follows)"
img+0x1D658  bl   img+0x16E928               ; os_log_simple
...
img+0x1D73C  mov  w0, #7                     ; OS_REASON_LIBXPC
img+0x1D740  mov  w1, #9                     ; reason code
img+0x1D744  mov  x2, #0                     ; payload NULL
img+0x1D748  mov  w3, #0                     ; payload size 0
img+0x1D74C  mov  x4, x19                    ; reason string
img+0x1D750  mov  x5, #0
img+0x1D754  bl   img+0x16F358               ; imported os_fault_with_payload
```

A full exact-text `bl` scan found only one caller of `img+0x1D5A8`:
`img+0x5F88`. The imported stub at `img+0x16F358` is a lazy/auth stub; the
next observed frame is the libsystem_kernel syscall-529 return at frame 0.
This reconstructs the exact chain:

```text
xpc-fault dictionary value
  -> libxpc fault function img+0x1D5A8
  -> os_fault_with_payload(7, 9, NULL, 0, value, 0)
  -> libsystem_kernel syscall 529
  -> simulated EXC_GUARD / LIBXPC / XPC_EXIT_REASON_FAULT
```

The fault function itself is not present in the report's six-frame prefix;
its frame was unwound/skipped, but the caller and callee bytes are exact.

## 5. Frames 2-5: exact libxpc context, not the fault function

Frames 2-5 resolve to exact libxpc functions:

```text
frame 2  function img+0xB81C   (frame img+0xB928)
frame 3  function img+0xB624   (frame img+0xB7B0)
frame 4  function img+0xACF8   (frame img+0xB0E4)
frame 5  function img+0xAB5C   (frame img+0xAC84)
```

Their string references include launchd/bootstrap dictionary keys
(`type`, `handle`, `lookup-handle`, `flags`, `name`, `instance`,
`cputypes`, `port`, `non-launching`) and `pre-exec`/`xpc-fault`/`error`.
None of these functions calls the fault function `img+0x1D5A8` in the exact
text; they are backtrace context, not the fault handler. No exact role beyond
that is claimed.

## 6. Reason-string value: exact source key, value still missing

The reason string is exactly the value of the `xpc-fault` key in the reply
dictionary passed to the frame-1 function. The actual value is not preserved
in the lightweight crash report.

**Exact device log correlation** in
`/var/log/com.apple.xpc.launchd/launchd.log.1`:

```text
64609: 2026-10-18 18:24:43.889040 <Notice>: launchdchrootex.67961 sent an invalid request
64610: 2026-10-18 18:24:44.009730 (system) <Warning>: failed lookup: name = com.apple.analyticsd, flags = 0x8, requestor = codesign[67961], error = 3: No such process
```

PID 67961 is the `launchdchrootexec` process that exec'd `/usr/bin/codesign`;
the guard report is PID 67961 at 18:24:44.0107. The failed analyticsd lookup
is 1 ms before the guard. This is a strong trigger correlation, not proof of
the dictionary value.

The device iOS launchd contains exactly two literal `xpc-fault` set sites
(`0x10002F4E4`, `0x10002FB60`), both for cross-domain session violations, not
the analyticsd failed-lookup path. Therefore the launchd log message is
**not** asserted to be the `xpc-fault` value. A `log show` query for the same
second produced no output; the value may require the non-lightweight
`os_reason` or a retained unified-log line.

Do not infer the misuse message from 15.6, and do not call the fault benign.

## 7. Remaining minimal input

Only the actual `xpc-fault` string / `os_reason` value remains missing.
Everything else requested by R023/R024 was acquired:

1. A non-lightweight crash report or ReportCrash/launchd line that preserves
   the `os_reason` reason string for incident
   `5AA40622-72B7-4995-AB7B-5F115A105EED`.
2. A retained unified-log line for libxpc's `"%s (backtrace follows)"` log at
   img+0x1D640 around 18:24:44, or another exact capture of the reply
   dictionary's `xpc-fault` value.
3. If no log is retained, an exact producer analysis of the reply dictionary
   (which launchd/libxpc path sets `xpc-fault` for a failed
   `com.apple.analyticsd` lookup) before asserting the string.

No additional cache/map/libxpc bytes are needed.

## 8. Limits

- Read-only: no generation, no package/source/build/Git change, no TCC or
  trustcache change, no remote file write. All remote reads streamed to
  stdout or base64 and were verified by local SHA-256.
- The local XNU source is `8792.81.2`; the device kernel is `8792.82.2`. The
  syscall number/encoding is corroborated by the exact device report and
  stub bytes, not only by the adjacent source.
- The iOS launchd binary is exact for this device, but its `xpc-fault` value
  construction for the analyticsd lookup is not established.
- Cache-relative offsets, image offsets and runtime addresses are kept
  distinct; no runtime address is used for the attribution.

(handover to codex now)
