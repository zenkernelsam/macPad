# devin-052-frontend — attribution independent review RESULT

**Doc:** `docs/evidence/devin-d052-attribution-independent-review-20261019.md`
**Binaries re-hashed this round:** `tccd_22F82_live` `42c388e8` (R002
fingerprint ✓), `TCC_22F82` `09b84137`.
**Device/backend/Git:** untouched, not contacted, not polled. D052
backend owns the live sender control — evidence reviewed as static
inputs only.

## Request verified prompt-free & read-only

Probe calls genuine `TCCAccessPreflightWithAuditToken` — 7-key schema
authored by TCC.framework. Options dict = `{TargetToken: &token,
Preflight: &err-block, SyncCallback: kCFBooleanTrue}`; daemon reads
`"preflight"` (`0x11f8c`) and the `"does not allow prompting; returning
preflight_unknown"` literal proves the check-only path. **FACT: no
prompt, no policy write.**

ABI settled: `x1` = pointer to 32B `audit_token_t` — SkyLight
`mov x1,sp` and probe `add x1,sp,#0x60` are the same mechanism
(arm64 lowers >16B by-value aggregates to stack-copy pointers).

## D050 anchors — all re-verified on `tccd_arm64e`

Sole `create_reply` `0x6508`; `function` gate `0x64e8`; strcmp dispatch
(pre-attribution `0x6ba0` + post-attribution `0x68a4`→`0x6fd8`);
attribution bail `0x69ac` → ERROR-gated log → **unconditional empty
send `0x69e0`**; alt sends `0x6744/0x746c/0x78f0`; sole `set_value`
`0x7968`; `responsibility_get_responsible_audit_token_for_audit_token`
imported+called `0x4fb48`; `TCCDAttributionChain` class real.

## Corrections (both directions)

- **`sender_auid=501` is forged**: REQUEST log calls interposed
  `_audit_token_to_auid` (`0x65b8` → `mac_hooks.m:13721` returns 501
  always). The 501-vs-(-1) "asymmetry" is forge-vs-raw, not session-vs-
  token. `sender_uid=0` is genuine (`euid` not interposed). Real
  attribution evidence = the `#103` itself (pid-keyed, interpose-free).
- **Zero tccd lines ≠ cached deny** (D051 wording narrowed): REQUEST
  log is INFO-gated; only missing ERROR would bound the bail path.
- **Attribution bail is flag-gated** (`0x6708`), not universal; EMPTY
  has ≥4 producers — EMPTY alone can't attribute to the bail without
  the correlated ERROR line.
- **`auid≥0` alone insufficient**: #103 keys on the connection token's
  pid/responsibility registration, not auid. Minimum distinguishing
  evidence: successful responsibility lookup on the connection token
  (absent ERROR + non-empty reply), corroborated by raw `task_info`
  `token.val[]` — a non-injected iOS helper avoids the interpose blind
  spot entirely.

## Backend experiment prerequisites (flagged confounders)

`com.apple.macosbooter.tccd.system` reachability from an iOS-native
process is **unverified** (mapped private name). iOS-side TCC SPI emits
iOS-schema keys — byte-identical 7-key reproduction means hand-built
dict (label provenance change). The forged 501 print is
sender-independent; don't read it.

## Conditional next observation

- non-empty reply+verdict → attribution bail explains EMPTY → observer
  decode separates "WS hits same wall" from "WS cached deny"
- same EMPTY+#103 → producer below attribution (endpoint identity reqs)
- non-empty+error → next producer named by the error field
- `raw1`/`impl_rc=0` remains collapsed — no branch proves policy verdict

(handover to codex now)
