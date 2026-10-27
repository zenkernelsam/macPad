# devin-114 backend result — isolated transport lifecycle/framing repair

Task file: `coordination/_relay/devin-114-backend-task.md`
Evidence: `docs/evidence/devin-d114-backend-20261019.md`
Touched: `analysis/d113-trace-transport/` only. No frontend moving
files, no production integration, no device/source/deploy/Git.

## Outcome

All six requested repairs implemented and exercised by executable tests:

- **Lifecycle/quiescence**: `in_flight` + `drain_active` ownership
  counters; `d113_quiesced()` gates `d113_drain_final` (-2) and
  `d113_free` (-1). Shutdown is a flag only — no I/O, no wait, safe
  under the lock-context test. Free refused before shutdown and while
  producers run; allowed after quiesce.
- **Data race**: `gen` is atomic (fetch_add at `drain_open`, relaxed
  read in `record`).
- **Repeated open**: prior fd closed before reopen; C7 verifies by open-
  fd *count* (numbers get reused — the D113-era identity check would
  false-positive).
- **Timebase**: `m<…>` is mach ticks → ns via cached `mach_timebase_info`
  (128-bit), `tb_num==0` degenerate-fallback documented; `h<…>` declared
  opaque pthread handle, not `pthread_threadid_np`; join key `(g,p,h,s)`.
- **Framing**: `D113R`/`D113T` classes, `n=<hexlen>` escaped-length
  check, `\n`/`\r`/`\\` escaping. Strict parser rejects length-mismatched
  or merged lines even when every marker write fails (C8).
- **errno**: SAVE/REST brackets `snprintf`+publish; C11 asserts `EDOM`
  survives `d113_record`.

## Results

- Concurrent suite `C0`–`C12`: **all PASS** — 4000/8000-attempt
  conservation exact, `io_in_lock=0`, producers complete while drainer
  blocked on a full pipe, shutdown-under-lock residue drained post-
  quiesce, marker-failure/truncation/embedded-newline all detected by
  framing (not the marker).
- D112 suite rerun: reference 16/16 PASS, bad adapter fails all
  sin-covering cases, **prototype adapter 16/16 PASS** — controls
  preserved, no regression.

## Limits kept explicit

Bounded CAS = bounded work, not wait-free; single drainer; `n=` is a
length check, not a CRC; shutdown protocol is cooperative (producers
observe the flag at next `record`); `m` is process-local ns, joins
require matching timebase+pid+gen. Production candidate remains
**PENDING** — prototype validation only.

## Commands

```text
cd analysis/d113-trace-transport
clang -Wall -o d113_test d113_concurrent_test.c d113_probe.c d113_transport.c -lpthread
./d113_test runs-cc
cd ../d111-trace-contract
./run_contract.sh runs-ref   d111_reference.c
./run_contract.sh runs-bad   d111_bad.c
./run_contract.sh runs-proto ../d113-trace-transport/d113_adapter.c \
                             ../d113-trace-transport/d113_transport.c
```

One bug found by the tests during development: the C7 leak check via
`fcntl(fdA,F_GETFD)` false-positived because the second open reused the
closed fd number — switched to counting open fds (open_fd_count), which
is reuse-proof.

(handover to codex now)
