# D201 — D141 drain → D169 v3 serializer: acceptance design (read-only)

Task: `coordination/_relay/devin-201-backend-task.md`. Design only —
no implementation, no edits to any accepted candidate
(`cd3bc6d`/`f33b5e2`/`b451c03` untouched), no device/product work.
This harness is **not** a WS observer install, performs **no** wait
measurement, and authorizes **no** device/product integration.

## Proposed component

`analysis/d202-drainser-candidate/` (name set at implementation;
compiled host-only: `d141_svctime.c` + `d169_svcdecode.c` + serializer
TU + `*_test.c` + `run_tests.sh` — same acceptance model as the three
committed candidates).

```
emit (producer, same thread or test-parked fixture)
   │  d141_emit / d141_mach_call+ret / d141_wrap_pass
   ▼  committed-prefix ring (CAP=256)
d141_drain(buf, CAP)  ← single consumer: lease by first caller
   │  bounded collect loop, drain_budget iterations
   ▼  d141_record collected[CAP]
d202_write_stream(path, collected, n, pid, uuid16, label,
                  emitted, dropped, uncommitted)
   │  exact file: sizeof(d169_stream_hdr) + n*64, no end marker
   ▼
d169_stream_load(path) → d169_decode(probe=that, ws=synthetic)
   round-trip proof
```

## Contract points

- **Bounded collection**: drain budget = small constant (e.g. 8) calls
  of `d141_drain(rec, CAP)`; each call itself is bounded (≤CAP slot
  probes). Loop exits on `head==tail` after drain, or when budget is
  exhausted — **never** spins waiting for a stalled claim. Collect
  buffer is exactly `D141_RING_CAP` (overflow beyond that was already
  producer-dropped — bounded by ring design).
- **Single-consumer lease**: first `d141_drain` caller owns the lease
  for process lifetime (D5); harness runs drains on one thread only;
  foreign-thread drain → `-1` + `drain_refused++` is covered by test.
- **Metadata**: harness calls `d141_set_generation(pid, uuid)` then
  `d141_gate_init` (documented order). Stream header fields:
  `uuid[16]`/`uuid_valid` from `d141_get_uuid` (0 → `uuid_valid=0`,
  suppress-by-design downstream); `pid`/`gen` = the same values the
  harness set (there is **no** pid accessor — the harness is the
  producer's peer, not a discoverer). Pre-write assertion: every
  collected record's `gen == (pid<<32)^uuid_lo64` and `pid` field ==
  set pid → mismatch refuses to write (never emits a stream the
  decoder must reject as `E_MULTIGEN`/`E_IMAGE_MISMATCH`).
- **Drop/uncommitted snapshots**: after final drain, header carries
  `emitted/dropped/uncommitted` verbatim from the counters;
  `n_records` = actually collected (which can be < emitted when a
  claim stalled mid-slot — honest, not synthesized).
- **Exact boundary**: file is `sizeof(hdr)+n*64` bytes — D169 enforces
  `E_TRUNCATED`/`E_TRAILING`; the writer mirrors `d190_write_stream`
  (fwrite header + records, fclose checked, errno preserved, partial
  write → −1).
- **Stalled producer**: `D141_TEST_HOOKS` parks an emit between claim
  and seq-commit (the t21 pattern). Drain stops at the open slot,
  `uncommitted++`; harness budget expires → stream still written with
  truthful `n_records`/`uncommitted>0` and the run flagged
  incomplete — no infinite wait, no silent padding.
- **Test-only quiescence**: `d141_reset_for_test` between scenarios
  only when all producer/consumer threads are parked — documented
  non-production, per D184.

## Enumerated tests (each deterministic, zero sleeps)

| s | proves |
|---|---|
| s1 | set→gate→K emits→drain→write→`d169_stream_load` accepts; n/kinds/ts/gen/pid verbatim; paired with synthetic WS stream → `d169_decode` scores |
| s2 | uuid_valid=1, uuid==accessor bytes, gen==(pid<<32)^lo64; a deliberately corrupted record gen makes the harness *refuse* to write (pre-write assertion) |
| s3 | >CAP emits → `dropped>0` verbatim in header; decode reconcile flag honest; no silent loss |
| s4 | parked mid-claim producer → partial drain, `uncommitted>0`, `n_records`=collected only, bounded retries exhausted, run flagged incomplete; release → second drain+stream collects remainder |
| s5 | write to unwritable path → −1 + errno; `/dev/full` short write → −1, never a false success |
| s6 | collect buffer < pending → bounded multi-drain gathers the whole committed prefix — no silent truncation |
| s7 | second-thread drain → −1 and `drain_refused++` (lease) |
| s8 | zero records → `sizeof(hdr)`-only file loads cleanly; decode scores nothing |
| s9 | `set_generation` never called → `d141_get_uuid` −1 → `uuid_valid=0` → loader accepts, decode suppresses scoring (`image_unverified`) |

## Explicitly out of scope

No production consumer thread, no drain-to-file transport, no WS hook
or permit registration, no `_current_message` read, no
`MACWS_MACH_MSG_ALL` interaction, no timing conclusion. The harness
proves only that D141-emitted records survive into a loader-valid
v3 stream with honest metadata — the D164/D166 join acceptance still
requires the separately gated observer milestone.
