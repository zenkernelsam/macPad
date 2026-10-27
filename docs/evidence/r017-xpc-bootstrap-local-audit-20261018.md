# R017 — exact 22F82 libxpc / `__xpc_bootstrap_services` local audit

Date: 2026-10-18. Read-only offline audit. No device/SSH, no source/test/
build-input edit, no build, no dependency install, no Git write. This note and
the R017 result are the only additions.

## 1. Inputs and local identities

| Item | Exact value / source |
| --- | --- |
| 22F82 crash reports | `analysis/r010-provider-f1/crashes/tccd-2026-10-18-182623.ips`, `authd-…`, `systemstatusd-2026-10-18-182649.ips` |
| 22F82 libxpc | `F3F19227-FF8F-389C-A094-6F4C16E458AF`, arm64e, `/usr/lib/system/libxpc.dylib`, crash base `0x180ad2000`, size `0x44000` (278,528 B) |
| 22F82 libxpc second identity | `coordination/artifacts/r006-lldb-pane.txt:108`: same UUID at runtime base `0x192522000` |
| 22F82 installed libmachook | `76A98A5A-0C9C-32E5-93A3-B006F2BCD6D4`, arm64e, crash base `0x1003dc000` (tccd; authd/systemstatusd have different slides) |
| R010 exact build artifact | `/private/tmp/macpad-r010-clean/.theos/obj/libmachook.dylib`, UUID `76A98A5A…`, fat SHA `c9fa3825…`; exact arm64e thin at `/tmp/r017-r010-libmachook-arm64e`, SHA `4e76d297…` (local extraction only) |
| 15.6 libxpc (adjacent only) | `analysis/device-leftovers-20261006/var-mobile/dscq/usr/lib/system/libxpc.dylib`, UUID `1B8951EB-68DB-37BA-9581-240B796AA872`, minos 15.6, `t __xpc_bootstrap_services @ 0x18017bb84` |
| 15.6 material is not 22F82 | Different UUID/build; only structural comparison below. |

The R010 clean artifact UUID matches the crash's installed libmachook UUID,
and `loadImageCallback` starts at image offset `0x5764` in both the artifact
and the crash frame (`symbolLocation 4840` = `0x12e8`; `0x5764 + 0x12e8 =
0x6a4c`). The installed thin was re-signed after the fat build (R010 installed
SHA `4fa53411…` vs fat `c9fa…`), so the byte-level installed instruction should
be re-read by Backend R011; the code/UUID/offset match is exact.

## 2. Current source trigger and data construction

`loadImageCallback` is registered from `InitStuff`
(`libmachook/mac_hooks.m:12066`) via
`_dyld_register_func_for_add_image`. The XPC branch is entered only when the
added image name starts with `/usr/lib/system/libxpc.dylib`
(`mac_hooks.m:6353`). It then:

1. `xpc_dictionary_create(NULL, NULL, 0)`; `xpc_dictionary_set_uint64(dict,
   "/System/Library/Frameworks/Metal.framework/Metal", 2)`
   (`:6356-6362`).
2. `MSFindSymbol((MSImageRef)header, "__xpc_bootstrap_services")`
   (`:6359-6360`).
3. Logs `#### XPC_BOOTSTRAP: fn=… dict=…` (`:6361-6363`), then calls
   `_xpc_bootstrap_services_fn(dict)` and logs `result=%d` (`:6364-6366`).
   If the symbol is NULL, it logs `SYMBOL NOT FOUND` and does not call
   (`:6367-6368`).

The implementation calls `MSFindSymbol` for the **libxpc image itself**, then
calls the returned function pointer with one argument (x0 = dict). The source
declares `int (*)(xpc_object_t)`; the prototype/return type is not obtained
from the exact 22F82 function.

`MSFindSymbol` returns raw symbol addresses:
`find_sym_in_image` returns `(void *)(slide + nl[i].n_value)` for a local
`N_SECT` symbol (`misc/cydiasubstrate_macos.m:250-263`); the export-trie path
returns `(const uint8_t *)(uintptr_t)addr` (`:91-104`). It does **not** PAC-sign
the result.

## 3. Exact caller instruction and crash chain (22F82)

The R010 clean arm64e artifact (UUID matches crash) disassembles the call as:

```text
6a20: aa0003f3  mov x19, x0        ; x19 = MSFindSymbol result
...
6a40: b40049b3  cbz x19, 0x7374
6a44: aa1403e0  mov x0, x20        ; x0 = XPC dictionary
6a48: d63f0a7f  blraaz x19        ; A-key authenticated call, zero modifier
6a4c: f94002a8  ldr x8, [x21]     ; crash LR points here
```

Crash report (all three processes):

- `pc = 0x180ae1140` = libxpc base `0x180ad2000` + `0xf140`, symbol
  `_xpc_bootstrap_services`, `symbolLocation 0`; frame 0.
- `x19 = 0x180ae1140` (the same raw target); `x0 = x20 = 0x1457042b0` (dict);
  `lr = libmachook base + 0x6a4c`; `x26` is a PAC-signed
  `loadImageCallback` pointer starting at `0x1003dc000 + 0x5764`.
- `far = 0x2000000180ae1140`; `esr = 0x82000004` (Instruction Abort,
  translation fault); subtype “possible pointer authentication failure”.
- Source `mac_hooks.m:6365` maps to this call; the crash stack is
  `InitStuff → _dyld_register_func_for_add_image → dyld initializer →
  loadImageCallback → _xpc_bootstrap_services`.

**Corroborated by exact instruction + registers:** the call is an
authenticated `blraaz x19` (key A, zero modifier), while `MSFindSymbol` returns
a raw, unsigned symbol address. `x19` is the raw target, and the FAR is that
same address with PAC/poison top bits; the CPU faults before the target body
executes. This is a **call-site authenticated-pointer contract mismatch**, not
proof that the 22F82 function's own prototype is wrong.

**Still missing / THEORY:**
- The exact 22F82 libxpc instruction at `0xf140` and function body/prototype
  (argument registers beyond x0, return type, internal `retab`/indirect calls).
- The exact installed thin libmachook bytes at image offset `0x6a48` (the
  artifact is UUID/offset-exact; installed re-signing must be read back).
- Whether a correctly signed A-key/zero-modifier pointer enters the function
  and whether the function is safe to call during the libxpc add-image
  callback. The latter is separate from the observed auth failure.
- The 22F82 `__xpc_bootstrap_services` argument/return semantics; 15.6 shows
  one x0 argument and an object return, but is version-adjacent only.

## 4. 15.6 structural comparison (not 22F82 fact)

Local 15.6 libxpc (`1B8951EB…`, minos 15.6) at `0x18017bb84`:

```text
pacibsp; sub sp,#0x30; ...
mov x19,x0                 ; x0 = caller-provided xpc object
mov x0/x1/x2,#0; bl _xpc_dictionary_create
... xpc_dictionary_set_uint64(dict, key, 5)
... calls another libxpc helper, sets a second key/value
... xpc_dictionary_set_value(dict, key, x19)
mov w0,#800; x1=dict; x2=&out; bl __xpc_domain_routine
release dict; return x19 via retab
```

So the 15.6 function takes one x0 object, creates its own dictionary, and
returns the original object (not an `int`). Its callers
(`__xpc_add_bundle`, `__xpc_uncork_pid_domain_locked`,
`__xpc_dyld_image_callback`) also pass a dictionary/object. This supports the
source's one-argument x0 shape structurally, but the source's `int` return
type is **not** a 15.6 ABI fact and the exact 22F82 function remains unknown.
Do not use the 15.6 body/prototype as 22F82 evidence.

## 5. Successful pre-R012 call evidence

- R007 pre-fix log (`analysis/r007-f11h/char-f11h-converted-0.log:12-13`):
  `#### XPC_BOOTSTRAP: fn=0x0 dict=0x126e10050 (registering Metal compiler
  service)` followed by `SYMBOL NOT FOUND`. The old provider skipped the call.
- No local log contains `XPC_BOOTSTRAP: result=` or a non-null `fn=0x…` call
  result. The first actual call found is the R010 crash. There is **no
  successful pre-R012 call evidence**; do not present the skipped path as
  success.

## 6. Precise inputs for Backend R011

1. **Exact 22F82 libxpc artifact**: preserve UUID `F3F19227-…`, arm64e,
   `/usr/lib/system/libxpc.dylib`, SHA-256, cache UUID/slide, base/size; at
   minimum read `__TEXT` bytes at image offset `0xf140` (±0x100), the symbol
   table/`LC_FUNCTION_STARTS`, and disassemble the function entry and any
   internal auth/indirect branch. If available, preserve the full image for
   prototype/xref analysis (no full cache copy).
2. **Exact installed libmachook arm64e**: verify UUID `76A98A5A-…`, installed
   SHA `4fa53411…` (or current installed value), and read/disassemble image
   offset `0x6a40..0x6a50`; confirm `blraaz x19` and the raw/signed state of
   x19. The R010 clean artifact is UUID/offset-exact but not the re-signed
   installed bytes.
3. **Runtime registers at the call site**: on a disposable read-only lldb
   session, break at `loadImageCallback+0x12e4` (`0x6a48`), capture x0–x8,
   x16/x17, x19/x20, sp, pc, far, esr, lr; single-step into/through the
   authenticated call. Confirm whether x19 is raw or PAC-signed and whether
   the fault is the `blraaz` itself. Do not patch, skip, or gate the call.
4. **Exact 22F82 function contract**: disassemble the entry/prologue and uses
   of x0–x7; determine argument count/types and return; inspect exact libxpc
   callers/xrefs to `__xpc_bootstrap_services`; determine whether calling it
   from the libxpc add-image callback is valid or requires a later
   initialization point. Label any 15.6 comparison as adjacent only.
5. **Optional successful-control evidence**: if a read-only control can call
   the exact target with a correctly A-key/zero-modifier signed pointer, record
   the signed pointer construction, entry registers, return/errno, and whether
   libxpc accepts the dictionary. This must not be a constant return, blind
   role gate, or removal of the setup call.
6. **Preserve** the three crash reports, their image UUIDs/registers, the
   exact installed libmachook/libxpc identities, and any success/failure
   `XPC_BOOTSTRAP` log lines.

## 7. Forbidden interpretations / fixes

Do not call the 15.6 symbol/body the 22F82 ABI; do not infer the callee
prototype from its name or from a different build. Do not propose a constant
return, blind role/identity gate, or removal of the setup call as a fix.
Do not claim the 22F82 function body is ABI-incompatible from the current
crash; the exact instruction/register chain shows a call-site authenticated
pointer mismatch before entry. The target function's ABI/semantics remains a
separate missing input.

## 8. Commands / exits and limits

Read-only `rg`/`sed`/`grep`/`otool`/`nm`/`llvm-objdump` over the listed local
artifacts and the R010 clean artifact exited 0; the exact arm64e slice was
extracted locally to `/tmp/r017-r010-libmachook-arm64e` (not installed, not a
source/build change). No device, source/test/build, dependency or Git
operation was run.

Limits: exact 22F82 libxpc bytes/prototype are absent locally; the exact
installed libmachook thin bytes were re-signed and are not local. The 22F82
PAC/authenticated-call mismatch is strongly corroborated; the exact target
function ABI/lifecycle and a correctly signed control remain Backend R011
inputs.

(handover to codex now)
