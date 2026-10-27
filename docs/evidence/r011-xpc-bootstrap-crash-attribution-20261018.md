# R011 — exact 22F82 libxpc entry and XPC-bootstrap crash attribution

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-011-backend-result-task.md`. Read-only: no
source/package edit, build/install, launchd/service change, WindowServer
start/restart, debugger attach, marker/TCC/cache mutation, or compiler
operation. Device reads were bounded cache/libmachook range reads. The
R010 package remains installed and its baseline backups are preserved.

## 1. Correction to the R010 frame attribution

R010 listed the startup crash image as `libmachook.dylib` UUID
`76a98a5a-0c9c-32e5-93a3-b006f2bcd6d4`. The crash report frame 0 is
**libxpc.dylib** UUID `F3F19227-FF8F-389C-A094-6F4C16E458AF`, image offset
`0xf140`; `libmachook.dylib` UUID `76a98a5a-…` is **frame 1**, the
`loadImageCallback` caller at image offset `0x6a4c`. The package rejection
stands; the image/cause attribution is corrected here.

## 2. Exact cache and libxpc image identity

The 22F82 rootfs cache:

| Item | Value |
|---|---|
| cache | `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e` |
| cache UUID | `BE59BB71-7B69-3EFC-86A8-5625A077AE2E` (matches R007 shared cache) |
| files | main 1,591,689,216 bytes; `.01` 1,832,419,328 bytes; `.map` 874,394 bytes |
| cache header SHA-256 | `b78ba2651311e7d7d4ab28a3cbd345acd798ddb99f9881c9c1e8e943bbc31c63` |
| map SHA-256 | `4c817de2e92565feaa07e544cb7921f08e53badd2fccee877e5d99b155d32088` |
| mappings | offset 512, count 6; `__TEXT` mapping 0 at `0x180000000`, fileoff 0 |

The exact image is `/usr/lib/system/libxpc.dylib`, UUID
`F3F19227-FF8F-389C-A094-6F4C16E458AF` (same UUID as crash frame 0):

| Item | Value |
|---|---|
| cache file offset | `0x136000` |
| `__TEXT` | vmaddr `0x180136000`, size `0x44000`, fileoff `0x136000` |
| `__TEXT` SHA-256 | `af2bbe7c88c8be38aedef36d80410db39c4376cc1f9ebd0b0cf2b052582b0db5` |
| image header SHA-256 | `8d5b12c57bf877375300a4d7eba0dc4263acfc992f1a129b20c5d63eb87e8ba7` |
| `LC_BUILD_VERSION` | platform 6, minos `0x100800`, sdk `0x100800` (exact stored values) |
| `LC_ID_DYLIB` | `/usr/lib/system/libxpc.dylib` |
| `LC_SYMTAB` | symoff 1,199,958,032; 2,721 symbols; stroff 1,367,660,880; strsize 461,166,809 |
| runtime slide | **`0x99c000`** (`0x180ad2000` crash base − `0x180136000` unslid) |
| function | unslid `0x180137140` → runtime **`0x180ae1140`** (`+0xf140`) |
| function file offset | `0x145140` |
| entry bytes (256 B) SHA-256 | `b6c7c5f257a34e5bae662e9fe60485de9b2cafb08e39848b786936c5395c6796` |
| entry first bytes | `7f2303d5 ffc300d1 f44f01a9 fd7b02a9 fd830091 f30300aa …` |

The crash PC `0x180ae1140` and FAR `0x2000000180ae1140` are exactly
`unslid 0x180137140 + slide 0x99c000` (plus the high poison bit). The
function entry begins with `pacibsp`; the callee never executed. The
image/`__TEXT`/function ranges were extracted read-only; a full
image-plus-shared-LINKEDIT hash was not produced because only the exact
ranges needed were read.

## 3. Exact caller instruction and registers

Installed `/var/jb/usr/macOS/lib/libmachook.dylib`: SHA-256
`4fa53411d6477720cdfb4a540f40fa1c63ce020ff093576fa6732dd3da399322`,
UUID `76A98A5A-0C9C-32E5-93A3-B006F2BCD6D4`, `__TEXT` vmaddr/fileoff 0,
`loadImageCallback` at `0x5764`. The crash caller/return is at image offset
`0x6a4c`; the bounded call-site bytes were read directly from the installed
file:

```text
0x6a20  mov  x19, x0
...
0x6a40  cbz  x19, #0x7374
0x6a44  mov  x0, x20          ; dict
0x6a48  blraaz x19            ; authenticated indirect call
0x6a4c  ldr  x8, [x21]        ; return address after call
```

The source is `libmachook/mac_hooks.m:6359-6366`:

```c
int(*_xpc_bootstrap_services_fn)(xpc_object_t) =
    MSFindSymbol((MSImageRef)header, "__xpc_bootstrap_services");
...
if (_xpc_bootstrap_services_fn) {
    int result = _xpc_bootstrap_services_fn(dict);
}
```

The caller sets `x0 = x20` (the dictionary) and branches through `x19` with
`blraaz` (key A, zero modifier). The crash reports show, for all three
processes:

| Register | tccd (70481) | systemstatusd (70814) | authd (70469) |
|---|---|---|---|
| x0 | `0x1457042b0` | `0x151e09500` | `0x1498041d0` |
| x20 | same as x0 (dict) | same as x0 | same as x0 |
| x19 | `0x180ae1140` | `0x180ae1140` | `0x180ae1140` |
| LR | `0x1003e2a4c` | `0x1011dea4c` | `0x104faea4c` |
| PC | `0x180ae1140` | `0x180ae1140` | `0x180ae1140` |
| FAR | `0x2000000180ae1140` | `0x2000000180ae1140` | `0x2000000180ae1140` |
| ESR | `0x82000004` instruction abort, translation fault | same | same |
| x1/x2/x3 | `0x10049d82e / 0x1b788029 / 0xfffffffffffffff2` | `0x10129982e / 0x1b788029 / 0xfffffffffffffff2` | `0x10506982e / 0x1b788029 / 0xfffffffffffffff2` |
| x16/x17 | unrelated scratch; not the fault target | same | same |

Every LR is exactly `libmachook base + 0x6a4c`; `x19` is the raw libxpc
entry `0x180ae1140` with **no PAC signature bits**. `x1-x3` are not callee
arguments (the callee's prologue overwrites x0-x2 for its first internal
call). `x16/x17` are unrelated; the faulting branch uses `x19`.

## 4. Why the call faults — exact chain

The provider's `MSFindSymbol` returns the raw linked-image address:

```c
// misc/cydiasubstrate_macos.m:250-261
return (void *)(slide + (uintptr_t)nl[i].n_value);
```

The provider does sign trampoline results elsewhere:

```c
// misc/cydiasubstrate_macos.m:534
*result = ptrauth_sign_unauthenticated(thunk, ptrauth_key_asia, 0);
```

but `MSFindSymbol` has no equivalent signing. The compiler emits
`blraaz x19` for the function-pointer call; `blraaz` authenticates the
pointer with key A and zero modifier. `x19` is the raw
`0x180ae1140`; the authentication therefore fails and yields the poisoned
target `0x2000000180ae1140`; the instruction fetch at the valid function
entry aborts before `pacibsp` executes.

This is **runtime+static confirmed** at the call boundary, not an inference
from the address alone: exact caller instruction, exact raw `x19`, exact
provider return path, exact callee entry, and the FAR/ESR/PC correlation.
It is the R007-flagged unbounded `__xpc_bootstrap_services` setup call, but
the evidenced fault is the raw function pointer, not the callee ABI. The
R010 “PAC/ABI theory” is refined: the PAC authentication contract is
confirmed; the 22F82 libxpc ABI is **not** implicated.

## 5. Callee ABI and dictionary shape

The entry at `0x180ae1140` is a normal arm64e function:

```text
pacibsp
sub sp, sp, #0x30
stp x20, x19, [sp, #0x10]
stp x29, x30, [sp, #0x20]
add x29, sp, #0x20
mov x19, x0
mov x0, #0
mov x1, #0
mov x2, #0
bl 0x180ad3b0c
...
retab
```

It consumes one incoming argument in x0 (moved to x19) and returns in x0
with `retab`; it does not read x1-x3 before defining them for internal
calls. That is consistent with the source's
`int(*)(xpc_object_t)` and the single x0=dict call. The crash occurs on the
instruction fetch at the entry, before the body executes; therefore the
callee prototype and dictionary semantics cannot be the instruction-abort
cause.

The source dictionary shape is one key/value:
`"/System/Library/Frameworks/Metal.framework/Metal"` → uint64 `2`, created
with `xpc_dictionary_create(NULL,NULL,0)` and passed in x0. The crash report
does not contain heap/dictionary contents, and no successful callee
execution was produced, so the dictionary's semantic correctness remains
unvalidated; it is not the fault cause.

## 6. Missing / additional observations

- An independent cache-aware local-symbol-table entry for
  `__xpc_bootstrap_services` was not resolved by reading `LC_SYMTAB` at the
  naive cache-file offsets (shared-LINKEDIT/subcache mapping); the crash
  symbolization, exact entry/PC, caller instruction and raw pointer are
  exact. A cache-aware local-symbol extractor would be the bounded
  additional input if symbol-table provenance is required.
- No runtime dictionary memory or successful callee execution; a correctly
  signed call would be needed to validate dictionary semantics.
- No new crash/process generation was manufactured for this packet.

## 7. Device state and cleanup

At 18:49 HKT, a new read-only inventory found native SpringBoard 872 /
tccd 883 / backboardd 905 alive, all markers absent, the R010 package
`com.kdt.macosbooter 0.3.4` installed, and the published provider inode
250668640 present. No WindowServer, project tccd-system, displayd, inputd,
interopd, iconservicesagent, launchdchrootexec or probe process was
running. A pre-existing `MacWSHost` app PID 71324 (mobile, elapsed 17:47)
was present after the R010 cleanup; it was not started or stopped by R011.
No device files, markers, services, source or packages were changed by this
packet; the cache/libmachook reads were bounded read-only ranges.

## 8. Conclusion and narrow next step

The R010 crash is a provider/caller function-pointer contract defect:
`MSFindSymbol` returns a raw 22F82 libxpc address and the caller executes
`blraaz x19`, which authenticates it and poisons it. The libxpc entry is a
valid one-argument arm64e function; no ABI/dictionary failure is evidenced.
A role/identity gate or removal/constant-return of the call would only hide
this path; it does not correct the raw-pointer contract. The narrow fix is
to sign/authenticate the function pointer with `ptrauth_key_asia` (key A,
zero) at the call boundary (or expose a function-pointer-specific resolved
symbol API), then rebuild/redeploy and re-run the owned-generation check.
No source change was made in R011.

Evidence: `coordination/artifacts/R011-xpc-bootstrap-crash-attribution-20261018.json`,
`analysis/r011-xpc-crash/` (cache/image/function/callsite extracts and
disassembly; current inventory), and the R010 crash reports under
`analysis/r010-provider-f1/crashes/`.

(handover to codex now)
