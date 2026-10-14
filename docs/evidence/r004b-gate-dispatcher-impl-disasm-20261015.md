# Gate → dispatcher → impl — full disassembly (RE-confirmed)

Evidence date 2026-10-15.  Source binary:
`analysis/SkyLight_22F82` — LC_UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`,
sha256 `13d7a0ca010a9808234c71c34fd650b26329de99d1aceaeb5ddbb58f8b906c61`.
`__TEXT` va `0x1850e5000` → file offset 0 (linear).  Decoder: capstone 5.0.7.
Annotated dumps: `coordination/artifacts/r004b-disasm-{gate,dispatcher,impl}.txt`.

**ERRATA (2026-10-15):** superseded by `ventura-22F82-tcc-checker-20261015.md`
+ `coordination/artifacts/R005-static/exact-functions.txt` — full
symbol-resolved LLDB disassembly on the same binary.  Real symbol names:
gate=`CGXSenderCanSynthesizeEvents`, dispatcher=
**`WSAuditTokenHoldsCachedProcessRight`** @`0x1854257ec`, impl=
**`audit_token_check_tcc_access`** @`0x185425da0`, resolver=
**`tcc_lookup_symbol`** @`0x185425e44`.  Corrections to this doc:
(1) cache check is two-stage — `rightDesc+0x18` known-bits test decides
whether the impl is skipped, `rightDesc+0x10` then decides allow/deny —
NOT a single OR of both masks; (2) the `0x1d8ba6b48` cell is
`getProcessCredentialCache.onceToken` initialized via `dispatch_once`,
not an opaque sentinel; (3) new-key insertion runs through
`WSDoWithCredentials` + `__setProcessFlagsInCache_block_invoke` and
registers `processCredentialCacheRemoveDeadProcess` via
`register_for_port_notification_in_session`; (4) raw TCC return `0` =
pass at this callsite — any older claim that TCC preflight returning 1
meant allow is retracted (callsite-specific `cset eq`); (5) the right
object symbol is `kWSTCCServicePostEvent` (payload string
`"kTCCServicePostEvent"`, len 20 — both readings verified).

## 1. Gate — `CGXSenderCanSynthesizeEvents` @ `0x1853fd464`

```
ldr x8, [gCurMsgAudit @ __DATA+0x1d8ba61b8] ; NULL → return 0
ldrb/cmp size >= 0x34                        ; small → return 0
copy audit (32B) → sp+0x30
copy rightDesc @0x1d3dcb6c8 (40B) → sp
x16 = paciza(0x185425da0)                    ; signed impl ptr
bl dispatcher @0x1854257ec  (x0=audit, x1=rightDesc, x2=impl)
retab → returns dispatcher result
```

Thin marshal + signature; no decision of its own beyond audit presence/size.

## 2. Dispatcher @ `0x1854257ec`

```
pid    = audit_token_to_pid(audit)          ; stub → libbsm (0x1dff3f358)
pidver = audit_token_to_pidversion(audit)   ; stub → libbsm (0x1dff3f360)
if pid == 0:            return (rightDesc[0x20] != 0)      ; auid path
if pid == getpid():     return 1                            ; SELF allowed
key = pid | pidver<<32
if cacheSentinel @0x1d8ba6b48 == -1:        ; normal mode
    cached = rightsCache @0x1d8ba6b40 ? CFDictionaryGetValue(cache,key) : 0
    if (rightDesc[0x18] & cached) || (rightDesc[0x10] & cached):
        return 1                          ; cached permit mask
    result = impl(audit_copy, rightDesc[0])  ; blraaz — the checker call
    bits   = result ? rightDesc[0x10] : rightDesc[8]
    if rightsCache:
        bits |= cached
        CFDictionaryContainsKey ? CFDictionarySetValue(cache,key,bits)
                                : insert via dict-helper @0x185425a60
    return result
```

The cache is a per-WS-process CFDictionary keyed by `{pid | pidversion<<32}`
holding a rights bitmask; the impl runs only on miss, and the result is
folded back as permit/deny bits.  Stubs confirmed by runtime probe dump:
`audit_token_to_pid/pidversion`, `getpid`, `CFDictionaryGetValue`.

## 3. impl @ `0x185425da0` — the checker is TCC preflight

```
if !lazyChecker @0x1d8ba6b50:
    name = "TCCAccessPreflightWithAuditToken"      ; cstr @0x1854d3c3e
    lazyChecker = resolve(name)                    ; helper @0x185425e44
    if !lazyChecker → return 0
return checker(rightDesc[0] → CFString, audit_copy, 0) == 0
```

Resolver @`0x185425e44`: `dlopen("/System/Library/PrivateFrameworks/
TCC.framework/TCC", 5)` (stub @0x1dfef4428 — runtime-confirmed dlopen),
then `dlsym(name)` (stub @0x1dfef4440), logging "Could not load TCC" /
"Could not find TCC symbol" via the os_log path on failure.

So the lazy checker is **`TCCAccessPreflightWithAuditToken`** resolved once
per process through SkyLight's own public `dlopen`/`dlsym` stubs.  LOADER-OBS
= 0 in our window is fully consistent: resolution happens once on the first
miss (likely during WS-internal activity long before our post); a cached
deny needs no impl call at all.  No loader silence is claimed beyond the
observed window.

## 4. rightDesc contents (file-verified @ __DATA_CONST)

| address | name object → string | [8] | [0x10] | [0x18] | [0x20] |
|---|---|---|---|---|---|
| `0x1d3dcb6c8` | **`kTCCServicePostEvent`** | 1 | 2 | 3 | 1 |
| `0x1d3dcb760` | `com.apple.private.SkyLight.event.monitor` | 4 | 8 | 0xc | 0 |
| `0x1d3dcb7a8` | **`kTCCServiceListenEvent`** | 0x100 | 0x200 | 0x300 | 1 |

Sibling impl @`0x185425ff0` (used by the `0x1854260b4` caller with
rightDesc@0x1d3dcb760/7a8 selected by flag @0x1d8ba6b60) does a different
check: `SecTask*`-family calls (`0x18547badc`, `0x18547babc` stubs) then
compares the produced value against a global constant
(`cset w0, eq`) — an **entitlement-equality check** for the
`event.monitor` path, not a TCC service.

## 5. What this proves for the 1002 denial

`SLSPostEventRecord → gate → dispatcher → cache miss → impl →
TCCAccessPreflightWithAuditToken("kTCCServicePostEvent", sender_audit, 0)
!= 0` → impl 0 → dispatcher caches `rightDesc[8]=1` deny bits → gate rc=0
→ WS reply 1002.

The authentic Ventura input path therefore requires a **real TCC grant for
`kTCCServicePostEvent` on the sender's audit token** (or same-pid posts,
which WS allows unconditionally).  This is RE-confirmed against the pinned
binary; the producer that would normally satisfy the grant on macOS is the
loginwindow/user-session TCC path — matching the earlier runtime fact that
the probe lacks `com.apple.SkyLight.loginwindow` and that
`SLSCreateLoginSession` yields `sid=0` here.

## 6. Non-claims

- No statement that "the gate never calls dlopen/dlsym" — it does, once,
  lazily, per process; our window simply didn't contain a first call.
- `dladdr` nearest-symbol reporting in probe dumps is heuristic; stub
  identities above are RE-confirmed via slot-address matching plus the
  probe's resolved table only where explicitly stated.
- Static interpretations predate a functioning IDA session and remain
  capstone-verified on this exact binary only.
