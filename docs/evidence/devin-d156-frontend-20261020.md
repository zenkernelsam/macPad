# D156 — D152 RET aux contract corrected per D155 (spec-only)

Scope: `coordination/_relay/devin-152-backend-task.md` edits only. No
candidate source/tests touched, no tests run, no device, no backend
contact.

## Changes applied (zero-API-delta route, per D155 recommendation (a))

1. **I5 CALL leg** — added "`aux` = `option` verbatim" (the leg clause
   previously specified only `msgid`; aux was unstated).
2. **I5 RET leg** — replaced "`aux` carries `kr` + option bits" (an
   overclaim vs `d141_mach_ret(msgid, kr)`) with "`aux` = `kr`
   verbatim"; option bits remain verbatim on the paired CALL aux and
   are recovered via the same-tid nearest-unmatched pairing rule.
   Explicit: no packing, signature unchanged, zero API delta.
3. **I5 test spec** — added aux-preservation assertion (CALL `aux ==
   option` AND RET `aux == kr` verbatim) alongside the existing
   distinct-id join, timeout exclusion, async-interleave, and per-leg
   errno requirements.
4. **R5 row** — updated to name the corrected contract and require
   aux-preservation tests.
5. **Revision map** — added at top of packet noting the D154 (D153)
   and D156 (D155) corrections.

## D9 check

D9's emit-path enumeration requires no change — the correction removes
a packing requirement rather than adding one; "fixed 64-byte structure
copy" remains the only record-write step.

## Consistency verified against frozen source

`d141_mach_call(msgid, option)` → `emit(CALL, msgid, option)`;
`d141_mach_ret(msgid, kr)` → `emit(RET, msgid, kr)` (:154-159). The
corrected contract now matches the shipped API exactly.
