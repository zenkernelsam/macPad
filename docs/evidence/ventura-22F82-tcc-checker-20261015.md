# Ventura 22F82: exact permission-check chain, 2026-10-15

Status: exact static disassembly plus prior bounded runtime evidence; **input remains unfixed**. This note supersedes the unidentified-checker and “preflight1 means allow” interpretations. It does not diagnose which branch the current WindowServer takes.

## Identity and method

Input: `analysis/SkyLight_22F82`, arm64e UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`, SHA256 `13d7a0ca010a9808234c71c34fd650b26329de99d1aceaeb5ddbb58f8b906c61`, image base `0x1850e5000`.

Under the user's replacement AGENTS instructions, exact-file otool/Capstone/LLDB disassembly and verbatim runtime evidence are valid evidence channels. The former IDA-only coordination restriction is superseded for this task. Tool availability does not retroactively validate old interpretations: each conclusion below cites fresh exact-file LLDB output.

Raw complete functions: `coordination/artifacts/R005-static/exact-functions.txt`; command/exit record beside it. LLDB `image list` confirms UUID/base. Initial command failed because `image list -v` is unsupported on this host; that failed attempt remains in `identity.json`/`lldb-dispatcher.txt`, not counted as analysis success. Corrected invocations exit0.

## RE-confirmed: the checker is TCC preflight, with zero meaning pass at this callsite

`audit_token_check_tcc_access`, VA `0x185425da0` (image+`0x340da0`):

```text
0x185425dd4 ldr   x8, [x21, #0xb50]       // cached function
0x185425dd8 cbnz  x8, 0x185425df4
0x185425de0 add   x0, x0, #0xc3e           // "TCCAccessPreflightWithAuditToken"
0x185425de4 bl    0x185425e44              // tcc_lookup_symbol
0x185425dec str   x0, [x21, #0xb50]
0x185425df0 cbz   x0, 0x185425e14          // unresolved => zero return
0x185425df4 ldp   q0, q1, [x20]           // original audit bytes
0x185425df8 stp   q0, q1, [sp]
0x185425dfc mov   x1, sp
0x185425e00 mov   x0, x19                  // service from right descriptor
0x185425e04 mov   x2, #0x0
0x185425e08 blraaz x8
0x185425e0c cmp   w0, #0x0
0x185425e10 cset  w0, eq
```

Therefore this caller treats raw TCC preflight0 as true/pass and any nonzero as false/deny. The earlier standalone probe returning1 is **not** an allow witness. This is callsite-specific; do not infer other TCC functions' return conventions.

`CGXSenderCanSynthesizeEvents` references `kWSTCCServicePostEvent` at VA `0x1d3dcb6c8`; LLDB resolves that symbol. The original gate passes `audit_token_check_tcc_access` to `WSAuditTokenHoldsCachedProcessRight`.

## RE-confirmed: loader and rights caches explain why absent loader logs are inconclusive

`tcc_lookup_symbol`, VA `0x185425e44` (image+`0x340e44`), loads `/System/Library/PrivateFrameworks/TCC.framework/TCC` via dlopen with mode5 when its cached handle is zero, then resolves the requested symbol through dlsym. It logs an error and returns0 on load/symbol failure. The string addresses are decoded by LLDB; earlier hand-derived truncated string addresses are not reused.

`WSAuditTokenHoldsCachedProcessRight`, VA `0x1854257ec` (image+`0x3407ec`):

- decodes pid and pidversion via their named libbsm stubs; pid matching WS's own pid returns1;
- combines pid/pidversion into the dictionary key and initializes the cache with dispatch_once as needed;
- tests cached bits against right descriptor+0x18 (known bits); if present, descriptor+0x10 decides the returned boolean;
- otherwise calls the supplied checker at `0x185425914`, then chooses descriptor+0x10 on true or+0x08 on false and updates the process flags cache;
- writes existing keys through CFDictionarySetValue and registers new-process handling through `__setProcessFlagsInCache_block_invoke`.

A cached deny can thus return0 without loader activity; a cached checker pointer can call TCC without dlsym. Neither branch is proven to be the currently taken one. The R004B result—two real gate calls returning0 with no observed public dlsym—is consistent with multiple branches and remains INCONCLUSIVE about the cause.

## Next runtime distinction

In ONE exact WS generation, collect the actual sender pid/pidversion, right bits, cached flags, checker entry/return and, if reached, raw w0 immediately after `0x185425e08`. Prefer bounded debugger breakpoints at validated runtime addresses; no extra functional calls, no fake rights and no constant-return override. Correlate this with one canonical event and reply1002. If live raw TCC return is nonzero, trace the actual policy/service/identity evaluation upstream; do not flip entitlements or session state blindly.

Probe-local globals and a matching symbol name alone do not establish the live WS branch. Input acceptance still requires WS-DISPATCH plus visible interaction.
