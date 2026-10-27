# D179 — does D141 retain/export the full 16-byte UUID for D169 v3?

Exact-source audit. Answer: **no** — upper 8 bytes are discarded at
`set_generation` and nothing exports any UUID. v3's `image_uuid[16]`
is today a producer-declared field, only its low 64 bits are
record-verifiable.

## 1. What the candidate retains (verified at source)

- `d141_set_generation(pid, uuid[16])` (:55-72): receives all 16
  bytes, but stores only `lo = uuid[0:8]` into
  `g_gen = (pid<<32) ^ lo` (:66-69) and `g_pid = pid` (:67).
  `uuid[8:16]` is **read once, never stored** — unrecoverable after
  the call returns.
- Globals (:30-35): `g_gen`, `g_pid` only. No `g_uuid[16]` exists.
- Public API (:137-176): no accessor for `g_gen`, `g_pid`, or any
  uuid — the only export channel is per-record `.gen`/`.pid` on the
  ring (header :61 shows `gen` = the folded tag).
- `d141_drain` emits records only; **no serializer/stream writer
  exists in the candidate** — the v3 header is filled by the future
  drain consumer, not by D141.
- `d141_reset_for_test` clears gen/pid (:236) — a future `g_uuid`
  global must be cleared there too (test-only path).

## 2. Consequence for the v3 header

| Field | Verifiable on-record? | Provenance today |
|---|---|---|
| `gen` | yes (uniform per stream; checked in decoder) | producer echo |
| `pid` | yes (`rec.pid` ↔ `hdr.pid` enforced) | producer echo |
| `image_uuid[0:8]` | yes — `recovered lo64 == uuid_lo64(header)` enforced (:160) | record-derived check |
| `image_uuid[8:16]` | **no** — absent from every retained state | producer-declared only |

So D175's authoritative-vs-verified split is structurally forced:
the upper 64 bits can only ever be a *declaration* unless
`set_generation` retains the full uuid AND the drain consumer reads
it from the same globals — and even then it's a producer claim
(records never carry hi64); it merely upgrades provenance from
"independent declaration" to "same call argument that minted gen".

## 3. What a future integration milestone would add (not now)

1. `static unsigned char g_uuid[16]` stored under INITIALIZING in
   `set_generation` (same publication window as pid/gen — C3
   ordering already requires it); cleared in `reset_for_test`.
2. An accessor (e.g. `d141_get_uuid(unsigned char out[16])` —
   returns 0 iff a generation was set) — zero-cost, read-only.
3. Drain-consumer wiring: writes `hdr.image_uuid` from the accessor,
   `uuid_valid=1`.
4. Decoder side: unchanged — already treats header uuid as
   authoritative + lo64 consistency-checked (D175 :160-161).

API delta class: additive globals+getter inside the UNINIT window —
same review scope as prior candidate changes; **separately
reviewed milestone required** per acceptance terms.

## 4. Bottom line

D141 today exports nothing beyond per-record `gen`/`pid`; the v3
upper-UUID bytes are **declared, not retained**. The lo64
consistency check keeps the declaration honest against the record
stream; full-UUID provenance needs the 3-step addition above, gated
on its own review.
