# D114 — transport lifecycle / race / timebase / framing repair + tests

Task: `coordination/_relay/devin-114-backend-task.md`. Scope: isolated
fixes inside `analysis/d113-trace-transport/` only. No production
integration, no frontend moving files, no device. The prototype remains
an unaccepted candidate for production.

## Defects addressed (task list → fix)

| # | Task finding | Fix |
|---|---|---|
| 1 | Shutdown flag alone let an already-entered producer publish after drain | `in_flight`/`drain_active` atomic ownership; `d113_quiesced()` = `shutdown && in_flight==0 && drain_active==0`; `d113_drain_final` and `d113_free` refuse (-2/-1) unless quiesced |
| 2 | `gen` plain-int write at `drain_open` raced `record` reads | `gen` is now `_Atomic uint32_t`, incremented at `drain_open`, relaxed-read in `record` |
| 3 | Repeated `drain_open` leaked the prior fd | `drain_open` closes `t->fd` before opening (C7 counts open fds, not numbers — fd reuse makes identity checks a false positive) |
| 4 | `m<…>` field was raw `mach_absolute_time` ticks mislabeled as time | Converted via cached `mach_timebase_info` (128-bit multiply) → true ns; `tb_num==0` is the documented degenerate fallback |
| 5 | `pthread_self` printed as `t<…>` could read as a thread ID | Field renamed `h<…>`, documented as the opaque `pthread_t` handle — NOT `pthread_threadid_np`, not a kernel tid; join key = `(g,p,h,s)` |
| 6 | Forced-newline truncation produced a false valid record | Two record classes: `D113R` (complete) / `D113T` (truncated payload, `truncated++`); a reader distinguishes them |
| 7 | Partial write + failed marker could fake a complete record | Length-checked framing: `n=<hexlen>` escaped-payload count; strict parser rejects any line whose payload length disagrees, even if every marker write fails (C8) |
| 8 | `snprintf` could clobber caller errno in `record()` | `errno` SAVE/REST brackets the whole formatting+publish body; C11 asserts `EDOM` survives a live `d113_record` |
| 9 | Embedded `\n` in payload created phantom record lines | Escaping `\n`→`\\n`, `\r`→`\\r`, `\\`→`\\\\`; emitted line can never contain a raw newline (C10); re-escape on shrink never splits a pair |
| 10 | `d113_free` had no lifecycle guard | Refuses (-1) unless quiesced — before shutdown, while a producer is in-flight, or while a drainer is active |

## Files changed (all under `analysis/d113-trace-transport/`)

- `d113_transport.h` — rewritten: ownership/lifecycle contract, atomic
  `gen`, `in_flight`/`drain_active`, `tb_num/tb_den`, frame spec,
  explicit non-claims (no wall-time wait-freedom, no 2-drainer support).
- `d113_transport.c` — rewritten: `esc()`, `mono_ns()`, in-flight
  bracketing, R/T classes, `n=` length field, quiesced/final/free.
- `d113_adapter.c` — close order is now `drain → shutdown → drain_final
  → drain_close → free`; under-lock path still shutdown-only (deliberate
  1-fd + ~45 KB leak to exit, documented).
- `d113_probe.c` — `d113_probe_short_always(on,n)` persistent write
  clamp for the marker-failure case.
- `d113_concurrent_test.c` — rewritten: strict frame parser +
  C5b/C6–C11 added.
- `README.md` — updated contracts.

## Exact commands run

```text
cd analysis/d113-trace-transport
clang -Wall -o d113_test d113_concurrent_test.c d113_probe.c d113_transport.c -lpthread
./d113_test runs-cc
cd ../d111-trace-contract
./run_contract.sh runs-ref  d111_reference.c
./run_contract.sh runs-bad  d111_bad.c
./run_contract.sh runs-proto ../d113-trace-transport/d113_adapter.c \
                             ../d113-trace-transport/d113_transport.c
```

## Results

Concurrent/lifecycle/framing suite — all PASS:

```text
C0   PASS  interpose live (3 ops)
C1   PASS  joined=1 io_in_lock=0                        # 4 producers, held lock
C2   PASS  attempts=4000 emitted=256 dropped=3744 valid=256 corrupt=0
C3   PASS  producers_joined=1 drainer_blocked=1 io_in_lock=0 fill=65536
C4   PASS  drainer_joined=1 attempts=8000 emitted=256 dropped=7744
C5   PASS  io_in_lock=0 free_before_shutdown=-1 final=5 # residue→fd
C5b  PASS  free after quiesce
C6   PASS  free_mid_rc=-1 joined=1 quiesced=1 final=256 free_rc=0
C7   PASS  fds before=5 after_open1=6 after_open2=6     # no leak (fd reuse)
C8   PASS  all-writes-short: valid=0 corrupt=1 emitted=0 dropped=4
C9   PASS  payload=399 valid=0 trunc_lines=1 corrupt=0 tr=1
C10  PASS  embedded-nl: valid=1 corrupt=0 emitted=1
C11  PASS  record() errno=33 preserved
C12  PASS  all phases completed inside deadline
```

D112 contract matrix after D114 changes:

- Reference adapter: 16/16 PASS (unchanged — control preserved).
- Bad adapter: all sin-covering cases still FAIL as designed.
- Prototype adapter (D114 transport): **16/16 PASS**, zero FAILs.

## Design notes / honest limits

- The shutdown protocol is **cooperative**: producers must call
  `d113_record` to observe the flag. `d113_shutdown` never waits — a
  producer sleeping *between* record calls is visible to nobody until it
  next records; quiescence covers in-flight records only.
- `in_flight` protects the publish window; a producer preempted inside
  `record` keeps `in_flight>0` and `d113_free` refuses — no UAF, and no
  lock-held waiting on it either.
- C6 exercises shutdown-while-recording; `free_mid_rc=-1` proves the
  lifecycle check refuses before shutdown deterministically (the
  in_flight>0 path is timing-dependent, so the deterministic refuse arm
  is what's asserted).
- C8 clamps **every** write to 3 bytes — record and marker both partial;
  the strict parser reports 0 valid / 1 corrupt, counters `em=0 dr=4`.
  Framing, not the marker, carries the detection.
- Frame check is `n=` length + `D113R/T` prefix — sufficient for the
  stated threats; it is not a CRC. Bit-flips inside a payload that
  preserve length remain undetectable (out of scope; noted).
- `m<…>` is process-local CLOCK_MONOTONIC-equivalent ns via
  `mach_timebase_info`; cross-process/cross-boot joins must match
  `tb_num/tb_den` AND pid/generation — no claim of a global clock.
- Bounded CAS = bounded work attempts, not wall-time wait-freedom;
  single-drainer only. These non-claims are written into the header.
- The candidate production-integration column remains **PENDING** —
  D112/D114 validate the isolated prototype only.
