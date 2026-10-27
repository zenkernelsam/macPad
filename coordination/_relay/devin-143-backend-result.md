# D143 backend result — ring/init lifecycle repaired

Status: DONE. `analysis/d141-svctime-candidate/` only — no production, no
device, no sentinels.

## Repairs (all four review defects closed)

1. **Multi-consumer tail race** → single-consumer lease: first `d141_drain`
   caller claims by tid; foreign drains return `-1` + counted. MPMC no
   longer implied — enforced.
2. **Skip-past-uncommitted overwrite** → drain emits only the contiguous
   committed prefix and stops at the first open slot (`uncommitted++`);
   tail never passes a slot a producer may still write. Structural fix.
3. **Init publication order** → explicit `UNINIT→INITIALIZING→ARMED|
   DISARMED` word; sole initializer publishes final state with release;
   concurrent callers spin (bounded) to the *final* decision — no false
   disarm. `set_generation` serialized inside the UNINIT window.
4. **Tautological MPSC test** → real unique-msgid conservation (no dup,
   in-range, `emitted+dropped==attempted`, `drained==emitted`).

## New adversarial tests (deterministic barriers, no sleeps)

T9 paused-claim/no-skip-past · T10 foreign-drain refusal · T11 unique
msgid conservation · T12 wraparound-with-open-slot · T13 concurrent
gen-vs-gate · T14 8× concurrent gate_init all observe ARMED.

## Results

- ASan+UBSan: **129 checks, 0 failures**.
- TSan: **130 checks, 0 failures**, zero race reports.
- arm64: real `-arch arm64` object (compile-level only).
- API delta: `d141_drain` → `ssize_t` (carries -1 refusal); record format
  and all other signatures preserved.

## Documented bound

Producer death between claim and commit wedges reclamation — bounded
(ring stops at capacity, never corrupts). Stated in README.

Evidence: `docs/evidence/devin-d143-backend-20261019.md`
Candidate: **PENDING**

(handover to codex now)
