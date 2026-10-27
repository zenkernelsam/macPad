# Frontend R019 result — XPC signing change not yet visible; static scope gate recorded

Date: 2026-10-18. Read-only scope audit. No source/test/build-input edit, no
build, no deploy, no SSH, no Git write. Added only this result and
`docs/evidence/r019-xpc-signing-scope-audit-20261018.md`
(SHA-256 `c8f9cfc0d350fe7bf706f1147983db5a3b3203a5d225b64ef15face76eab3b87`).

Candidate HEAD: `c97e219`.

## Candidate visibility and verdict

No candidate was visible at the start of the audit (only pre-existing
coordination state and untracked relay/artifact files; `git diff --name-only`
had no source path; `git log --all -S` found no signing commit; backend R011
states no source change). During the audit a candidate appeared:
`libmachook/mac_hooks.m` mtime `2026-10-18T18:57:05+0800`, diff
14 insertions/1 deletion, SHA-256
`5567b452df17c5720ef08c54fc95768cd71e3377a099caf8e4e7ad3a6f03f087`.

The candidate is a single hunk in the XPC branch:

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

The existing log, null gate, dictionary x0 and direct call are unchanged.
`mac_hooks.m` already imports `<ptrauth.h>`.

**Static verdict: scope PASS.** One source file, one XPC hunk, one
`MSFindSymbol` call, raw null-checked before a single A-key/zero sign,
arm64e-only with raw fallback; no global `MSFindSymbol`/provider/AppInput/
Metal/Steam changes, no new callsite, no role gate/removal/constant. The log
prints the signed pointer rather than raw (non-functional). `git diff --check`
exit 0. Untracked `misc/macos_ws_event_probe` (arm64e, mtime 2026-10-15) is
not in the candidate diff and was not modified.

## MSFindSymbol callsite classification (current source)

**Hook inputs (provider patches/signs original; not direct):**
`mac_hooks.m:2993,3020,3033,3073,3111,3162/3165,3228,3262`,
`:8168/8169,8191,8294,8346,8530,8661,8734,8742`;
`Metal_hooks.x:24832,24835` → `MSHookFunction`.

**Patch/anchor/address arithmetic (no direct call):**
`mac_hooks.m:6165` (SkyLight slide/cbz retarget; stale comment claims
`MSFindSymbol` is PAC-signed, but provider returns raw);
`MTLCompilerBypassOSCheck/Tweak.x:1461,1526` (`StripPAC` + verifier patch).

**Direct function-pointer calls:**
1. **XPC bootstrap** `mac_hooks.m:6360-6365` — the intended narrow target;
   raw pointer, `blraaz x19`, A-key/zero (R017/R018).
2. **SkyLight deque pop_back** `mac_hooks.m:3216` raw → direct call at
   `:2742` in `macws_pop_on_startcomp_bail`; parent StartComposite hooks are
   currently unpermitted/dormant, but this is a raw direct-call pattern that
   must not be broadened.
3. **Steam Accessibility accessor** `MacWSSteamProcess.m:1449-1454` raw cast
   → `library(NULL)`; Steam-only, no exact arm64e runtime evidence.
4. **Steam CreateSimpleProcess** `MacWSSteamProcess.m:1823` raw stored →
   cast call at `:1599-1605`; Steam-only, no exact arm64e runtime evidence.
5. **Metal plugin pointer** `Metal_hooks.x:25099` raw stored via `%init`; no
   visible direct call through that variable; unresolved.

Most other `MSFindSymbol` uses are hook/patch inputs; the provider definition
(`cydiasubstrate_macos.m:267-283`) returns raw and is a global API boundary,
not a callsite.

## Existing signing contracts to preserve

- `ptrauth_key_function_pointer` = `ptrauth_key_asia` = A-key; function
  pointer extra data 0 (Xcode 17 `ptrauth.h:15-40`).
- Provider `MSHookFunction` signs its thunk with
  `ptrauth_sign_unauthenticated(thunk, ptrauth_key_asia, 0)`
  (`cydiasubstrate_macos.m:529-534`), runtime `orig()=42`.
- Probe re-signs a raw refusal pointer with `ptrauth_key_asia, 0`
  (`macos_mshook_provider_probe.c:125-145`), runtime `res()=42`.
- `AppInputBridge.m:7037-7039,8958-8960` signs candidate pointers with
  `ptrauth_key_function_pointer, 0`.
- `Metal_hooks.x:24551-24560` uses address-diversified GOT auth; return
  addresses use `asib`, data pointers `asda/asdb`; separate contracts.

## Exact static gate (candidate checked against every item)

1. Diff only `libmachook/mac_hooks.m` near the XPC branch; no changes to
   provider/include/AppInput/Metal/Steam/package/build/test/unrelated docs.
2. `MSFindSymbol` implementation/API unchanged; no global signing, new
   export/helper/role gate, or blanket result signing.
3. Exactly one local signed pointer for `__xpc_bootstrap_services`; raw
   result null-checked before signing; only non-null signed once.
4. Sign exactly `ptrauth_sign_unauthenticated(raw, ptrauth_key_asia, 0)` (or
   the function-pointer alias, 0), arm64e-only with raw fallback; no double
   sign.
5. Signed pointer used only for the log and call target; `x0=dict`, call
   order, log format and dictionary construction unchanged (printed pointer
   value is now signed, non-functional).
6. No new/retargeted direct callsite; pop_back/Steam/Metal raw patterns
   untouched; no setup removal, constant return, role gate, or dictionary
   change.
7. Provider/AppInput signing contracts unchanged; no new
   `MSHookFunction`/TCC/pixel/input behavior.
8. New artifact disassembly must still show authenticated XPC call
   (`blraaz`) and no raw fallback on arm64e; clean build/device acceptance
   remain separate backend steps.
9. `git diff --check`, focused provider/route tests and package inputs clean;
   no unrelated diff.

The stale `mac_hooks.m:6168` comment claiming a PAC-signed `MSFindSymbol`
result is pre-existing and must not be changed/reused as a global API
justification. Exact 22F82 callee ABI/dictionary semantics and a signed-target
runtime control remain unproven (R017/R018); signing is a boundary correction,
not callee acceptance.

## Limits

The candidate satisfies the static gate by inspection; static-only, no build,
installed artifact or device runtime acceptance. Exact 22F82 callee
ABI/dictionary semantics remain unproven (R017/R018). No source, build,
device or Git action was taken by this audit.

Full detail:
`docs/evidence/r019-xpc-signing-scope-audit-20261018.md`.

(handover to codex now)
