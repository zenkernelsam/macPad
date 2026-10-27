# D191 frontend result — 2026-10-20

D169 v3 serialized ABI frozen for D190's loopback output.
Report: `docs/evidence/devin-d191-frontend-20261020.md`.

## Frozen facts

- **File = `sizeof(hdr)+n*64` exactly** — `d169_stream_hdr` is
  **120 bytes** (field-offset table in report); `d141_record` is
  **64 bytes** (offsets tabled). No marker, no padding hazards,
  verbatim struct dump is canonical.
- **Loader gate order**: HDR_TRUNC→MAGIC→VERSION(3)→CAP(256)→
  RESERVED(0)→UUID_FLAGS(0|1)→COUNT_BOUND(≤2²²)→TRUNCATED→
  TRAILING; uuid_valid normalized to bool on load.
- **Identity**: rec.pid==hdr.pid, single gen per stream,
  `rec_image_lo64` == `image_uuid[0:8]`, hdr.gen==rec.gen when
  nonzero; cross-stream UUID equality at decode.
- **ABI**: native little-endian raw structs; D190 must `#include`
  the committed `d141_svctime.h`+`d169_svcdecode.h` (relative
  include path must resolve) — private redefinition forfeits match.
- **D190 contents**: `n_records=2` — CALL(kind 8, msgid `0x74a7`,
  aux `0x00304003`) + RET(kind 9, msgid `0x750b`-or-0, aux=kr);
  hdr gen/pid/uuid internally consistent → `d169_stream_load`
  passes with zero synthetic input.
- Non-magic records skipped, not parse errors; corrupt gen/pid
  fails identity instead — noted.

Read-only; no edits/build/device/invocation; no commit.

(handover to codex now)
