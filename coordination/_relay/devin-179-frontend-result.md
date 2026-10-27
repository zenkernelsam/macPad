# D179 frontend result — 2026-10-20

Full-UUID export feasibility established.
Report: `docs/evidence/devin-d179-frontend-20261020.md`.

## Verdict: not retained, not exported

`d141_set_generation` receives `uuid[16]` but stores only
`uuid[0:8]` into `g_gen` (:66-69); `uuid[8:16]` is discarded at the
call site and unrecoverable. No `g_uuid` global, no accessor, no
serializer — the only export channel is per-record `.gen`/`.pid`.

## For D169 v3

`image_uuid[0:8]` is record-verifiable (lo64 check already enforced);
`image_uuid[8:16]` is producer-declared only — and remains a
producer claim even if retained (records never carry hi64); storing
it upgrades provenance to "same argument that minted gen", not
verifiability.

## Future integration milestone (3 steps, separate review)

1. `g_uuid[16]` stored under INITIALIZING in `set_generation`,
   cleared in `reset_for_test`.
2. Read-only accessor `d141_get_uuid(out[16])`.
3. Drain consumer fills `hdr.image_uuid` + `uuid_valid=1` from it.
Decoder side needs no change.

No edits/builds/device/integration. No commit.

(handover to codex now)
