# devin-048-frontend — frozen observer snapshot review RESULT

**Doc:** `docs/evidence/devin-d048-final-observer-snapshot-review-20261019.md`
**Device/backend/Git:** untouched, not contacted, not polled.

## Frozen snapshot (verified twice — matches backend's declared set exactly)

`h=a7c8438b` / `c=6641d9df` / `hooks=7321d92c` / `test=39d6791e` /
`Makefile=a7cc7862` / dylib `fddc64c1…` (arm64 `5B9F5C19…CD43D8C7C9CB`,
arm64e `353600B3…0DA92108B5` — source→object linkage verified).

## The .c delta: ONE functional fix — and it repairs a real bug I missed

`d43c65d9`→`6641d9df` changes exactly one thing in `gate_invoke`:
`cred = *env->audit_slot` → `cred = **env->audit_slot` (+ NULL-guard +
documenting comment). The reviewed code dereferenced the `void***` env
one level short: `cred` was the `__bss` CELL ADDRESS, not the container —
`csz` would have read the pointer's upper bytes as "size" (pass ≥0x34 by
coincidence) and `token` would memcpy adjacent `__bss` garbage. Frozen
code reaches the container correctly. **Self-flagged: my D047 checklist
never traced the indirection depth — recorded honestly.** The test
fixture was updated in lockstep (`slot→cell→container` two-level model).
All other .c bytes identical — the other five D046 verdicts carry over
WITH this diff evidence.

## Final host test on EXACT frozen sources: ALL PASS (45 checks)

fifo/device/0644 negatives now pass in final fixture order — my D047
3-FAIL run definitively closed as a test-ordering artifact (children
forking after parent's consumed once → inherited valid fd), independently
reproduced as non-bug via my own forked-FIFO binary
(`dropped=1 seq=1 exit=0`).

## Checklist — all verified on final source

errno envelope ✓ (unchanged) / disarm scalar scope ✓ (+new NULL-guard;
scalars-for-umbrella boundary documented) / private-log mode validation
✓ (fchmod-on-validated-fd) / FIFO non-blocking ✓ (repro + test) /
publication & chain correlation ✓ / bounded-write count-vs-latency ✓
(pass-count bounded, wall-clock disclaimed) / address+ABI+PAC pins ✓
(byte-verified + fail-closed assert) / default-off display isolation ✓ /
**no test macros in production** ✓ (Makefile clean, .o/dylib lack
override symbols, `write_all` static, zero test strings).

## Schema↔decoder reconciliation

144B fixed records, `magic 'MSOB'`/`version=2`/`site∈1..3` (emit
range-guards unknown sites at source). Decoder contract: frame on 144B,
reject magic/version mismatch, tolerate a truncated tail from a bounded
write failure. No decoder in-repo yet — consumer-side work, schema
supports it.

## Backend isolated-helper evidence

`devin-048-backend-result.md` + the d048 evidence doc **not yet saved** —
recorded absent, not polled. Coordinator keeps it helper-level only.

## Eligibility

- **Host/local level: fully validated on frozen hash** — everything
  executable-verified. (D049-scoped wording: "helper" here means
  host-built binaries only; the backend's actual-device arm64e/PAC
  helper is still running, not accepted.)
- **Real-WS level: structurally ready** for a coordinator-elected
  deployment review only.
- **Not claimed:** live observation done, real hooks firing, pixels,
  any policy verdict (raw1 = API default ≠ explicit denial — reaffirmed),
  fourth-site implementation (contingent design).

---
(handover to codex now)
