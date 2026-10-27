# devin-049-frontend — strict observer decoder + evidence matrix RESULT

**Doc:** `docs/evidence/devin-d049-observer-decoder-20261019.md`
**Artifacts:** `analysis/devin-d049-frontend/` — `obs_decode.py`
`ca076ef7`, `test_decode.py` `473af12a`, `abi_fixture.c` `59f4ee14`,
`host_fixture.bin` `7e8bbfbf` (256 real records, frozen host-test log).
**Device/backend/Git:** untouched, not contacted, not polled.

## Wording correction applied

D048 doc+result rescoped: "helper validated" → **host/local only**; the
backend's arm64e/PAC helper is still running, not accepted — nothing
speaks for on-device results. **[CORRECTED D051:]** helper result landed —
probe `f435057a` ran **15/15 PASS on device** (helper-level: real invoke
bodies + mock originals + real PAC fn-ptr chain). Still NOT real-WS
install acceptance.

## Decoder (strict, fail-closed)

- ABI from **compiled** sizeof/offsetof fixture (not hand-derived):
  144B LE, `magic=0x4d534f42 ver=2 sites1-3 flags⊆0x3f`
- Rejects: bad magic, wrong version, bad site, unknown flag bits,
  site/flag incoherence (ROOT-on-non-gate, LAZY-on-non-impl,
  AUDIT_PTR-without-flag), pointer VA-ceiling violations
- Interior bad frame → `CORRUPT_FRAME`+halt (not tail-only assumption);
  final <144B → `INCOMPLETE` with exact missing count
- seq integrity: DUP/OUT_OF_ORDER/GAPS → verdict `ANOMALOUS` exit≠0;
  BUDGET_EDGE = informational (drop budget may hide further calls)
- `--recover`: byte-scan resync + `SKIPPED_BYTES` spans + untrusted
  records/chains — never upgrades to clean
- **Zero-call guard printed on every run:** stored count ≠ call count;
  missing records never prove zero calls without producer counters

## Outcome matrix (3-site discrimination)

`GATE_ARM_A_NULL` / `GATE_ARM_B_UNDERSIZED` / `GATE_DENY_NO_DISP` /
`DISPATCHER_DENY(cache|bits|identity)` / `RESOLVER_FAIL(lazy=0)` /
`EVALUATED_NONALLOW collapsed{raw1|raw2}` — explicitly **not** a policy
verdict / `EVALUATED_ALLOW(raw0)` / `STANDALONE_INNER` /
`UNCHAINED_EMIT`. Chain shapes report missing sites as information.

## Tests — 21 checks ALL PASS

Every corruption class covered incl. interior torn (strict-fail +
marked-recovery) and the real 256-record fixture (`CLEAN` +
BUDGET_EDGE — live proof of the completeness caveat; chains
reconstructed exactly). Two decoder self-bugs caught+fixed by the
matrix (anomaly-escalation, trusted-only chains); one bound corrected
(VA-ceiling, documented limit).

## Status

Disposable tooling only — no production changes, no 4th hook, no
policy claim. If a backend device log lands: decode with explicit
scope (token bytes = identity material, decode fields not raw dumps).

---
(handover to codex now)
