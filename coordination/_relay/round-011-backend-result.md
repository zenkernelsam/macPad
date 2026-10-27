# Backend R011 result — exact 22F82 libxpc entry and XPC-bootstrap crash attribution

Date: 2026-10-18 (Asia/Hong_Kong). Scope:
`coordination/_relay/round-011-backend-result-task.md`. Read-only: no
source/package edit, build/install, launchd/service change, WindowServer
start/restart, debugger attach, marker/TCC/cache mutation, or compiler
operation. Device cache/libmachook reads were bounded read-only ranges.
The R010 package remains installed and its baseline backups are preserved.

## 1. R010 frame correction

R010 listed the startup crash image as `libmachook.dylib` UUID
`76a98a5a-0c9c-32e5-93a3-b006f2bcd6d4`. The crash frame 0 is actually
**libxpc.dylib** UUID `F3F19227-FF8F-389C-A094-6F4C16E458AF` at image
offset `0xf140`; libmachook (UUID `76a98a5a-…`) is frame 1,
`loadImageCallback +0x6a4c`, the caller. The package rejection stands; only
the image/cause attribution is corrected.

## 2. Exact 22F82 cache/image identity and slide

| Item | Value |
|---|---|
| cache | `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e` |
| cache UUID | `BE59BB71-7B69-3EFC-86A8-5625A077AE2E` (matches R007 shared cache) |
| files | main 1,591,689,216 B; `.01` 1,832,419,328 B; map 874,394 B |
| cache header SHA-256 | `b78ba2651311e7d7d4ab28a3cbd345acd798ddb99f9881c9c1e8e943bbc31c63` |
| map SHA-256 | `4c817de2e92565feaa07e544cb7921f08e53badd2fccee877e5d99b155d32088` |
| libxpc image | `/usr/lib/system/libxpc.dylib`, UUID `F3F19227-FF8F-389C-A094-6F4C16E458AF` |
| cache file offset | `0x136000` |
| `__TEXT` | vmaddr `0x180136000`, size `0x44000`, fileoff `0x136000`, SHA-256 `af2bbe7c88c8be38aedef36d80410db39c4376cc1f9ebd0b0cf2b052582b0db5` |
| image header SHA-256 | `8d5b12c57bf877375300a4d7eba0dc4263acfc992f1a129b20c5d63eb87e8ba7` |
| `LC_BUILD_VERSION` | platform 6, minos `0x100800`, sdk `0x100800` (exact stored values) |
| `LC_ID_DYLIB` | `/usr/lib/system/libxpc.dylib` |
| runtime slide | **`0x99c000`** (`0x180ad2000` crash base − `0x180136000` unslid) |
| function | unslid `0x180137140` → runtime **`0x180ae1140`** (`+0xf140`) |
| entry bytes SHA-256 | `b6c7c5f257a34e5bae662e9fe60485de9b2cafb08e39848b786936c5395c6796` |
| entry first bytes | `7f2303d5 ffc300d1 f44f01a9 fd7b02a9 fd830091 f30300aa …` (`pacibsp; sub sp,sp,#0x30; …`) |

The crash PC `0x180ae1140` and FAR `0x2000000180ae1140` are exactly
`unslid 0x180137140 + slide 0x99c000` plus the high poison bit. The
libxpc entry begins with `pacibsp`; the callee never executed. Only the
exact image/header/`__TEXT`/function ranges were read; a full
image+shared-LINKEDIT hash was not extracted.

## 3. Exact caller instruction and registers

Installed libmachook: SHA-256
`4fa53411d6477720cdfb4a540f40fa1c63ce020ff093576fa6732dd3da399322`,
UUID `76A98A5A-0C9C-32E5-93A3-B006F2BCD6D4`, `loadImageCallback` at
`0x5764`. The call site bytes read from the installed file:

```text
0x6a40  cbz  x19, #0x7374
0x6a44  mov  x0, x20          ; dict
0x6a48  blraaz x19            ; authenticated indirect call
0x6a4c  ldr  x8, [x21]        ; return address
```

Source `libmachook/mac_hooks.m:6359-6366`: the function pointer is
`int(*_xpc_bootstrap_services_fn)(xpc_object_t)` from
`MSFindSymbol((MSImageRef)header, "__xpc_bootstrap_services")`; the call is
`_xpc_bootstrap_services_fn(dict)`. The compiler uses `blraaz x19` with
x0=dict.

Crash registers (tccd 70481 / systemstatusd 70814 / authd 70469):

| Register | Value |
|---|---|
| x0, x20 | dict heap pointer (per process; x20 = x0) |
| x19 | `0x180ae1140` in all three — raw libxpc entry, no PAC signature bits |
| LR | libmachook base + `0x6a4c` in all three (return after `blraaz`) |
| PC | `0x180ae1140` |
| FAR | `0x2000000180ae1140` |
| ESR | `0x82000004` instruction abort, translation fault |
| x1/x2/x3 | not callee arguments (the callee prologue overwrites x0-x2); values listed in the artifact |
| x16/x17 | unrelated indirect-branch scratch; not the fault target |

## 4. Cause — runtime+static-confirmed call-boundary PAC authentication failure

Provider `MSFindSymbol` returns the raw linked address
(`misc/cydiasubstrate_macos.m:261`: `return (void *)(slide + n_value);`)
without a PAC signature. The provider does sign trampoline results
elsewhere (`:534`, `ptrauth_sign_unauthenticated(thunk, ptrauth_key_asia,
0)`), but `MSFindSymbol` does not. The caller's `blraaz x19` authenticates
the raw pointer with key A/zero; x19 is `0x180ae1140` with no signature; the
authentication produces the poisoned target `0x2000000180ae1140`; the
instruction fetch at the valid libxpc entry aborts before `pacibsp` runs.

This is not an inference from the address alone: exact caller instruction,
exact raw x19, exact provider return path, exact callee entry and the
PC/FAR/ESR/LR correlation all agree. It is the R007-flagged unbounded
`__xpc_bootstrap_services` setup call, but the evidenced fault is the raw
function pointer, not the callee ABI. The 22F82 **libxpc ABI is not
implicated**; the fault is at the function-pointer call boundary.

## 5. Callee ABI and dictionary shape

The libxpc entry is a normal arm64e function:

```text
pacibsp; sub sp,sp,#0x30; stp x20,x19; stp x29,x30; add x29,sp,#0x20;
mov x19,x0; mov x0,#0; mov x1,#0; mov x2,#0; bl 0x180ad3b0c; …
retab
```

It consumes one incoming argument in x0 (moved to x19), returns in x0 with
`retab`, and does not read x1-x3 before defining them. This is consistent
with `int(*)(xpc_object_t)` and the single x0=dict call. The crash occurs on
the instruction fetch at the entry, before the body executes; the callee
prototype/dictionary cannot be the instruction-abort cause.

The source dictionary is one key/value:
`"/System/Library/Frameworks/Metal.framework/Metal"` → uint64 `2`
(`xpc_dictionary_create(NULL,NULL,0)`). The crash report has no heap/dict
contents and no successful callee execution was produced, so the
dictionary's semantic correctness remains unvalidated; it is not the fault
cause.

## 6. Missing / additional bounded observations

- Independent cache-aware local-symbol-table entry for
  `__xpc_bootstrap_services` was not resolved with naive LC_SYMTAB
  cache-file offsets (shared LINKEDIT/subcache mapping); crash symbolization
  plus exact entry/PC/pointer are exact. A cache-aware local-symbol reader
  would be the additional input if symbol-table provenance is required.
- No runtime dictionary memory or successful callee execution; a correctly
  signed call would be needed to validate the dictionary semantics.
- No new crash/process generation was manufactured.

## 7. Device state and cleanup

At 18:49 HKT: native SpringBoard 872 / tccd 883 / backboardd 905 alive; all
markers absent; R010 package `com.kdt.macosbooter 0.3.4` installed; provider
target inode 250668640 present. No WindowServer, project tccd-system,
displayd, inputd, interopd, iconservicesagent, launchdchrootexec or probe
process was running. A pre-existing `MacWSHost` app PID 71324 (mobile,
elapsed 17:47, present after the R010 cleanup) was left untouched; it was
not started or stopped by R011. No device files, markers, services, source
or packages were changed; only bounded cache/libmachook reads were made.

## 8. Narrow next step

The evidence supports a provider/caller defect, not a libxpc ABI or
dictionary defect: sign/authenticate the `MSFindSymbol` result with
`ptrauth_key_asia` (key A, zero) at the function-pointer call boundary, or
expose a function-pointer-specific resolved-symbol API, then rebuild,
redeploy and repeat the owned-generation check. A role/identity gate,
removing the setup call or returning a constant would only hide this path
and does not correct the raw-pointer contract. No source change was made in
R011.

Evidence: `docs/evidence/r011-xpc-bootstrap-crash-attribution-20261018.md`,
`coordination/artifacts/R011-xpc-bootstrap-crash-attribution-20261018.json`,
`analysis/r011-xpc-crash/` and the R010 crash reports under
`analysis/r010-provider-f1/crashes/`.

(handover to codex now)
