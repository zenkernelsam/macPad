# D169 svcdecode — offline two-stream service-wait decoder (D175 repair)

**Status:** offline diagnostic candidate, repaired against the frozen
D170 checklist (G1–G12 + C1–C7), the D172 findings, and the D175
full-UUID/fail-closed-suppression requirements. Not accepted, not
committed. No sender, hooks, permits, device, or product integration.

## Model

Two independent record streams are decoded together:

- **probe stream** — client-side `MACH_CALL`/`MACH_RET` legs emitted by
  the instrumented probe process (own gen tag, own seq space, probe tid).
- **WS stream** — `PASS_ENTER/EXIT`, `DISPATCH_BEGIN`, `HANDLER_BEGIN`,
  `IMPL_INVOKE`, `REPLY_ENQUEUE`, `FLUSH_BEGIN` edges emitted by
  WindowServer (own gen tag, own seq space, services tid).

The streams merge by `ts_ns` with a deterministic stable tie-break:
`(ts_ns, stream_id[probe<ws], original index)`. Merged order is ts
order, not drain order — non-monotonic ts is legal input (C4).

## Image identity (D175 — full width, never low64)

Identity is the **authoritative 128-bit SkyLight Mach-O UUID** carried
in `d169_stream_hdr.image_uuid` + `uuid_valid` (format v3) and the
in-memory `d169_stream`. With `gen = (pid<<32) ^ uuid_lo64`, each
record's recovered low64 `((gen_hi32 ^ rec.pid) << 32) | gen_lo32` is a
**consistency check against the stream's declared UUID bytes only** —
never an identity claim. Rules:

- `rec.pid !=` stream `pid` (when declared) → `E_IMAGE_MISMATCH`;
- per-record recovered low64 diverges between records →
  `E_IMAGE_MISMATCH` (non-uniform `gen` → `E_MULTIGEN`);
- recovered low64 != the stream's declared `uuid[0..8]` →
  `E_IMAGE_MISMATCH` (uuid/gen disagreement);
- both streams `uuid_valid` but UUIDs differ → `E_IMAGE_MISMATCH`;
- `opts->image_uuid_set` requires BOTH streams to be `uuid_valid` and
  byte-equal to the filter → `E_IMAGE_MISMATCH`;
- either stream `uuid_valid == 0` (including empty streams relying on
  the header) → `image_unverified` → **scoring suppressed**; samples are
  never accepted on low64 alone.

## Fail-closed suppression (D175)

`scoring_suppressed` is set and `probes_scored` forced to 0 (would-be
samples retained in `suppressed_samples`, all other counters/classes
untouched) whenever ANY of:

- `image_unverified` — unknown/incomplete UUID on either stream;
- `stats_unknown` — seq-index allocation failure;
- `tids_capped` — tid table overflow (never score a truncated argmax);
- `pend_overflow` — pending-CALL table overflow (never let skipped
  CALLs shift RET association);
- `span_overflow` — span table overflow.

Merge/state allocation failure returns `D169_E_ALLOC` (non-OK,
diagnostics-free path is explicit).

## Stream file format (v3, exact-length — no trailer marker)

```
d169_stream_hdr { magic='DSV1', version=3, cap=256, gen, pid, flags,
                  emitted, dropped, uncommitted, n_records,
                  image_uuid[16], uuid_valid, reserved, label[24] }
followed by exactly n_records × 64-byte d141_record.
```

File size must equal `sizeof(hdr) + n*64` exactly — short = `TRUNCATED`,
long = `TRAILING`, plus `HDR_TRUNC/MAGIC/VERSION/CAP/COUNT_BOUND/READ`
classes. `reserved` must be 0 (`E_RESERVED`) and `uuid_valid` must be
exactly 0 or 1 (`E_UUID_FLAGS`) — invalid values are never normalized;
the in-memory view enforces the same domain. **No end marker; exact
length is the boundary.**

## Rules (D170 checklist mapping)

| Gate | Implementation |
|---|---|
| G1/G8 format | v3 exact-boundary parser, seven distinct malformed classes |
| G2 gen/image | per-stream gen/pid/low64 uniformity, full-UUID cross-check, `image_uuid` filter |
| G3 svc tid | argmax PASS_ENTER; tie→`AMBIGUOUS_TIE`, zero→`INSUFFICIENT`, cap→`tids_capped`+suppressed |
| G4/G5 spans | probe CALL opens, svc-tid HANDLER binds msgid, probe RET closes; second CALL poisons |
| G6 seq gaps | full-stream sorted-unique seq indexes; footprint `[min,max]` vs whole index (D172 F2) |
| G7 non-success RET | consumes nothing; delayed success pairs; EOF-open reported |
| G9 drops | verbatim counters; `gaps<dropped` reconcile only when stats known |
| G10 stats | median `HANDLER−CALL`, cluster `max(8ms,wait/2)`; suppressed → zero |
| G11 batch | bracket depth, msgid binding, foreign edges inert |
| G12 fixtures | t1–t22 cover the frozen list + D172/D175 additions |

## Verification

`bash run_tests.sh`:

- ASan+UBSan: **98 checks, 0 failures** (23 scenarios; t23 covers the
  >1024 `span_overflow` fail-closed path and exposed — then fixed — the
  span-creation dependency on the pending-CALL cap; t16 covers the
  `reserved`/`uuid_valid` header domain checks)
- TSan: **98 checks, 0 failures** — concurrency sanitizer on a
  single-threaded decoder; races/UB only, not semantics
- arm64 object: `Mach-O 64-bit object arm64` (compile-level only,
  x86_64 host)
