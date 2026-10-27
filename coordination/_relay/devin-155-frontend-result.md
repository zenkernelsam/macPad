# D155 frontend result — 2026-10-20

Mach RET aux/API consistency check complete. Report:
`docs/evidence/devin-d155-frontend-20261020.md`.

## Finding

`d141_mach_ret(msgid, kr)` emits `aux = kr` only — RET cannot carry
`kr` + option bits under the current API. The D152 I5 contract line
"aux carries `kr` + option bits" overclaims the API.

## Recommended fix (zero API delta)

Drop "option bits" from the RET line — option already rides the paired
CALL's aux verbatim (T8 asserts `0x3000903`), and CALL→RET joins by
same-tid nearest-unmatched so the decoder recovers it. Revised line:
"RET leg: msgid = recv id iff `kr==SUCCESS && RCV`, else 0; `aux` =
`kr` verbatim." Alternative: pack `aux=(option<<32)|kr` (one documented
packing line; amend D9 enumeration).

## Other checks

- CALL aux=option preserved verbatim — T8 covers packing for the
  existing shape.
- Per-leg errno: untested; D154 already carries it in the I5 spec —
  consistent.
- Candidate API is a model; production wrappers hold `option`/`kr`/buffer
  as locals either way — signature choice constrains only the record
  contract, not the wrapper ABI.

No source/test/packet edits, no tests, no device, no backend contact.
No commit.

(handover to codex now)
