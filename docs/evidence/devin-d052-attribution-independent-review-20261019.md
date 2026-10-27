# D052 — independent attribution-control & causal audit (read-only)

Date 2026-10-19, packet `coordination/_relay/devin-052-frontend-task.md`.
**Scope: read-only local review of the exact 7-key request/preflight
options, minimum sender-attribution evidence, and a fresh independent
control-flow audit of D050's tccd claims on the saved binary. No device,
no backend contact, no production/source/Git changes.**

Binary audited: `analysis/d050-tccd/tccd_arm64e` (extracted slice of
`tccd_22F82_live`, sha256 `42c388e8…` = R002 live fingerprint — re-hashed
this round). Client image: `analysis/TCC_22F82` (`09b84137…`, arm64e
`1238D7CA-…-3BF1`).

## 1. Request construction — verified prompt-free & read-only

`macwstccdprobe/main.c:134-162` calls the **genuine**
`TCCAccessPreflightWithAuditToken` (`dlopen`+`dlsym`, no hand-built
dict) — so the 7-key wire schema is authored by Ventura TCC.framework,
not the probe. Probe contributes only service name + caller token.

Disasm of TCC `+0x93bc` (`TCCAccessPreflightWithAuditToken`):

- Builds a 3-key options dict via `CFDictionaryCreate(alloc=0,
  keys=fp-0x50, values=fp-0x68, count=3, keyCB=const, valueCB=NULL)`.
  Keys decode to **`_kTCCAccessRequestOptionTargetToken`,
  `_kTCCAccessRequestOptionPreflight`,
  `_kTCCAccessRequestOptionSyncCallback`**; values = `x1` (token ptr),
  `&error-block(+0x548)`, `kCFBooleanTrue`.
- Calls `TCCAccessRequest(service, options, &reply-block)` — a
  **preflight** request carrying the caller's audit token, with
  synchronous callback delivery. Wire keys (R008): `function=
  TCCAccessRequest`, `service`, `preflight=true`, `target_token`,
  `TCCD_MSG_ID`, `require_purpose=null`, `background_session=false`.

Daemon-side confirmation the request is **check-only**: `do_TCCAccessRequest`
reads `"preflight"` (`tccd_arm64e 0x11f8c`), and the literal
*"Service kTCCServiceAccessibility does not allow prompting; returning
preflight_unknown"* (`0x13698`) proves the preflight path bypasses
prompting. **FACT (static): the 7-key preflight request does not prompt
and writes no policy state** — the reply-carrier fields are
`result`/`preflight_unknown`, not mutation.

**ABI settled (was contested):** `x1` is a **pointer to a 32-byte
`audit_token_t`**. Both call shapes emit it identically — SkyLight's
impl `mov x1, sp` and the probe's compiled by-value typedef both lower
to `x1 = &token_copy` (probe `0x100003a3c`: `ldp/stp` 32B copy →
`add x1, sp, #0x60`). TCC stores `x1` verbatim as the `TargetToken`
dict value (`stur x1, [x29,#-0x68]`, value callbacks NULL — no retain
expected). By-value and by-pointer are the same mechanism here.

## 2. D050 control-flow audit — independent verification on `tccd_arm64e`

Every anchor confirmed by direct disasm:

| claim | site | status |
|---|---|---|
| sole `xpc_dictionary_create_reply` | `0x6508` → `x23` (empty by construction) | ✓ |
| `get_string("function")`; missing→refuse | `0x64e8` → `0x67e4` | ✓ |
| strcmp dispatch on function | `0x6ba0` chain (pre-attribution) + `0x68a4` chain (post-attribution → `TCCAccessRequest`→`0x6fd8`) | ✓ real handlers exist |
| attribution-fail → ERROR-gated log + unconditional empty send | `0x69ac` → `os_log_type_enabled(0x10)` `0x69c8` → helper `0x52ca4` ("Failed to create attribution chain from message." @`0x52cc0`) → send `0x69e0` | ✓ — matches R002 transcript verbatim |
| alternative empty sends | `0x6744`, `0x746c`, `0x78f0` (+ `0x69e0`) | ✓ — EMPTY has multiple producers |
| only one `set_value` in handler | `0x7968` ("user_tccd_unavailable") | ✓ |
| `responsibility_get_responsible_audit_token_for_audit_token` | imported `U`, called at `0x4fb48` (attribution ctor region) | ✓ — the #103 call is genuine tccd code |
| `TCCDAttributionChain` ObjC class | ivars `targetPID`, `responsiblePID`, `accessorAuditToken`, `responsibleAuditToken` | ✓ structurally real |

**Nuances D050's wording should carry:**

- **Attribution creation is flag-gated, not universal**: `0x6708`
  `tbnz w0,#0 → 0x6838` selects it; `w28==0` path jumps straight to
  dispatch via `0x69e8`. The EMPTY reply observed correlates with the
  ERROR line for *that* request (R002) — EMPTY alone is **not unique**
  to the attribution bail (four+ empty-send exits share `x23`).
- **REQUEST log is INFO-level gated** (`os_log_type_enabled` `w1=1` at
  `0x6638`, `cbz → 0x66c4` skips it). ERROR (`0x10`) is near-always on;
  INFO is not. **Zero REQUEST lines cannot prove a request never
  arrived** — only absence of the ERROR line bounds the bail path.

## 3. Correction — `sender_auid=501` is the interpose, not session context

The REQUEST log's `sender_auid` field is populated via
`_audit_token_to_auid` (stub call `0x65b8`). libmachook **unconditionally
interposes** it (`mac_hooks.m:13721-13723` returns 501 always;
`DYLD_INTERPOSE` `15442`) — and tccd is injected (R004 §4: it takes the
libxpc create hooks via `InitMetalHooks`). **So `sender_auid=501` is the
forced value; the genuine connection auid is unmeasured in that line**
(likely `-1`, consistent with `target_token`'s embedded `auid:-1` and
with `#103`). `audit_token_to_euid` is NOT interposed → `sender_uid=0`
is genuine root.

Consequence: D050's "501-vs-(-1) asymmetry" is not evidence of
session-vs-token difference; it is forged-vs-raw. The *real* evidence
for unattributed sender is the `#103 Policy not found` itself — which
runs on the **connection's** audit token (pid-keyed responsibility
table), independent of the interpose.

## 4. Minimum sender-attribution evidence — what "attributed" must show

`auid≥0` alone is **not sufficient** and probably not the operative
field: the `#103` failure is `…for_audit_token() from PID[46525]:
Policy not found` — a **PID/responsibility-policy registration**
lookup, not an auid check. Minimum distinguishing evidence for a
genuinely-attributed sender:

1. the sender's **connection audit token** (kernel-issued,
   `xpc_connection_get_audit_token` — unforgeable by our interposes)
   produces a **successful** `responsibility_get_responsible_audit_token`
   — witnessed by *absence* of the ERROR line **and** a non-empty reply;
2. corroborated by raw `token.val[]` fields read via `task_info` (mach
   trap — NOT through interposed helpers), since any
   `audit_token_to_auid` call *inside the injected helper* would also
   print 501. An iOS-native (non-injected) helper avoids this blind
   spot entirely.

## 5. Confounders for the backend sender-control experiment

- **Endpoint reachability**: `com.apple.macosbooter.tccd.system` is the
  *mapped private* name registered by the chroot tccd into the real
  launchd — whether an iOS-native process can resolve it must be
  verified, not assumed (backend packet asserts "system-domain-registered
  and reachable" — mark as unverified prerequisite).
- **Schema provenance**: an iOS-side sender calling its *own* TCC SPI
  emits iOS-schema keys — byte-identical 7-key reproduction means
  hand-building the dict, which changes "authored by real framework"
  provenance. Either is fine, but label which.
- **Platform identity differs**: iOS-side sender token ≠ chroot token
  shape — that's the intended variable, but a differing reply could
  reflect platform handling, not responsibility registration alone.
- **Interpose asymmetry inside tccd is sender-independent** — the 501
  print will look identical for any sender; don't read it.

## 6. Corrections applied to my own docs (history retained)

- D049 doc+result: "helper still running" → **landed 15/15 on device**
  (helper-level only; not WS acceptance).
- D051 doc+result: "deny is cached/resolved locally" → narrowed:
  zero observed tccd lines is **not discriminating** (INFO-gated
  REQUEST log + unverified capture); arm-level deny / cached deny /
  unlogged delivery all consistent.

## 7. Bounded next-observation design (conditional on sender result)

If backend's attributed sender yields a **non-empty reply with a
verdict field** → attribution-layer bail was sufficient explanation for
EMPTY; next observation = whether WS's own request (once observer is
authorized) shows `impl_rc=0` + non-empty-verdict or the same bail —
that separates "WS hits the same wall" from "WS cached deny".

If **same EMPTY + same #103** → the private endpoint's
service/identity requirements sit below attribution; next producer =
the handler's dispatch path for unattributed iOS senders (no code
change needed — same experiment reads it).

If **non-empty with an error field** → next producer downstream of
attribution (service DB/session); the error string names it.

In all branches: `raw1`/`impl_rc=0` remains a collapsed class — no
result of this experiment proves or refutes explicit TCC policy.

## 8. Limits

- All tccd-side findings are static RE on `tccd_arm64e` (`42c388e8`,
  `9845D17F`) — runtime correlation for the authentic request rests on
  R002's saved transcript.
- Whether `responsibility_get_*` internals consult `audit_token_to_auid`
  is out of slice (they're libsyscall/kernel-adjacent); the #103 failure
  stands regardless.
- No device log exists to decode; decoder unchanged except the D051
  `GATE_DENY_*` honesty label (26 checks still ALL PASS).
