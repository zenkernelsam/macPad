# Frontend R018 result — arm64e function-pointer signing contract audit

Date: 2026-10-18. Local-only read-only audit. No device/SSH, no source/test/
build-input edit, no build, no dependency install, no Git write. Added only
this result and
`docs/evidence/r018-arm64e-function-pointer-signing-audit-20261018.md`
(SHA-256 `cd87e6a542ed06a0aca6520fcb054eb4324dc75f8590e74061caa799d9bdc1c4`).

Candidate HEAD: `c97e219`.

## Exact key contract

Xcode 17 `ptrauth.h:15-40` defines:

- `ptrauth_key_asia = 0`;
- `ptrauth_key_process_independent_code = ptrauth_key_asia`;
- `ptrauth_key_function_pointer = ptrauth_key_process_independent_code`;
- C function pointers use this key with **extra data always 0**.

Thus `ptrauth_key_function_pointer` and `ptrauth_key_asia` are the same A-key,
zero discriminator; `blraaz` is the matching indirect call. `asib` is the
return-address key; `asda/asdb` are data keys.

## Established project patterns

- **Runtime-confirmed:** `MSHookFunction` signs its original thunk with
  `ptrauth_sign_unauthenticated(thunk, ptrauth_key_asia, 0)` and comment
  states callers use `blraaz`; `orig()=42` and
  `VERDICT=INSTALLED-ORIGINAL-CALLABLE` are recorded in R003/R010 probe
  artifacts.
- **Runtime-confirmed:** `macos_mshook_provider_probe.c` re-signs a raw
  refusal pointer with `ptrauth_key_asia, 0` before calling it
  (`nopermit-strip res()=42`), and preserves a compiler-signed pointer for
  Form B (`nopermit-raw res()=42`).
- **Source-confirmed:** `AppInputBridge.m:7037-7039,8958-8960` signs resolved
  candidate function pointers with `ptrauth_key_function_pointer, 0` before
  use.
- **Distinct, not this contract:** `Metal_hooks.x:24551-24560` signs an
  address-diversified authenticated-GOT slot target; return-address stripping
  uses `asib`; data pointers use `asda/asdb`; `macws_code_pointer.h` only
  strips for inspection.

## XPC callsite comparison

`MSFindSymbol` returns raw local-symbol addresses
(`cydiasubstrate_macos.m:250-263`; export path `:91-104`). The R010 exact
arm64e artifact (UUID `76A98A5A…`) calls the XPC symbol with `blraaz x19` at
image offset `0x6a48`; the crash report has raw `x19 =
0x180ae1140`, `x0 = dict`, `lr = loadImageCallback+0x12e8`, `far =
0x2000000180ae1140`, instruction abort before the target body.

The XPC callsite uses neither valid form: the pointer is neither
compiler-signed nor re-signed with A-key/0. This is a **source-confirmed
callsite contract mismatch** against the project’s own successful patterns.
It does not establish the 22F82 callee prototype/ABI or semantic/lifecycle
safety.

## Other raw MSFindSymbol uses

`MacWSSteamProcess.m:1449,1599-1605,1823` calls raw `MSFindSymbol` pointers
without visible signing; `Metal_hooks.x:25099` stores one. No exact arm64e
runtime evidence was found; Steam may run in the arm64 slice. These are
unresolved same-class analogs, not successful counterexamples. Most other
`MSFindSymbol` uses pass to `MSHookFunction` or patch bytes; they do not call
the raw symbol directly.

## Missing exact evidence (not a fix)

1. Installed libmachook arm64e bytes at `loadImageCallback+0x12e4` (`0x6a48`):
   confirm `blraaz x19` and whether x19 is raw/signed; exact 22F82 libxpc
   entry bytes/prototype at image offset `0xf140`.
2. A read-only runtime witness of the exact 22F82 target entered with a
   correctly A-key/zero-signed pointer, recording entry registers, return/errno
   and whether the dictionary is accepted at the libxpc add-image callback
   stage.
3. Exact 22F82 function body/prototype/callers/lifecycle; 15.6 is
   version-adjacent only and must not be used as ABI evidence.
4. Any evidence that the Steam/Metal raw calls execute on arm64e; otherwise
   they remain unresolved.

No fix, role gate, setup-call removal, constant return, or 15.6 ABI inference
is proposed. The evidence only records the established signing patterns and
the exact gaps.

## Limits

The A-key/zero contract and provider/probe signing are runtime-proven; the
XPC raw-pointer call is source/crash-confirmed as a mismatch. The exact 22F82
target execution/prototype and the Steam/Metal arm64e behavior remain
unproven.

Full detail:
`docs/evidence/r018-arm64e-function-pointer-signing-audit-20261018.md`.

(handover to codex now)
