# D103 backend result — TCC client request token/key construction

Offline exact-binary audit of `analysis/TCC_22F82` (UUID
`1238D7CA-719A-36A4-94B1-2F387E6B3BF1`, sha256 `09b84137…`). No device, no
probes, no setters, no token changes, no builds/source/Git writes.
Evidence: `docs/evidence/devin-d103-backend-20261019.md`; raw artifacts +
key-table decodes under `analysis/tcc-client/d103/` (7 files, hashes in doc).

## What the exact disassembly proved

**`TCCAccessPreflightWithAuditToken` path** (the PostEvent preflight leg):
options dict `{kTCCAccessRequestOptionTargetToken: <raw audit_token*>,
Preflight: <result-mapper block>, SyncCallback: kCFBooleanTrue}` (value CBs
NULL → pointer passes verbatim) → `TCCAccessRequest` captures it at emit-block
`+0x30` → wire dict (6 keys: `function="TCCAccessRequest"`, `service`,
`preflight`, **`target_token`**, `background_session`, `require_purpose`) where
**`target_token = xpc_data_create(caller's audit_token ptr, 0x20)`**
(`0x1850cc930`) or `xpc_null` if the option was absent.

**`TCCAccessRequestIndirect` path**: 15-key dict from static key table
`0x1d3dbde80` — **`indirect_object_token = xpc_data_create(blk+0x78, 0x20)`**
(`0x1850ce09c`), copied verbatim (`ldp q1,q2 [x22]` @ `0x1850cda14`) from the
function's **x2 arg** — the indirect-object identity, distinct from the
requesting subject.

**Credential-dict path**: `_tcc_credential_encode_to_dictionary` (`0x1850d81f0`)
emits `AUTHENTICATOR_AUDIT_TOKEN_KEY` via `xpc_dictionary_set_data(…,0x20)`
(`0x1850d824c`); attached under `SUBJECT_CREDENTIAL_DICTIONARY_KEY` /
`INDIRECT_OBJECT_CREDENTIAL_DICTIONARY_KEY` in the `tcc_authorization_*`
request shape.

**Setter**: `{function, pid, responsible_pid}` — **PIDs only, no token**;
attribution rests on tccd's entitlement/system-server gate + kernel checks.

**Receiver mapping (tccd `initWithMessage:`, D102 artifact)**: probe order is
`target_token` → `indirect_object_token` → `SUBJECT_CREDENTIAL_DICT`
(AUTHENTICATOR_TYPE==1 → AUDIT_TOKEN) → each gated `len==0x20` →
`_accessingProcess`; else `_accessingProcess` nil → accessor falls back to
`_requestingProcess` (kernel-attested message audit token). Malformed-length
token → logged, degrades to fallback (not init failure).

## Scoped caveats carried forward

- Static construction proves code shape, not which arm a given PostEvent call
  ran — wire bytes need saved runtime correlation.
- "No pidversion/asid comparison observed in tccd" ≠ "dependencies don't
  validate them" (proc_pidpath_audittoken/csops/Security unresolved).
- Empty reply stays ambiguous across P0/P1/P2 + the len!=0x20 arm.
- Residuals R1–R4 listed in doc (NULL-token sync-fetch leg, 4th CheckAuditToken
  key, external Indirect/credential producers, `_tccd` token-arg semantics).

(handover to codex now)
