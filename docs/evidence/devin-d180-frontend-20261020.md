# D180 — acceptance plan: isolated D141 full-UUID retention/export

Scope: `analysis/d141-svctime-candidate/` only. Metadata-provenance
addition — records still carry only `gen`/`pid`; hi64 never reaches
a record and can never be record-verified. No drain consumer, v3
writer, hooks, sender, or transport in this milestone.

## 1. Minimal delta (3 pieces, per D179)

1. `static unsigned char g_uuid[16]` + `static _Atomic int
   g_uuid_valid`, stored inside `d141_set_generation`'s INITIALIZING
   window: `memcpy(g_uuid, uuid, 16)` BEFORE the pid/gen stores,
   then `g_pid`, `g_gen`, then `g_uuid_valid = 1` (release), then
   `g_state = UNINIT` (release — existing).
2. `int d141_get_uuid(unsigned char out[16])` — acquire-load
   `g_uuid_valid`: `1 → memcpy out, return 0`; `0 → zero out,
   return -1`. No syscalls, no locks, errno untouched.
3. `d141_reset_for_test` clears `g_uuid[16]` and `g_uuid_valid`.

## 2. Publication ordering — the design point

pid/gen are stored relaxed (:67-69); a reader must never infer uuid
validity from `gen != 0` — relaxed stores give no cross-variable
ordering, so a mid-window reader could see `gen` set while
`g_uuid` is half-written. The `g_uuid_valid` release/acquire flag is
the minimal correct gate: acquire on `1` synchronizes all prior
stores in the window. The flag sits inside INITIALIZING so the
existing single-winner CAS still serializes the triple (pid/gen/uuid)
as one publication.

## 3. Contract semantics to freeze

- Immutable: first `set_generation` wins; later calls return -1 and
  leave uuid untouched (existing post-set refusal unchanged).
- Validity domain: valid from successful set until `reset_for_test`.
- Empty/uninitialized: `get_uuid` → -1, `out` zeroed (never stale
  stack bytes).
- Disarmed gate path: does not alter uuid state (gate and
  generation are orthogonal by design).
- Consistency: `uuid_lo64(accessor bytes) == uuid_lo64(recovered
  from a drained record's gen/pid)` — the exact relation D175's
  decoder enforces, proven in-test.

## 4. Focused tests (barrier/hook-driven, zero sleeps)

| # | Test | Asserts |
|---|---|---|
| U1 | verbatim round-trip | `get_uuid` == 0, all 16 bytes incl. hi64 |
| U2 | before any set | `-1` + zeroed `out` |
| U3 | record binding | drained record's `(gen,pid)` recovers lo64 == `uuid_lo64(accessor)` |
| U4 | reset | `get_uuid` → -1 after `reset_for_test` |
| U5 | publication race (deterministic) | `d141_hook_gen_init` parks set inside INITIALIZING → concurrent `get_uuid` returns -1 (flag still 0); release → verbatim read. No torn/mid-window read is possible by construction |
| U6 | immutability | second `set_generation` → -1, uuid unchanged |
| U7 | errno | unchanged across `get_uuid` |
| U8 | regression | all 437 existing checks pass; new count reported |

## 5. Acceptance rows

- R1: U1–U8 pass under ASan+UBSan and TSan (counts reported).
- R2: U5 proves the flag gates readers (no scheduling luck — hook
  parks the writer).
- R3: README/contract line states: accessor uuid is a *provenance*
  field — hi64 is not record-verifiable; decoder treats it as
  producer-declared (D175 wording preserved).
- R4: no API change to emit/drain/ring; no new dependencies; the
  accessor is additive.

## 6. Explicitly out of scope

Drain consumer / v3 header writer wiring, product hooks, probe
sender, transport, device, sentinel behavior, and any frame-wait
conclusion. Commit only after reviewer acceptance of the milestone.
