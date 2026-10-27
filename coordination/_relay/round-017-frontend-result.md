# Frontend R017 result — exact XPC bootstrap caller audit

Date: 2026-10-18. Local-only read-only audit. No device/SSH, no source/test/
build-input edit, no build, no dependency install, no Git write. Added only
this result and
`docs/evidence/r017-xpc-bootstrap-local-audit-20261018.md`
(SHA-256 `dc39101caa4c3be86d79a64219d3fbe87237cd6e23a8ac5dbf76c8b3caff2aa6`).

Candidate HEAD: `c97e219`.

## Exact 22F82 identities (found locally)

- Crash reports: `analysis/r010-provider-f1/crashes/tccd-2026-10-18-182623.ips`,
  `authd-…`, `systemstatusd-2026-10-18-182649.ips`.
- libxpc: UUID `F3F19227-FF8F-389C-A094-6F4C16E458AF`, arm64e,
  `/usr/lib/system/libxpc.dylib`, crash base `0x180ad2000`, size `0x44000`.
  Same UUID appears in `coordination/artifacts/r006-lldb-pane.txt:108` at
  runtime base `0x192522000`.
- Installed libmachook: UUID `76A98A5A-0C9C-32E5-93A3-B006F2BCD6D4`.
- R010 exact build artifact: `/private/tmp/macpad-r010-clean/.theos/obj/
  libmachook.dylib`, UUID `76A98A5A…`, fat SHA `c9fa3825…`; local arm64e thin
  `4e76d297…`. Installed thin was re-signed (R010 SHA `4fa53411…`); code/UUID/
  offset match must be re-read from the installed bytes.
- 15.6 libxpc is a different artifact:
  `analysis/device-leftovers-20261006/var-mobile/dscq/usr/lib/system/
  libxpc.dylib`, UUID `1B8951EB-68DB-37BA-9581-240B796AA872`, minos 15.6,
  `t __xpc_bootstrap_services @ 0x18017bb84`. It is structural comparison
  only, **not** 22F82 ABI evidence.

## Source trigger/data construction

`loadImageCallback` is registered from `InitStuff`
(`libmachook/mac_hooks.m:12066`). The XPC branch triggers when the added image
path starts with `/usr/lib/system/libxpc.dylib` (`:6353`). It creates an
`xpc_dictionary_t` with key
`/System/Library/Frameworks/Metal.framework/Metal` and value 2
(`:6356-6362`), resolves `__xpc_bootstrap_services` via `MSFindSymbol`
(`:6359-6360`), logs `fn/dict`, and calls it with the dictionary as x0
(`:6364-6366`). If the symbol is NULL, it logs `SYMBOL NOT FOUND` and does not
call (`:6367-6368`). `MSFindSymbol` returns raw `slide + n_value` for local
`N_SECT` symbols and does not PAC-sign the result
(`misc/cydiasubstrate_macos.m:250-263`).

## Exact caller/crash chain (FACT from local artifact + crash registers)

The R010 clean arm64e artifact (UUID matches the crash) has:

```text
6a20: aa0003f3  mov x19, x0     ; MSFindSymbol result
6a40: b40049b3  cbz x19, …
6a44: aa1403e0  mov x0, x20     ; XPC dictionary
6a48: d63f0a7f  blraaz x19     ; A-key authenticated call, zero modifier
6a4c: …                         ; crash LR
```

The crash reports show:

- `pc = 0x180ae1140` = libxpc base + `0xf140` = `_xpc_bootstrap_services`
  (symbolLocation 0);
- `x19 = 0x180ae1140` (raw target), `x0 = x20 = 0x1457042b0` (dict),
  `lr = libmachook base + 0x6a4c`, `x26` is a PAC-signed `loadImageCallback`
  pointer starting at `0x1003dc000 + 0x5764`;
- `far = 0x2000000180ae1140`, `esr = 0x82000004` = Instruction Abort,
  translation fault;
- stack: `InitStuff → _dyld_register_func_for_add_image → dyld initializer →
  loadImageCallback → _xpc_bootstrap_services`.

The `blraaz x19` instruction requires an A-key/zero-modifier authenticated
function pointer; `MSFindSymbol` returns a raw address. The raw x19 target and
the PAC/poisoned FAR, with the fault before any target-body instruction, make
a **call-site authenticated-pointer contract mismatch strongly corroborated**
by exact instructions and runtime registers. This does **not** prove the 22F82
function's own prototype/ABI is wrong.

## Still missing (Backend R011 inputs)

1. Exact 22F82 libxpc bytes/prototype: read `__TEXT` at image offset `0xf140`
   (±0x100), symbol table/`LC_FUNCTION_STARTS`, disassemble entry and internal
   auth/indirect branch; determine x0–x7 argument use, return type and
   callers/xrefs. Full image optional; no full-cache copy.
2. Exact installed libmachook arm64e bytes at image offset `0x6a40..0x6a50`
   (confirm `blraaz x19` and x19 raw/signed; verify UUID/SHA).
3. Runtime registers at `loadImageCallback+0x12e4` (`0x6a48`): x0–x8, x16/x17,
   x19/x20, sp, pc, far, esr, lr; single-step the authenticated call. No
   patch/skip/gate.
4. Exact 22F82 function contract and initialization context: whether the
   one-argument dictionary call is semantically valid at the libxpc add-image
   callback, and what the function returns. 15.6 shows one x0 object argument
   and an object return (`pacibsp; mov x19,x0; …; retab`) but is adjacent only.
5. A read-only successful control with a correctly A-key/zero-modifier signed
   target, if authorized, to prove the pointer fix enters the function and to
   capture its return/errno. Do not use a constant return, blind role/identity
   gate, or removal of the setup call as a fix.

## Pre-R012 call evidence

`analysis/r007-f11h/char-f11h-converted-0.log:12-13` records
`#### XPC_BOOTSTRAP: fn=0x0 dict=0x126e10050 …` followed by
`SYMBOL NOT FOUND`: the old provider skipped the call. No local
`XPC_BOOTSTRAP: result=` or non-null successful call log was found. There is
**no successful pre-R012 call evidence**.

## Limits

The exact 22F82 libxpc binary/body/prototype is not local; only crash metadata
and registers identify it. The installed libmachook thin was re-signed, so its
byte-level instruction must be re-read. The call-site PAC/auth mismatch is
strongly corroborated; the target ABI/semantics and a signed-pointer control
remain Backend R011 inputs. 15.6 symbols/comments are not 22F82 facts.

Full detail:
`docs/evidence/r017-xpc-bootstrap-local-audit-20261018.md`.

(handover to codex now)
