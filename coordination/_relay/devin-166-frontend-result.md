# D166 frontend result — 2026-10-20

Correlation uniqueness audited; amendment appended to D164.
Report: `docs/evidence/devin-d166-frontend-20261020.md`.

## Verdict: uniqueness not provable under concurrent identical IDs

- 64-byte record = `msgid`+`aux` only — `local_port` is absent; the
  D142 `(msgid−100, local_port)` key is half-unimplementable.
- `(msgid, tid, gen)` is a group key — 30 identical `0x74a7` probes
  share the tuple; collisions reachable via retries, pipelining, and
  concurrent senders (unseparable without port).

## Minimum sufficient rule

Serialize-one-probe: one `0x74a7` in flight per window → open span
unambiguous by construction. Queued fallback = ordinal pairing inside
seq-contiguous spans only (a gap may be either kind → voids the
span). Single-sender arm control substitutes for missing port.

## Measured vs inferred

Kind/msgid/tid/ts/order are verbatim-measured; the CALL↔HANDLER
association is ordinal inference — valid only under the two
constraints above.

D164 amended §3/§6 wording only; design scope preserved. No code,
tests, device, or integration. No commit.

(handover to codex now)
