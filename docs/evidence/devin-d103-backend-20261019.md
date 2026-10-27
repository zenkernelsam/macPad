# D103 — TCC client request construction: token/key provenance into D102 attribution

Offline exact-binary RE audit. **No device contact, no runtime probes, no setter
invocation, no token changes, no builds, no source edits, no Git writes.**

## 0. Target identity

- Image: `analysis/TCC_22F82` — arm64e Mach-O shared library (dyld-cache extract)
- UUID `1238D7CA-719A-36A4-94B1-2F387E6B3BF1`
- sha256 `09b841378285a120a04b1c8718dd5c7e3b98b4e59849eea0d8767f845d02f2de`
- Correlated server image: `analysis/d050-tccd/tccd_arm64e`, UUID
  `9845D17F-0409-3884-8A17-260C729636E5` (D100/D102 artifacts)

Raw artifacts (all verbatim `otool -tvV` output or byte dumps, this image only):

| File | sha256 | Contents |
|---|---|---|
| `analysis/tcc-client/d103/d103-preflight-entry-disasm.txt` | `ca24bb79…` | `_TCCAccessPreflightWithAuditToken` `0x1850d03bc` + `_TCCAccessRequest` head/option-parse `0x1850c8d9c` |
| `analysis/tcc-client/d103/d103-request-emit-block-disasm.txt` | `ba694f2a…` | `TCCAccessRequest` emit block `0x1850cc81c`–`0x1850cccf3` |
| `analysis/tcc-client/d103/d103-indirect-disasm.txt` | `2f63f403…` | `_TCCAccessRequestIndirect` `0x1850cd254` + emit block `0x1850cdbfc`+ |
| `analysis/tcc-client/d103/d103-credential-encode-disasm.txt` | `b276edf5…` | `_tcc_credential_encode_to_dictionary` `0x1850d81f0`+ |
| `analysis/tcc-client/d103/d103-setter-disasm.txt` | `398b49fa…` | `_TCCAccessSetPidResponsibleForPid` `0x1850d0574` + IPC blocks |
| `analysis/tcc-client/d103/d103-keytables.txt` | `d9bb7124…` | decoded key/global tables (chained-fixup stripped) |
| `analysis/tcc-client/d103/d103-raw-bytes.txt` | `99e24de0…` | raw instruction words at every token-bearing site |

RE method note: `otool`'s disassembly annotates `__cstring` literal-pool loads but
not `__cfstring`/key-table loads; all cfstring and C-string table entries below
were decoded manually by stripping chained-fixup tag bits (`ptr &
0xffffffffffff`) — decode transcript preserved in `d103-keytables.txt`.

## 1. FACT: `TCCAccessPreflightWithAuditToken` request construction

`_TCCAccessPreflightWithAuditToken` @ `0x1850d03bc` (service=x0, audit_token=x1):

```
options = CFDictionaryCreate(NULL, keys, values, 3,
                             kCFTypeDictionaryKeyCallBacks, /*valueCBs*/ NULL)
  key[0] "kTCCAccessRequestOptionTargetToken"  -> x1  (raw audit_token* — NOT CFData)
  key[1] "kTCCAccessRequestOptionPreflight"    -> &stackblock (invoke 0x1850d548; result mapper)
  key[2] "kTCCAccessRequestOptionSyncCallback" -> kCFBooleanTrue
TCCAccessRequest(service, options, &outer-block)   // bl @0x1850d04f0
```

Value callbacks are NULL, so the token value is the **verbatim caller pointer** —
no copy, no CFData boxing at this stage. Key cfstring objects:
`0x1dd48b0d8`/`0x1dd48b0b8`/`0x1dd48b0f8` (self-named debug strings).

## 2. FACT: `TCCAccessRequest` option → emit-block capture map

`0x1850c8d9c` parses options via `CFDictionaryGetValue`:

| option (cfstring VA) | capture slot in emit block |
|---|---|
| `kTCCAccessRequestOptionPreflight` (`0x1dd48b0b8`) → x22 | block+0x20 |
| `kTCCAccessRequestOptionTargetToken` (`0x1dd48b0d8`) → x23 | **block+0x30** |
| `kTCCAccessRequestOptionSyncCallback` (`0x1dd48b0f8`), `CFEqual`'d vs kCFBooleanTrue → w24 | block+0x48 |
| `kTCCAccessCheckOptionPurposeRequired` (`0x1d8b9d190`) → w25/w26 | block+0x50/+0x4c |
| callback arg (x19), service (x20), queue (x21) | block+0x28, +0x38, +0x40 |

Emit block invoke = `0x1850cc81c` (pacia'd at `0x1850c8eec`).

## 3. FACT: the `TCCAccessRequest` wire message — 6 keys

Emit block `0x1850cc81c` builds `xpc_dictionary_create(keys=x29-0x88,
values=x29-0xc0, count=6)` @ `0x1850ccb04`:

| wire key | value construction | address |
|---|---|---|
| `function` | `xpc_string("TCCAccessRequest")` | `0x1850cc8e8` |
| `service` | `xpc_string(UTF8([blk+0x38]))` | `0x1850cc900` |
| `preflight` | `xpc_bool([blk+0x20] != 0)` — i.e. "Preflight option was supplied" | `0x1850cc90c–0x1850cc91c` |
| **`target_token`** | **`xpc_data_create([blk+0x30], 0x20)` if non-NULL, else `xpc_null_create`** | `0x1850cc924–0x1850cc934`, `0x1850cca30` |
| `background_session` | `xpc_bool(!_is_system_service(service))`, gated by global `0x1d8b9d1f8` (else literal 0) | `0x1850cca3c–0x1850ccac4` |
| `require_purpose` | `xpc_bool([blk+0x50]!=0)` if `[blk+0x4c]` set, else `xpc_null_create` | `0x1850ccad0–0x1850ccaf0` |

Then `_is_system_service(service)` → `_tccd(isSystemService, [blk+0x30])`
@ `0x1850ccb14–0x1850ccb1c` and the message is sent on the returned connection.

**`target_token` provenance (RE-confirmed):** the emitted 32 bytes are exactly
the `audit_token_t` pointer passed by the `TCCAccessPreflightWithAuditToken`
caller, copied verbatim — `xpc_data_create(ptr, 0x20)` at `0x1850cc930`. No
kernel self-token minting, no validation, no length check on the client side.

NULL-token leg (`0x1850cc938`): when no `TargetToken` option exists, the block
runs a `dispatch_sync` round-trip that produces a CFData, reads its first 16
bytes (`CFDataGetBytes`, `cmp w8,#2` @ `0x1850cc9bc`) and invokes the
PAC-authenticated option blocks — a self-identity fallback, not a wire field.
(Theory on the `==2` discriminator: matches the first audit_token `val` word of
a fetched identity; not needed for this task's scope — labeled THEORY.)

## 4. FACT: `TCCAccessCheckAuditToken` and `TCCAccessPreflight` share the channel

- `_TCCAccessCheckAuditToken` @ `0x1850c8bb0` builds the **same** options dict
  plus a 4th key (global at `x22+0x190`), `TargetToken` = its audit-token arg.
- `_TCCAccessPreflight` @ `0x1850c9fa0` builds a **2-entry** dict that **omits
  `TargetToken`** → `CFDictionaryGetValue` returns NULL → wire `target_token` =
  `xpc_null`. The receiver then falls through to the next probe (§6).

## 5. FACT: `TCCAccessRequestIndirect` emits `indirect_object_token`

`_TCCAccessRequestIndirect` @ `0x1850cd254` → emit block `0x1850cdbfc` builds a
single `xpc_dictionary_create(keys, values, 15)` @ `0x1850ce0f0`. The 15-entry
key table is a static `const char*[]` at VA `0x1d3dbde80` (file off 123470 —
why `otool` shows no literal-pool xref for `indirect_object_token`).

| wire key | value | token-relevant |
|---|---|---|
| `function` | `"TCCAccessRequestIndirect"` | |
| `service` | string | |
| `background_session` | `!is_system_service` | |
| `target_csreq` | `xpc_data(CFData@[blk+0x30], CFDataGetLength)` or null | designated requirement, variable length |
| `target_identifier` / `target_identifier_type` / `target_path` / `target_team_id` / `target_prompt` / `target_sdk_version` / `target_platform_type` | string/int64 option fields | |
| **`indirect_object_token`** | **`xpc_data_create(blk+0x78, 0x20)`** @ `0x1850ce09c` | **32-byte audit token** |
| `timeout` | int64 | |
| `preflight` / `require_purpose` | bools | |

plus `xpc_dictionary_set_uint64(dict,"target_csflags",[blk+0x68])` after create
(`0x1850ce11c`).

**`indirect_object_token` provenance (RE-confirmed):** `TCCAccessRequestIndirect`
copies 32 bytes with `ldp q1,q2,[x22]` @ `0x1850cda14` → block+0x78, where
`x22 = x2` — the function's **third argument**, a raw `audit_token_t*` supplied
by the caller (the *indirect object* identity: the file/provider/target the
subject wants access *to*), not the requesting subject.

## 6. FACT: credential-dictionary path

`_tcc_credential_encode_to_dictionary` @ `0x1850d81f0` emits a nested credential
dict containing:

- `TCCD_MSG_CREDENTIAL_AUTHENTICATOR_TYPE_KEY` = uint64 type
- `TCCD_MSG_CREDENTIAL_AUTHENTICATOR_AUDIT_TOKEN_KEY` =
  `xpc_dictionary_set_data(dict, key, ptr, 0x20)` @ `0x1850d824c` — **32-byte
  authenticator audit token**
- `…_FILE_PROVIDER_DOMAIN_ID_KEY` = string
- `…_AUTHENTICATOR_DATA_KEY` = xpc value
- `TCCD_MSG_CREDENTIAL_ASSUMED_IDENTITY` = nested dict

The request-builder at `0x1850d9b48` attaches these under
`TCC_MSG_REQUEST_AUTHORIZATION_SUBJECT_CREDENTIAL_DICTIONARY_KEY`
(`set_value` @ `0x1850d9c04`) and
`…_INDIRECT_OBJECT_CREDENTIAL_DICTIONARY_KEY` (`0x1850d9c50`), with
`function="TCCAccessRequest"` set separately (`set_string` @ `0x1850d9cfc`).
This is the `tcc_authorization_preflight`-family wire shape — a superset
message, distinct from the 6-key preflight message.

## 7. FACT: receiver side — D102 `initWithMessage:` probe order (exact)

In tccd `-[TCCDAttributionChain initWithMessage:evaluateResponsibility:processInfo:]`
(`0x10005001c`, D102 artifact lines ~300–540), `_accessingProcess` resolution
probes the client-declared token fields **in this order**:

1. `xpc_dictionary_get_data(msg, "target_token", &len)` @ `0x100050120`
   — non-NULL ptr → token path
2. else `xpc_dictionary_get_data(msg, "indirect_object_token", &len)` @
   `0x100050150` — non-NULL → token path; NULL → step 3
3. else `xpc_dictionary_get_value(msg,
   "TCC_MSG_REQUEST_AUTHORIZATION_SUBJECT_CREDENTIAL_DICTIONARY_KEY")` @
   `0x100050314` → requires `AUTHENTICATOR_TYPE_KEY == 1` (`cmp/b.ne` @
   `0x10005035c`) → `xpc_dictionary_get_data(dict,
   "TCCD_MSG_CREDENTIAL_AUTHENTICATOR_AUDIT_TOKEN_KEY", &len)` @ `0x100050374`
4. token path (`0x100050160`): sets accessing-specified flag, **requires
   `len == 0x20`** (`cmp/b.ne` @ `0x10005017c–0x100050180`), copies
   `ldp q0,q1` → `createProcessFromAuditToken:processInfo:` → stored at
   `[chain+0x10]` (`_accessingProcess`); non-NULL result → accessing-valid flag
5. `len != 0x20` → logged via `_os_log_type_enabled` + helper
   (`0x1000501cc`–`0x100050220`), then falls through **without** setting
   accessing — i.e., a malformed-length client token degrades to the fallback,
   not to init failure.
6. No accessing token at all → `_accessingProcess` stays nil → D102's accessor
   returns `_requestingProcess` (`+0x20`, built earlier from the
   kernel-attested message audit token — `0x1000500d0` region).
7. `xpc_null` wire values make `xpc_dictionary_get_data` return NULL → treated
   as absent; the probe chain continues (verified: `cbz x0` fallthroughs).

## 8. FACT: the setter carries PIDs, not tokens

`_TCCAccessSetPidResponsibleForPid` @ `0x1850d0574` →
`xpc_dictionary_create(keys@0x1d3dbe030, count=3)`:
`{"function": "TCCAccessSetPidResponsibleForPid", "pid": int64(x20),
"responsible_pid": int64(x19)}` — the responsibility registration wire contract
is **two bare pids + opname**; no audit token is transmitted. Attribution of the
setter call itself therefore rests entirely on tccd's entitlement/
`macos_isSystemServer` gate + the kernel setter's own checks (D100/D101).

## 9. Mapping onto D102 ivars (accessing vs requesting)

| client emission | tccd field | source |
|---|---|---|
| `target_token` (32B, caller-supplied ptr) | `_accessingProcess` (+0x10) via `createProcessFromAuditToken:` | Path A (`TCCAccessPreflightWithAuditToken` arg1, `TCCAccessCheckAuditToken` arg) |
| `indirect_object_token` (32B, arg2 of `TCCAccessRequestIndirect`) | `_accessingProcess` | Path B |
| `SUBJECT_CREDENTIAL_DICT.AUTHENTICATOR_AUDIT_TOKEN` (32B, type==1) | `_accessingProcess` | Path C (`tcc_authorization_*` family) |
| kernel message audit token (XPC connection creds) | `_requestingProcess` (+0x20) | always present |
| none of the above / len!=0x20 / xpc_null | `_accessingProcess` = nil → accessor falls back to `_requestingProcess` | |

For `TCCAccessPreflightWithAuditToken(kTCCServicePostEvent, token)`:
wire = `{function:"TCCAccessRequest", service:"kTCCServicePostEvent",
preflight:true, target_token:<32B caller token>, background_session:<bool>,
require_purpose:<bool|null>}` — and tccd builds `_accessingProcess` from that
caller-supplied token while `_requestingProcess` stays the connection-attested
sender. The client-declared token is exactly the "declared identity" surface
D060/D102 flagged: nothing in the traced client path binds it to the sender.

## 10. Scope limits — what this static trace does NOT prove

- **Construction ≠ emission.** The disasm proves the code paths; whether a
  specific `PostEvent` invocation actually carried a non-NULL `target_token`
  (vs `xpc_null`) requires the saved runtime transaction/log correlation —
  static evidence alone cannot decide which arm ran.
- **pidversion/asid caveat stands** (D102 erratum): no explicit pidversion/asid
  comparison was observed in the tccd attribution path — that is "no extra
  comparison observed in this tccd path," NOT proof that
  `proc_pidpath_audittoken`, `csops_audittoken`, or Security-framework
  dependencies fail to validate those fields.
- **Empty-reply ambiguity stands**: an empty reply remains consistent with P0
  getter error, P1 nil `TCCDResponsibilityIdentity`, P2 nil `TCCDProcess`, and
  now also a declared-token `len!=0x20` arm — no single class is provable from
  the reply alone.
- `kTCCIndirectObjectAuditToken`/`kTCCCodeIdentity*` globals exist (decoded in
  keytables) but no in-image caller of `TCCAccessRequestIndirect` was found —
  it is exported SPI consumed by other frameworks (e.g. callers passing
  indirect-object identities); which producer populates arg2 is outside this
  image.
- `_tccd(isSystemService, token_ptr)`'s second parameter is passed the token
  pointer; whether the connection layer uses it for endpoint selection or
  credential pinning was not fully traced (residual, low-priority).

## 11. Residuals / next discriminators

- R1: decode the NULL-token `dispatch_sync` fetch (`0x1850cc938` leg) — which
  XPC reply it reads and what the `cmp w8,#2` discriminator selects.
- R2: map the 4th option key in `TCCAccessCheckAuditToken` (global at
  `x22+0x190`) — likely a prompt/behavior flag; not token-bearing.
- R3: enumerate external producers of `TCCAccessRequestIndirect` arg2 /
  credential dicts (`Security`, `AppKit` images) — not locally audited.
- R4: confirm `_tccd` connection-helper semantics for the token argument.

All forward claims in this memo are RE-confirmed against `TCC_22F82` (UUID
above) or `tccd_arm64e` D102 artifacts at the cited addresses; no runtime
assertion is made.
