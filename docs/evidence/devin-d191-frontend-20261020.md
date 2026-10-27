# D191 — frozen D169 v3 serialized ABI for D190 loopback output

Source: committed `analysis/d169-svcdecode-candidate/` (d169_svcdecode.h,
loader @d169_svcdecode.c:504-556, identity @:137-165) +
`d141_svctime.h:52-62`. All facts verified against the committed
headers — no private redefinition permitted.

## File layout (exact size equation, :527-529)

`file_size == sizeof(d169_stream_hdr) + n_records * 64`
— no end marker, no padding, no trailing bytes.

### d169_stream_hdr — 120 bytes (d169_svcdecode.h:61-76)

| off | field | type | requirement |
|---|---|---|---|
| 0   | magic    | u64 | `0x44535631` 'DSV1' → else E_MAGIC |
| 8   | version  | u64 | `3` → else E_VERSION |
| 16  | cap      | u64 | `256` (D141_RING_CAP) → else E_CAP |
| 24  | gen      | u64 | stream gen; if nonzero must equal every record's gen → else E_MULTIGEN |
| 32  | pid      | i32 | stream pid; if nonzero must equal every record's pid → else E_IMAGE_MISMATCH |
| 36  | flags    | u32 | unchecked |
| 40  | emitted  | u64 | committed snapshot |
| 48  | dropped  | u64 | drop snapshot |
| 56  | uncommitted | u64 | snapshot |
| 64  | n_records | u64 | ≤ 2^22 → else E_COUNT_BOUND |
| 72  | image_uuid[16] | u8 | full 128-bit UUID, verbatim |
| 88  | uuid_valid | u32 | **exactly 0 or 1** → else E_UUID_FLAGS |
| 92  | reserved | u32 | must be 0 → else E_RESERVED |
| 96  | label[24] | char | verbatim, unchecked |

### d141_record — 64 bytes (d141_svctime.h:52-62)

| off | field | off | field |
|---|---|---|---|
| 0 | magic u64 | 32 | pid i32 |
| 8 | seq u64 | 36 | kind u32 |
| 16 | ts_ns u64 | 40 | msgid u64 |
| 24 | tid u64 | 48 | aux u64 |
|   |            | 56 | gen u64 |

`magic` must be `0x53564331` 'SVC1'; non-magic records are silently
skipped by identity checks (not a parse error — a malformed record
that keeps the magic but corrupts gen/pid fails identity instead).

## Loader acceptance order (:511-529)

HDR_TRUNC → MAGIC → VERSION → CAP → RESERVED → UUID_FLAGS →
COUNT_BOUND → TRUNCATED → TRAILING → READ. `uuid_valid` normalized
to boolean on load (:545); every other field verbatim.

## Identity conditions (stream_identity :137-165)

- `r.pid == header.pid` (when pid≠0) per record — else E_IMAGE_MISMATCH
- all records share one `gen` — second gen = E_MULTIGEN
- `rec_image_lo64(r) = ((gen>>32 ^ pid)<<32) | gen_lo32` must be
  uniform across records AND equal `image_uuid[0:8]` (LE u64) when
  uuid_valid — else E_IMAGE_MISMATCH
- header `gen` ≠ 0 must equal record gen — else E_MULTIGEN
- decode adds: both streams' 128-bit UUIDs equal (:229); nonzero
  `image_uuid_set` filter requires both match (:236)
- empty stream (n=0): all record checks skipped; header UUID stands
  alone (`uuid_valid=0` → image_unverified → scoring suppressed)

## ABI assumptions (frozen)

- **Native little-endian** — raw struct read/write, no byte-swap
  anywhere in the loader. Same-endian host required both sides
  (Darwin arm64/x86_64 both LE).
- **Include the committed headers** — D190 MUST `#include`
  `d141_svctime.h` (record, D141_SVC_MAGIC, D141K_* kinds) and
  `d169_svcdecode.h` (d169_stream_hdr, constants). Any private
  redefinition forfeits the ABI match; the d169→d141 relative
  include (`../d141-svctime-candidate/`) must resolve.
- No padding hazards: every field is naturally aligned; 120 and 64
  are both 8-multiples — verbatim `fwrite`/`read` of the structs is
  the canonical serialize.

## D190 loopback stream — concrete contents

- `n_records == 2` (CALL + RET; G13 keeps RET even on failure).
- records[0]: kind `D141K_MACH_CALL`(8), msgid `0x74a7`,
  aux `0x00304003` verbatim, ts pre-call.
- records[1]: kind `D141K_MACH_RET`(9), msgid `0x750b` iff
  kr==SUCCESS else `0`, aux=kr verbatim.
- header: `gen = (pid<<32)^uuid_lo64`, `pid` = sender pid,
  `image_uuid` = declared 128-bit SkyLight UUID, `uuid_valid=1`,
  `reserved=0`, `cap=256`, `emitted=2`, label free.
- gen/pid/uuid-lo64 consistency is internally provable on host —
  the loopback file must pass `d169_stream_load` with zero
  synthetic inputs; decode-level checks need a WS stream (synthetic
  or future device).
