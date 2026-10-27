# R018 — established arm64e function-pointer signing patterns vs the XPC call

Date: 2026-10-18. Local-only read-only audit. No device/SSH, no source/test/
build-input edit, no build, no dependency install, no Git write. This note and
the R018 result are the only additions.

## 1. Key contract (exact SDK header)

`/Applications/Xcode.app/.../usr/lib/clang/17/include/ptrauth.h:15-40`:

```c
ptrauth_key_asia = 0;
ptrauth_key_process_independent_code = ptrauth_key_asia;
ptrauth_key_function_pointer = ptrauth_key_process_independent_code;
// "The key used to sign C function pointers. The extra data is always 0."
```

So `ptrauth_key_function_pointer` and `ptrauth_key_asia` are the same A-key;
discriminator 0. `ptrauth_key_asib` (process-dependent code) is the return
address key; `asda/asdb` are data pointer keys. `blraaz` is the matching
A-key, zero-modifier indirect call. This is an exact toolchain contract, not a
22F82-specific inference.

## 2. Established successful signing patterns in this project

### 2.1 Provider MSHookFunction thunk — source + runtime confirmed

`misc/cydiasubstrate_macos.m:433-459` implements the refusal contract and
`:529-534` states:

```c
// Callers invoke *result via blraaz — authenticate with key IA and
// zero discriminator. Sign the thunk the same way or the indirect call
// faults at authentication (verified: probe emits blraaz x8).
*result = ptrauth_sign_unauthenticated(thunk, ptrauth_key_asia, 0);
```

Runtime witness: `coordination/artifacts/provider-probe-r003-final.json`
shows `orig=…`, `orig()=42`, `VERDICT=INSTALLED`; R010’s
`analysis/r010-provider-f1/probe-arch-run.txt` shows
`VERDICT=INSTALLED-ORIGINAL-CALLABLE` on both arch probes. This is the
project’s established, successfully called A-key/zero function pointer.

### 2.2 Probe re-signs a stripped/raw refusal pointer — runtime confirmed

`misc/macos_mshook_provider_probe.c:125-145`:

```c
// A caller holding a stripped pointer must re-authenticate before an
// indirect call — that's the PAC contract ...
toy_fn_t f = (toy_fn_t)ptrauth_sign_unauthenticated(
    resA, ptrauth_key_asia, 0);
calledA = f();
```

The same probe’s Form B (`:146-160`) passes the compiler-signed pointer and
calls `resB()` unchanged, showing the two valid forms: preserve an already
signed pointer, or sign a stripped/raw one with A-key/0. Runtime output in
`provider-probe-r003-final.json`: `nopermit-strip: res=… res()=42`,
`nopermit-raw: res=… res()=42`.

### 2.3 AppInputBridge direct function pointers — source confirmed

`libmachook/AppInputBridge.m:7037-7039` and `:8958-8960` sign resolved
candidate function pointers with
`ptrauth_sign_unauthenticated(candidate, ptrauth_key_function_pointer, 0)`
before storing/calling them (Dock event dispatcher and front-UI lifecycle).
This is the same A-key/zero contract under its function-pointer alias.
Source-confirmed; not separately device-runtime accepted in this audit.

### 2.4 Distinct data/GOT authentication — not this contract

`libmachook/Metal_hooks.x:24551-24560` signs a **GOT slot target** with
`ptrauth_key_function_pointer` and a discriminator computed by
`ptrauth_blend_discriminator(slot, 0)`. That is address-diversified
authenticated-GOT data, not the zero-modifier `blraaz` direct-call contract.
Return-address stripping uses `ptrauth_key_return_address` (asib) and data
pointers use `asda/asdb`; these are unrelated. `include/macws_code_pointer.h`
uses `ptrauth_strip(pointer, ptrauth_key_function_pointer)` only to inspect
code addresses; stripping is not signing.

## 3. The XPC callsite vs those patterns

`libmachook/mac_hooks.m:6353-6365`:

- `MSFindSymbol((MSImageRef)header, "__xpc_bootstrap_services")` returns a raw
  local-symbol address: `misc/cydiasubstrate_macos.m:250-263` returns
  `(void *)(slide + nl[i].n_value)`; the export-trie path returns the raw
  address (`:91-104`). No PAC sign is applied.
- The R010 clean arm64e artifact (UUID `76A98A5A…`, exact R017 identity) calls
  it at image offset `0x6a48` with `blraaz x19`; the crash report has
  `x19 = 0x180ae1140` (raw `_xpc_bootstrap_services`), `x0 = dict`,
  `lr = loadImageCallback+0x12e8`, `far = 0x2000000180ae1140` and an
  instruction abort before the target body.
- Thus the callsite uses neither valid form from §2: the pointer is neither
  compiler-signed nor re-signed with A-key/0. The exact `blraaz` instruction
  and raw x19/FAR are from R017; this audit adds only the key-contract
  comparison.

**Source-confirmed gap:** `MSFindSymbol` returns raw, and the direct call is
`blraaz`; the project’s established A-key/0 signing pattern is not applied
here. This is a callsite contract mismatch. It does **not** establish the
22F82 callee’s prototype/ABI, nor prove that the function is semantically
safe to call during the libxpc add-image callback.

## 4. Other raw MSFindSymbol function-pointer uses (unresolved analogs)

- `libmachook/Compatibility/MacWSSteamProcess.m:1449` casts a raw
  `MSFindSymbol` result to `MacWSAccessibilityLibraryFunction` and calls
  `library(NULL)` without a visible sign; `:1599-1605,1823` calls a raw
  `_CreateSimpleProcess` pointer via a cast. These are Steam-only paths and
  may run in the arm64 slice (no PAC); no exact arm64e runtime evidence was
  found. They are potential same-class gaps, not proven successful
  counterexamples.
- `libmachook/Metal_hooks.x:25099` assigns a raw
  `_getMetalPluginClassForService` pointer; no exact call/arch evidence was
  found. Do not treat it as proof either way.
- Most other `MSFindSymbol` callsites pass the pointer to `MSHookFunction`
  (provider strips and signs the returned thunk) or use it for byte-level
  patching; they do not call the raw symbol directly.

No local evidence was found of a successful **arm64e direct call** of a raw
`MSFindSymbol` function pointer without signing.

## 5. Missing exact evidence (not a fix proposal)

1. Exact installed libmachook arm64e bytes at `loadImageCallback+0x12e4`
   (`0x6a48`), confirming `blraaz x19` and whether x19 is raw or signed;
   exact 22F82 libxpc entry bytes/prototype at image offset `0xf140`.
2. A read-only runtime control that supplies the exact 22F82 target with
   A-key/zero signing (the project’s established form) and records entry
   registers, return/errno, and whether the function accepts the dictionary at
   the libxpc add-image callback stage.
3. Exact 22F82 function body/prototype/callers and lifecycle semantics; 15.6
   is version-adjacent only and must not be used as ABI evidence.
4. Any evidence that `MacWSSteamProcess`/Metal raw calls execute on arm64e;
   otherwise they remain unresolved analogs, not counterexamples.

Do not propose/implement a fix, add a role gate, remove the setup call, return
a constant, or infer the 22F82 callee ABI from 15.6. This note records only
the established signing patterns and the exact missing inputs.

## 6. Commands / exits and limits

Read-only `rg`/`sed`/`grep`/`otool`/`nm`/`llvm-objdump` over the listed source,
SDK headers, R017 evidence, R010 clean artifact and crash artifacts exited 0.
No device, source/test/build, dependency or Git operation was run.

Limits: the signing contract is established and runtime-proven for the
provider/probe targets; the XPC callsite’s raw-pointer call is source- and
crash-confirmed as a mismatch, but the exact 22F82 target function execution,
prototype and lifecycle remain missing. The other raw callsites have no exact
arm64e runtime evidence.

(handover to codex now)
