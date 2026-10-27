# D104 backend result — `_tccd` token semantics + callsite/runtime correlation

Offline only. Evidence: `docs/evidence/devin-d104-backend-20261019.md`;
artifacts `analysis/tcc-client/d104/` (disasm `1ccc6f60…`, raw bytes
`b5dcbdc7…`, service-table decode `3d4932f5…`).

## `_tccd(isSystemService, token*)` @ `0x1850c9584` — token arg decoded

The token pointer controls **endpoint routing only**:
`audit_token_to_auid` (fallback `audit_token_to_euid` when auid==-1, one-time
logged) → uid decides `com.apple.tccd` vs `com.apple.tccd.system` (flags 0x2
privileged on the system leg), and on the user leg applies
`xpc_connection_set_target_uid(conn, uid)`. The token never enters the message
and cannot alter kernel-attested sender identity. **No connection cache** —
fresh `xpc_connection_create_mach_service` per call; all 35 in-image callers
consume it locally into `_tccd_send_message`.

## Decisive callsite fact

`kTCCServicePostEvent` is in `_is_system_service`'s 7-entry table → the actual
SkyLight PostEvent checker calls `_tccd(1, &token)` → `cbnz w0` skips ALL token
logic → always `com.apple.tccd.system` + privileged flag. Wire message for
PostEvent: `{function:"TCCAccessRequest", service, preflight:true,
target_token:<32B sender token>, background_session:false,
require_purpose:null}`.

## Correlation verdict

Saved runtime establishes: non-null 52-byte cred container + sender token at
the gate (`#### CGXSYNTH … size=52 euid=0 pid=<probe>`), fresh-pid miss → impl
ran → checker called → w0=1 (R005). **But the token-to-getter leg is NOT
runtime-established**: pre-remap generations show
`_xpc_error_connection_invalid` at `tccd_send_message` (R006 — no daemon ever
received the request; the getter could not have run); post-remap (R007) shows
real dict replies — some tccd path ran, but which failure class produced the
absent/zero `result` is unproven (P0/P1/P2 and genuine-deny all consistent;
`result`-key presence itself unproven). The #103-getter hypothesis remains a
static potential route, not a runtime-attested producer; the R005-era 1002s
were produced by client-side transport failure.

## D103 erratum folded in

`len!=0x20` on a client-declared token is an input-shaping arm, not a failure
producer — accessing falls back to requesting and evaluation still runs; an
empty reply requires the subsequent attribution to fail.

Residuals R1–R3 in doc (NULL-token sync-fetch global, send-ladder internals,
per-message attribution in R007 captures).

(handover to codex now)
