# Round 006 — frontend result: Ventura responsibility wrapper/ABI

Date: 2026-10-18. Read-only offline audit; no device, source/test/build change,
build, package, Git write or background task. Only this file and
`docs/evidence/r006-responsibility-wrapper-abi-20261018.md` were added.

Candidate HEAD: `7e5d771`. Backend R003's fresh generation is not restarted.

## Retraction

**FACT (backend R003):** launchd hosting alone does not repair the failure.
The failing PID is the **target token** (49186/49199, iconservicesagent 48579),
not WS sender 48891. A temporary launchd-hosted probe 49211 and the
launchd-hosted `iconservicesagent` both log
`responsibility_get... from PID[...]: (#103) Policy not found`, empty reply.
R005's "launchd hosting is sufficient" candidate is retracted.

## What machine code proves

**22F82 tccd (FACT, control flow only):**

```text
0x10004fb48  bl _responsibility_get_responsible_audit_token_for_audit_token
0x10004fb4c  cbz w0, 0x10004fbc4
0x100051a54  error format (#errno/strerror)
```

Only: symbol called; `w0==0` success; nonzero error path. **No 22F82 argument
registers are captured.** Do not promote the 24G90 signatures below to 22F82.

**24G90 tccd (FACT, version-adjacent, not 22F82):**

| Callsite (VA / file offset) | Justified ABI |
| --- | --- |
| `0x10005c8a8` / `0x5c8a8` | token getter: x0=input `audit_token_t*`, x1=output `audit_token_t*`, x2=x3=0, int return 0/errno |
| `0x100008f60` / `0x8f60` | `int set_pid_responsible(int pid, int responsible_pid)`; log `(%d, %d): %d` |
| `0x10005ca9c` / `0x5ca9c` | `int get_pid_responsible(int pid)`; no x1 output pointer; return used as PID |
| `0x10002955c` / `0x2955c` | `void *get_attribution_for_audittoken(const audit_token_t*, int flags)`; x1=0 |
| `0x1000297b8` / `0x297b8` | `const char *identity_get_binary_path(void *identity)` |

Token getter x2/x3 semantics and attribution flags are unproven; the exact C
parameter count beyond the observed registers is not established. 24G90
`__TEXT` vmaddr `0x100000000`, fileoff 0.

## Boundary

- **FACT:** the responsibility lookup is a synchronous libSystem call inside
  tccd, not XPC. XPC is only the TCC request/reply layer; `#103` is errno.
- **FACT:** `ENOPOLICY` (103) means the target process has no registered
  responsibility policy; XNU stores `p_responsible_pid`, inherited on fork
  (`kern_fork.c:1096`), setter `proc_set_responsible_pid`
  (`kern_proc.c:4698-4706`), with no in-tree setter caller/proc_info flavor in
  the open-source iOS 16.3 XNU.
- **THEORY/UNKNOWN:** the actual syscall/Mach trap/proc_info selector/flavor
  and output-buffer/flags contract are not in the local open-source XNU or
  local binaries. Do not guess.

## Local inputs and the extraction failure

- `misc/libSystem.B.dylib` (16.8KB, macOS 14.0, UUID `7BD6C0A3-…`),
  `analysis/dyldwork/dsc_libSystem.B.dylib` (16KB, macOS 15.6) and
  `analysis/device-leftovers…/libsystem_kernel.dylib` (macOS 15.6) are stubs/
  wrong versions; none has the responsibility wrapper.
- Local iOS 16.3 cache has only main + `.19` mappings:
  `0x180000000+0x58000`, `0x1bc38c000+0x6e48000`. Its image table places
  `libsystem_kernel` at `0x1c4126000` and `libSystem.B` at `0x1d485d000`,
  both outside the local mappings.
- `python3 misc/dsc_extract_image.py … /usr/lib/system/libsystem_kernel.dylib`
  exited **1** with `image @0x1c4126000` then `AttributeError: 'NoneType'
  object has no attribute 'seek'`: missing subcache. No Ventura/native wrapper
  implementation is locally available.

## Missing exact inputs and extraction recipe

- Ventura 22F82 `libSystem.B.dylib` and `libsystem_kernel.dylib` from
  `/var/mnt/rootfs/System/Library/dyld/dyld_shared_cache_arm64e*`; UUID/SHA
  unknown until extracted.
- Ventura tccd `0x10004fb00–0x10004fb60` full register setup and `nm -m`
  provider/ordinal (tccd hash/UUID known: `42c388e8…` / `9845D17F…`).
- Native iPadOS 16.3 20D47 complete cache set covering the two load addresses
  above; UUID/SHA unknown.
- Exact wrapper syscall/trap/flavor.

Recipe: copy the **complete** cache set with hashes; run
`misc/dsc_extract_image.py <base> /usr/lib/system/libsystem_kernel.dylib <out>`
and the same for `/usr/lib/libSystem.B.dylib`. The tool only scans `.01`..`.15`;
for higher-numbered subcaches, create a temp directory with `.01`..`.15`
symlinks to the real files (do not edit the repo tool). Verify `file`,
`otool -l LC_UUID`, SHA, then disassemble with
`xcrun llvm-objdump -d --symbolize-operands` (or IDA). Device-side alternative:
`misc/extract_dyld_cache.py <cache-path> <out-dir>` using
`/usr/lib/dsc_extractor.bundle`.

## One discriminating native-vs-chroot control

1. Extract exact native iPadOS 16.3 20D47 and Ventura 22F82 libSystem/
   libsystem_kernel images; record UUID/SHA and confirm the getter symbols.
2. Build the same read-only helper for native and chroot; it only calls:
   `task_info(TASK_AUDIT_TOKEN)`,
   `responsibility_get_pid_responsible_for_pid(getpid())`,
   `responsibility_get_responsible_audit_token_for_audit_token(&self,&out)`;
   print return/errno, output token pid/auid/euid, and libSystem UUID via
   `dladdr`.
3. Run both under real launchd jobs (not SSH). No setter, no policy creation,
   no TCC change.
4. Outcomes: native valid + chroot ENOPOLICY → wrapper works, policy producer/
   launch contract differs; both invalid → the iOS contract is not provided;
   native symbol absent + chroot present → macOS-only API boundary. No fresh
   WS generation experiment.

## Commands / exits

`shasum -a 256`, `file`, `vtool -show-build`, local mapping reader,
`llvm-objdump -d`, and `git log --oneline -1` exited 0.
`python3 misc/dsc_extract_image.py …` exited **1** with the missing-subcache
trace above. No test/build/package/device command; no commit/reset/stash/push.
Scratch `/tmp/ioscache_extract` was removed; no background task remains.

Full detail: `docs/evidence/r006-responsibility-wrapper-abi-20261018.md`.

(handover to codex now)
