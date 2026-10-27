# D104 — `_tccd` token-argument semantics + SkyLight callsite/runtime correlation

Offline exact-binary RE + saved-evidence correlation only. No device, probes,
KRW/TCC, setters, token changes, source edits, builds, or Git writes.

Targets: `analysis/TCC_22F82` (UUID `1238D7CA-719A-36A4-94B1-2F387E6B3BF1`,
sha256 `09b84137…`), correlated against `analysis/SkyLight_22F82` (UUID
`96676A53-B1E0-3D7E-B98B-B73873CD1880`) and saved runtime artifacts.

Artifacts:

| File | sha256 | Contents |
|---|---|---|
| `analysis/tcc-client/d104/d104-tccd-helper-disasm.txt` | `1ccc6f60…` | `_is_system_service` `0x1850c94b4`, `_tccd` `0x1850c9584` full body, `_tccd_send_message` head |
| `analysis/tcc-client/d104/d104-raw-bytes.txt` | `b5dcbdc7…` | raw instruction words at all three sites |
| `analysis/tcc-client/d104/d104-servicetable.txt` | `3d4932f5…` | decoded 7-service system table + endpoint names |

## 1. FACT: `_tccd(int isSystemService, const audit_token_t *token)` @ `0x1850c9584`

Full decompile of the token leg (`0x1850c95bc`–`0x1850c967c`), verbatim disasm
in artifact:

```
if (w0 != 0)  goto select;                 // system service → token SKIPPED entirely
if (x1 == 0)  goto select;
uid  = audit_token_to_auid(token);          // token.val[0]
self = getuid();
if (uid == -1) {                            // auid absent
    uid = audit_token_to_euid(token);       // token.val[1]
    [dispatch_once'd log] "auid = -1; routing based on euid (%u) instead"
}
// 0x1850c9654: csel chain
w22 = 1  if uid ∈ {-3,-2,-1} (cmn w21,#3;hi)
      else (self!=0 && uid!=self)
x19 = token  if uid∈{-3,-2,-1} or uid!=self, else NULL
w20 = uid    if uid∈{-3,-2,-1} or uid!=self, else self
select:
  name = (w22==0) ? "com.apple.tccd" : "com.apple.tccd.system"
  flags = (w22!=0) << 1                     // 0x2 = PRIVILEGED on system path
  conn = xpc_connection_create_mach_service(name, global_queue, flags)
  if (conn && w22==0 && token_survived)     // user-tccd leg only
      xpc_connection_set_target_uid(conn, w20)
  xpc_connection_set_event_handler(conn, block@0x1dd48c440); resume; return
```

**What the second argument controls (RE-confirmed):** endpoint selection only.
- `auid`→uid of the *declared* token picks `com.apple.tccd` vs
  `com.apple.tccd.system` and, on the user-tccd leg, sets
  `xpc_connection_set_target_uid` to reach that uid's per-user daemon.
- The token is **never** placed in the outgoing message and **never** mutates
  the connection's kernel-attested sender credentials. A declared
  `target_token` cannot change who tccd believes the *sender* is — the D102
  `_requestingProcess` stays kernel-attested. Confirms the task's constraint:
  sender identity is not client-mutable through this argument.
- **No connection cache exists in `_tccd`.** Every call creates a fresh
  `xpc_connection_create_mach_service` + event-handler + resume. All 35
  in-image `bl _tccd` callsites consume the result into a local register and
  pass it to `_tccd_send_message`/`_tccd_send_v2` — no store-to-global observed.
- Negative-uid tokens (`auid`/euid ∈ {-3,-2,-1}) route to the system name.
- Non-root caller + different-uid token → system name; root caller +
  different-uid token → user name + `set_target_uid(uid)`; same-uid → user
  name, no retarget.

## 2. FACT: `kTCCServicePostEvent` is a system service — the token arg is dead at the actual callsite

`_is_system_service` @ `0x1850c94b4` `CFStringCompare`s the service against 7
globals — decoded verbatim (`d104-servicetable.txt`):

`Accessibility, PostEvent, ListenEvent, DeveloperTool, ScreenCapture,
SystemPolicyAllFiles, EndpointSecurityClient`

So for the SkyLight PostEvent check: `is_system_service("kTCCServicePostEvent")
== 1` → the emit block calls `_tccd(1, token)` → `cbnz w0` at `0x1850c95bc`
skips the entire token/uid block → **the 32-byte token pointer is never even
dereferenced at this layer**; the connection is always
`com.apple.tccd.system` + privileged flag `0x2`.

Consequence on the wire (combined with D103): the PostEvent preflight message
is `{function:"TCCAccessRequest", service:"kTCCServicePostEvent",
preflight:true, target_token:<32B sender token or null>,
background_session:false, require_purpose:null}` sent to
**`com.apple.tccd.system`** — not the user daemon. `background_session` is
`!is_system_service` → always `false` here; `require_purpose` is `xpc_null`
because `TCCAccessPreflightWithAuditToken` never sets the
`kTCCAccessCheckOptionPurposeRequired` option (w26=0 arm).

## 3. FACT: SkyLight callsite provenance (D043/D045 pins, re-verified)

`CGXSenderCanSynthesizeEvents` @ `+0x318464`: `gCurMsgAudit`
(`__DATA+0x1d8ba61b8`) container non-NULL + `size ≥ 0x34` → copies
`container+0x14..+0x33` (32B audit token) → dispatcher
`WSAuditTokenHoldsCachedProcessRight` `+0x3407ec` → on miss → impl
`audit_token_check_tcc_access` `+0x340da0` → `checker(service=desc[0],
x1=&token_copy, x2=0)` where checker = lazily-resolved
`TCCAccessPreflightWithAuditToken` (cell `0x1d8ba6b50`).

Callsite convention: **x1 = pointer to the 32-byte token copy** — matches the
client's dereference of x1 as a pointer (`xpc_data_create(ptr,0x20)`), so the
probe-source comment "audit_token_t BY VALUE / passing a pointer crashes"
describes that probe's own typedef misuse, not the binary's contract.
RE-confirmed from both sides.

## 4. Saved-runtime correlation — what is and isn't established

Saved WS-side evidence:

```
#### CGXSYNTH cred=0x16da6b220 size=52 euid=0 pid=42636 rc=0        (R002-era)
#### CGXSYNTH cred=0x16f4c7220 size=52 euid=0 pid=45811 rc=0 (x2)   (R003-era)
```

(DEVIN_STATUS.md verbatim captures) — the `gCurMsgAudit` container is non-NULL
and 52 bytes on real canonical posts, with decoded sender scalars
(euid=0, pid=probe). **A non-null, well-formed sender token at the gate
boundary is runtime-established.**

R005 lldb breakpoints (same slide, word-verified): fresh-pid miss →
`audit_token_check_tcc_access` → `blraaz` → **checker ran and returned raw
w0=1** → deny folded → 1002. **A non-null token reaching
`TCCAccessPreflightWithAuditToken` and a nonzero checker return are
runtime-established** (for that generation).

R006/R007 transport captures (file-offset breakpoints on the client's
`tccd_send_message` inside WS):

- **Pre-remap generation**: sync return = `_xpc_error_connection_invalid` on
  all 8 sends — `com.apple.tccd.system` was not registered (iPadOS system
  domain: `launchctl … "Could not find service"`; only `user/501/com.apple.tccd`
  exists; the macOS daemon ran as `com.apple.macosbooter.tccd.system` and the
  XPC-name remap was missing from `Metal_hooks.x`). **No tccd ever received the
  request — the responsibility getter could not have run.** The R005-era
  w0=1/1002 producer was the client-side transport failure, not any daemon
  evaluation.
- **Post-remap generation (R007)**: 3 sends → real `OS_xpc_dictionary` replies;
  handler walked known keys (`preflight_unknown`, `restricted`, `result`,
  `do_not_cache`); `xpc_dictionary_get_bool(reply,"result")` → `w0=0`; minis
  `w1=0`; SkyLight impl still `w0=1` → 1002. A real Ventura-tccd answer —
  meaning `initWithMessage:` completed far enough to produce a reply — but
  **which internal class produced the absent/false `result` is not
  established** (R007 correction 5: `result`-key presence itself unproven;
  P0 getter error / P1 nil ResponsibilityIdentity / P2 nil TCCDProcess /
  genuine deny all remain consistent).

**Verdict on the task question:** saved evidence establishes a non-null token
*into the checker boundary* (gate cred → impl → blraaz → checker w0=1) and, for
post-remap generations, delivery of a `target_token`-bearing message to a real
tccd. It does **not** establish "a non-null token passed to the particular
getter failure" — for pre-remap captures the getter never ran (transport), and
for post-remap captures the reply proves only that *some* tccd path ran, not
the responsibility-getter arm specifically. The getter-103 hypothesis remains a
static potential route, not a runtime-attested producer.

## 5. D103 erratum — `len != 0x20` is not a failure producer

Correcting D103 §10's class list: in `initWithMessage:` a client token with
`len != 0x20` is logged and skipped → `_accessingProcess` stays nil → the
accessor falls back to `_requestingProcess`. **That fallback is not itself a
failure** — the chain proceeds and `_evaluateResponsibleProcess` still runs
against the requesting process. An empty reply requires the *subsequent*
attribution to fail; the len!=0x20 arm only narrows which process gets
evaluated, it cannot by itself explain an empty reply. (D103's "degrades to
the fallback, not to init failure" sentence was correct; the failure-class
enumeration overstated its reach — the arm belongs to the *input-shaping*
column, not the failure-producer column.)

## 6. Residuals

- R1: the `0x1db437e**` global group behind the NULL-token `dispatch_sync` leg
  (`0x1850cc938`) is a one-shot self-identity fetch, not a connection cache —
  its fetch target is unmapped here.
- R2: `_tccd_send_message`'s internal reply-type plumbing (sync return →
  handler @ `0x1850ccebc`) is only sketched; the R006/R007 breakpoint offsets
  +940/+976/+1128/+1244 map to its send/error ladder but were not
  re-disassembled for this task.
- R3: whether the R007-era reply dicts came from WS's PostEvent check
  specifically vs concurrent traffic in the same capture window is bounded by
  the `postmin`-per-trial design but not per-message attributed.
