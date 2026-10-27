# devin-040 frontend result — restored ledger + input acceptance plan

Worker: macPad Devin D040 frontend 9589. Read-only; no device/backend
changes. Deliverable:
docs/evidence/devin-d040-restored-baseline-input-plan-20261019.md

## Part 1 — ledger consolidated + D039 corrections applied

Full restored-baseline ledger recorded: deployed source↔signed hash
pairs, live cache-extracted image identity (with extraction limit
stated), exact launch flags, static wallpaper identity
(`macws-forest-lake.png` sha256 `4931a89d…a1806` via workspacectl),
all runtime witnesses incl. PNG sha256, teardown/retention facts.

D039 wording fixes per coordinator, all applied in-place: "first since
Oct-12" → restoration vs 10-13 baseline (10-13 was visible);
saved-PNG residual → CLOSED (inspected + hash-verified); identity →
cache-extracted, not mapped-memory proof; causation → bundle-level
sufficiency, no per-permit isolation claimed; confounds →
enumerated-channel verification, not exhaustive guarantee.

## Part 2 — input plan: two-axis ladder, all existing capability

**Axis A (primary): exact AppInput** — chain complete + instrumented
end-to-end (Host `MacWSSendInputRecord` → inputd poll/recv/route →
`SendToAppInputBridge` per-PID socket → contract classify →
`windowWithWindowNumber:` → `sendEvent:` swizzle). Never witnessed
end-to-end. Ladder A0-A7 maps each hop to its existing witness; test
= control-panel Terminal launch (APP-CONTROL record exercises A1-A3
free) + `macws_inject_record.py keydown --target/--window` one char →
InputLab jsonl record or Terminal echo in next CARenderServer capture.
Tools all exist: injector, InputLab sink, capture, named failure modes
at every hop (REJECT field dump, contract reject class, endpoint
absence, swizzle absence, pixel-consequence split).

**Axis B (characterize, don't force): global SLSPostEventRecord** —
transport live, WS replies 1002 for every record class. Acceptance
requires raw reply dictionary hexdump (extend existing
`misc/macos_ws_event_probe.c`) + identification of the authoring check.
CGWarpMouseCursorPosition works = control channel baseline. 1002 with
identified author = valid characterized denial, not a fix prompt.
No TCC override / constant-return / spoof proposed.

Addendum — backend prep-note + gate interior RE-verified (capstone on
analysis/SkyLight_22F82): 1002 authored at `_XPostEventRecord`+0x388f94
(`cbz w0` of `CGXSenderCanSynthesizeEvents` → `mov w0,#0x3ea` →
`str w0,[reply+0x20]`); second class −304 exists for structural fails.
Gate interior symbol-resolved: container NULL → deny, size ≥0x34 →
deny, else `_WSAuditTokenHoldsCachedProcessRight` (cached-right +
TCC-fallback via PAC-signed `_audit_token_check_tcc_access`, cache
arm + async-request arm). R002 chain reached +0x3407ec → points 1-2
PASS and the real right/TCC verdict denies — 1002 is a GENUINE
authorization failure (no cached synthesis right + TCC PostEvent
denial), not structural dead-subsystem. Open: which right/service the
ctx encodes; whether real user-TCC grant path can populate the cache.
Both diagnostic targets' prologues verified verbatim-safe +
permit-installable. Keyboard route confirmed: keyRecord+targetPID>1 →
exact AppInput endpoint, bypasses VNC proxy AND the SkyLight gate.

**Likely next step**: bounded input A/B generation (visible desktop +
Terminal + one exact key record, read A1-A7). Ranked theories: first
blocker most likely endpoint-absence/contract-reject (A3/A4);
POLL-TIMEOUT would mean Host never emitted (synthetic → use injector,
not touches); B's 1002 most likely sender-gate-authored pending reply
dump.

Stopping after handover per packet — no next task claimed.

(handover to codex now)
