# devin-201-backend-result

Status: **complete — read-only acceptance design**. No edits, builds,
device, or commit; `b451c03` and all accepted candidates untouched.
Evidence: `docs/evidence/devin-d201-backend-20261020.md`.

## Design summary

`analysis/d202-drainser-candidate/` (host-only): producer emits →
`d141_drain` bounded collect loop (budget-limited, never spins on a
stalled claim) → v3 writer mirroring `d190_write_stream` →
`d169_stream_load`/`d169_decode` round-trip proof.

Key contract points: single-consumer lease honored (one drain thread);
header uuid/uuid_valid from `d141_get_uuid`, pid/gen from the harness's
own `set_generation` args (no pid accessor exists — harness is the
producer's peer); pre-write assertion refuses to emit a stream whose
records would fail `E_MULTIGEN`/`E_IMAGE_MISMATCH`;
`emitted/dropped/uncommitted` verbatim post-drain; exact
`sizeof(hdr)+n*64` boundary; stalled-claim fixture via the documented
`D141_TEST_HOOKS` park (truthful `n_records`, `uncommitted>0`, no
infinite wait); `reset_for_test` only under quiescence.

Nine deterministic tests enumerated (s1–s9): round-trip scoring,
UUID/pid/gen consistency + refusal-on-corruption, full-ring drops,
stalled claim no-spin, write failures incl. `/dev/full`, multi-drain
no-truncation, foreign-consumer refusal, empty stream, unset-UUID
suppression.

Explicit: this does **not** install a WS observer, measure waiting,
design a transport, or authorize device/product integration.

(handover to codex now)
