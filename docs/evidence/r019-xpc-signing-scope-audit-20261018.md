# R019 — static scope audit for the narrow XPC pointer-signing change

Date: 2026-10-18. Read-only scope audit. No source/test/build-input edit, no
build, no deploy, no SSH, no Git write. This note and the R019 result are the
only additions.

## 1. Candidate visibility (initial and final)

At the start of the audit no candidate was visible locally:

- `git status --porcelain` showed only pre-existing coordination state changes
  and untracked relay/artifact files;
- `git diff --name-only` contained no `libmachook/`, `misc/`, `include/`,
  `Makefile` or build-input path; `git diff -- libmachook/mac_hooks.m` was
  empty;
- `git log --all -S'ptrauth_sign_unauthenticated' -- libmachook/mac_hooks.m`
  returned no commits;
- Backend R011’s result explicitly states “No source change was made in R011.”

During the audit a candidate appeared in the working tree:
`libmachook/mac_hooks.m` mtime `2026-10-18T18:57:05+0800`,
`git diff --numstat` 14 insertions/1 deletion, diff SHA-256
`5567b452df17c5720ef08c54fc95768cd71e3377a099caf8e4e7ad3a6f03f087`.
The candidate is reviewed in §1A; the initial no-candidate record is retained
only as audit history.

## 1A. Candidate diff and static verdict

The candidate is a single hunk in `loadImageCallback`:

```c
void *_xpc_bootstrap_services_raw =
    MSFindSymbol((MSImageRef)header, "__xpc_bootstrap_services");
int(*_xpc_bootstrap_services_fn)(xpc_object_t) = NULL;
if (_xpc_bootstrap_services_raw) {
#if __arm64e__
    _xpc_bootstrap_services_fn = (int(*)(xpc_object_t))
        ptrauth_sign_unauthenticated(_xpc_bootstrap_services_raw,
                                     ptrauth_key_asia, 0);
#else
    _xpc_bootstrap_services_fn = (int(*)(xpc_object_t))
        _xpc_bootstrap_services_raw;
#endif
}
```

The existing log, null gate, dictionary x0 and `_xpc_bootstrap_services_fn(dict)`
call are unchanged. `mac_hooks.m` already imports `<ptrauth.h>` at line 28.

Static checklist:

| Requirement | Verdict |
| --- | --- |
| Only `libmachook/mac_hooks.m` source changed | PASS: diff name list has only that source file (plus pre-existing coordination state). |
| Only the XPC branch changed | PASS: one hunk at `6356-6375`; no other callsite touched. |
| `MSFindSymbol` implementation/API unchanged | PASS: provider untouched; no new export/helper/gate. |
| Raw result null-checked before signing | PASS: `if (_xpc_bootstrap_services_raw)`. |
| Sign exactly once with A-key/zero | PASS: `ptrauth_sign_unauthenticated(..., ptrauth_key_asia, 0)`. |
| arm64e-only with raw fallback | PASS: `#if __arm64e__` / `#else` raw. |
| Signed pointer only for call/log | PASS: signed variable is logged and used as the call target; no arithmetic/patch/other callsite; x0=dict and call order unchanged. |
| No extra callsite activation | PASS: no new `MSFindSymbol`, no hook/patch change, no role/removal/constant. |
| Provider/AppInput signing unchanged | PASS: no diff in `cydiasubstrate_macos.m`/`AppInputBridge.m`. |
| No unrelated source/build/test diff | PASS: source diff is the single hunk; no test/build input changed. |
| `git diff --check` | PASS: exit 0. |

Minor note: the log now prints the signed pointer (`fn=%p`) rather than the
raw pointer; raw is used only for the null check. This does not change the
call/behavior and is not a scope violation. An untracked
`misc/macos_ws_event_probe` arm64e binary (mtime 2026-10-15) exists in the
working tree; it is not part of the candidate diff and was not modified by
this audit (pre-existing artifact).

Static verdict: **scope PASS**. This is static-only: no build, installed
artifact or runtime acceptance was performed, and the exact 22F82 callee
ABI/dictionary semantics remain as R017/R018 limits.

## 2. MSFindSymbol callsite inventory (source, current tree)

`MSFindSymbol` occurs in these source call patterns:

### Hook inputs (provider patches and signs the returned original)

- `libmachook/mac_hooks.m:2993,3020,3033,3073,3111,3162/3165,3228,3262`
  (SkyLight `PrepareForUse`, StartComposite variants, EndCurrentComposite,
  EndUpdate, WSCompositeDestinationCreate, StopCapture) → `MSHookFunction`.
- `libmachook/mac_hooks.m:8168/8169,8191,8294,8346,8530,8661,8734,8742`
  (assert/abort/AGC/IOHID) → `MSHookFunction`.
- `libmachook/Metal_hooks.x:24832,24835` → `MSHookFunction`.

These are hook/patch inputs; `MSHookFunction` strips its symbol input and
returns a signed thunk. They do not directly call the raw `MSFindSymbol`
result and must not be changed by an XPC-only signing patch.

### Patch/anchor/address arithmetic (no direct function call)

- `libmachook/mac_hooks.m:6165`: `_WSCompositeDestinationCreateWithMetalTexture`
  resolved only to compute a slide and retarget render_update `cbz` bytes.
  The comment says “MSFindSymbol returns a PAC-signed pointer” and masks
  `0x7FFFFFFFFF`, but the provider implementation returns raw
  (`misc/cydiasubstrate_macos.m:250-263`). This is an existing stale
  assumption, not part of the candidate; do not “fix” it or rely on it.
- `MTLCompilerBypassOSCheck/Tweak.x:1461,1526`: `_AIRNTGetVersion` anchor
  with `StripPAC`/relative arithmetic and verifier byte patch, no direct call.

### Direct function-pointer calls

1. **XPC bootstrap** — `libmachook/mac_hooks.m:6359-6366`: raw
   `MSFindSymbol(__xpc_bootstrap_services)` is called with the dictionary.
   This is the target of the proposed narrow change. R017/R018 established:
   raw pointer, `blraaz x19`, A-key/zero contract.
2. **SkyLight deque pop_back** — `libmachook/mac_hooks.m:3216` stores raw
   `sym_pop` into `orig_skylight_state_stack_pop_back`; that pointer is
   called directly at `:2742` in `macws_pop_on_startcomp_bail`. The caller is
   reached only from SkyLight StartComposite hooks that currently have no
   permit (R007), so this direct path is dormant; it is nevertheless another
   raw direct-call pattern and must not be broadened by the XPC change.
3. **Steam Accessibility accessor** — `MacWSSteamProcess.m:1449-1454` casts
   raw `MSFindSymbol` to a function pointer and calls `library(NULL)`; no
   visible sign. Steam-only path; exact arm64e runtime evidence absent.
4. **Steam CreateSimpleProcess** — `MacWSSteamProcess.m:1823` stores raw
   `_CreateSimpleProcess`; `:1599-1605` calls it through a cast; no visible
   sign. Steam-only; exact arm64e runtime evidence absent.
5. **Metal plugin pointer** — `Metal_hooks.x:25099` stores a raw
   `_getMetalPluginClassForService` pointer via `%init`; the source does not
   show a direct call through that variable (`%hookf`’s own original path is
   not visible here). Treat as unresolved, not proven direct.

No other source `MSFindSymbol` call was found; the provider definition itself
(`misc/cydiasubstrate_macos.m:267`) returns raw and is a global API boundary,
not a callsite.

## 3. Established signing contracts that the change must preserve

- `ptrauth_key_function_pointer` = `ptrauth_key_asia` = A-key; C function
  pointer extra data 0 (Xcode 17 `ptrauth.h:15-40`).
- Provider `MSHookFunction` signs its thunk with
  `ptrauth_sign_unauthenticated(thunk, ptrauth_key_asia, 0)`
  (`misc/cydiasubstrate_macos.m:529-534`); probe/orig runtime evidence is
  `orig()=42`.
- Probe re-signs a raw refusal pointer with `ptrauth_key_asia, 0` before a
  call (`macos_mshook_provider_probe.c:125-145`); Form B preserves the
  compiler-signed pointer. Runtime `res()=42` is recorded.
- `AppInputBridge.m:7037-7039,8958-8960` signs resolved candidate pointers
  with `ptrauth_key_function_pointer, 0`; source-confirmed.
- `Metal_hooks.x:24551-24560` uses an address-diversified authenticated-GOT
  signature; return addresses use `asib`, data pointers `asda/asdb`. These
  are unrelated contracts and must not be conflated or changed.

## 4. Exact static gate for the proposed change (when visible)

A candidate is acceptable only if all hold:

1. Diff scope is exactly `libmachook/mac_hooks.m` near the XPC branch; no
   changes to `misc/cydiasubstrate_macos.m`, `include/`, `AppInputBridge`,
   `Metal_hooks.x`, Steam, package/build/test inputs, or unrelated docs.
2. `MSFindSymbol` implementation/API is unchanged; no new exported function,
   helper, environment/role gate, or blanket signing of all results. The
   review explicitly forbids generalizing `MSFindSymbol`.
3. Exactly one local signed function pointer for
   `__xpc_bootstrap_services`; the raw result is null-checked **before**
   signing; only a non-null raw pointer is signed.
4. Signing is exactly
   `ptrauth_sign_unauthenticated(raw, ptrauth_key_asia, 0)` (or the
   `ptrauth_key_function_pointer` alias, 0), arm64e-only with a raw fallback
   on non-arm64; signed once, not double-signed.
5. The signed pointer is used only as the call target; `x0 = dict`, the
   call order, return/log behavior and the dictionary construction are
   unchanged. The raw pointer remains available for the null check/log.
6. No direct callsite is added or retargeted; the SkyLight pop_back, Steam,
   and Metal raw patterns are untouched; no setup-call removal, constant
   return, role/identity gate, or dictionary change.
7. No change to provider/AppInput signing contracts or their existing
   signed-pointer paths; no new call to `MSHookFunction`/TCC/pixels/input.
8. New artifact disassembly must still show the XPC call as the authenticated
   call (`blraaz` target) and the signed pointer path; no raw fallback at the
   call on arm64e. A clean build and device acceptance remain backend/next
   steps and are not claimed here.
9. `git diff --check`, focused provider/route tests, and package verification
   inputs remain clean; no unrelated diff.

## 5. Existing risk notes

- `mac_hooks.m:6168` asserts `MSFindSymbol` returns a PAC-signed pointer; the
  provider returns raw. This is a pre-existing inconsistent comment/mask,
  not a candidate change. Do not modify it as part of the XPC signing fix,
  and do not use it to justify a global API change.
- `MSFindSymbol` is used as a hook/patch input in most callsites; changing
  it globally would alter those contracts. The review’s one-variable limit is
  the correct boundary.
- The XPC callee’s exact 22F82 ABI/dictionary semantics and the signed-target
  runtime control remain unproven (R017/R018); signing the pointer is a
  boundary correction, not acceptance of the callee behavior.

## 6. Commands / exits and limits

Read-only `git status`, `git diff`, `git diff --numstat`,
`git diff --check`, `git log --all -S`, `rg`, `sed`, `grep`, `otool`/`nm`
over the current tree, R017/R018 evidence and R011 result: exits 0; no
source/build/device/Git write. The candidate appeared during the audit and
was reviewed at `libmachook/mac_hooks.m` 14/1 diff, SHA
`5567b452…f087`. Static verdict is scope PASS; no build/install/runtime
acceptance was performed.

(handover to codex now)
