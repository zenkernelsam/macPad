# D113 — bounded-memory diagnostic transport prototype + D112 verification

Standalone prototype under `analysis/d113-trace-transport/` (NOT
libmachook, NOT the frontend candidate; production integration neither
performed nor claimed). Files: `d113_transport.h/.c` (bounded MPMC ring
+ drain), `d113_adapter.c` (`d111_helper.h` impl), `d113_probe.c`
(interpose + per-thread lock context), `d113_concurrent_test.c`,
`README.md` (assumptions/limits). Run artifacts and binaries removed.

## Design vs task requirements

- **No fs syscall / no heap / no wait under lock**: `d113_record` is pure
  userspace — snprintf into a fixed 256×160 B ring (allocated once at
  `d113_init`), Vyukov bounded-MPMC publish, ≤`D113_PUB_TRIES`=3 CAS
  retries then drop+count. Proven by interposed syscall counting
  (C1/C3/C5: `io_in_lock=0`), not by code review alone.
- **Drop on full/contention, never block**: 4000 attempts vs CAP 256 →
  256 emitted + 3744 dropped, zero producer waits.
- **Identity**: in-band `g<gen> p<pid> t<pthread_self> s<seq>
  m<mach_absolute_time>` per record — pid cached at init, tid/mono are
  syscall-free reads.
- **Drain**: unlocked-only API; `d113_drain_open` = the D112 TOCTOU-safe
  pattern (`O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC` + `fstat` regular/uid/0600,
  flag cleared after). fd-bound writes; partial → dropped + same-line
  ` <DROPPED-RECORD>` marker so a short-write suffix never parses as a
  complete record.
- **O_NONBLOCK honesty**: documented as bounding `open()` only; regular-
  file write latency is unbounded — lock exclusion is the bound.
- **Producer model documented**: MPMC-safe for N producers via bounded
  CAS; single drainer assumed (accidental second consumer degrades to
  bounded contention, not corruption).
- **Shutdown limits**: `d113_shutdown` = memory-only refuse+count
  residue; close-under-lock deliberately leaks 1 fd + ~41 KB ring to
  process exit rather than issue a `close()` that can block (C5 caught
  exactly this — `d113_drain_close` under lock fired 1 fs op; the API
  contract now marks drain_* as unlocked-only and the adapter's in-lock
  close takes the leak path).

## Contract-suite results (`./run_contract.sh runs-*`)

Three-column matrix, all under the D112 harness (executable-defined
syscall interposition, per-case forked deadlines):

| Column | Result |
|--------|--------|
| `d111_reference.c` | 16/16 PASS (T0–T10, T12–T15, T11) |
| `d111_bad.c` | FAIL on T1, T3, T4(deadline), T5, T6, T7, T8, T12, T13, T14 — every injected sin detected |
| `d113_adapter.c + d113_transport.c` (prototype) | **16/16 PASS** — incl. T8 `0 I/O ops under lock` (emit = enqueue only), T14 post-open swap records landing fd-bound in `.orig` |

Prototype verbatim run identical to reference (all PASS lines; T5 `-2`,
T12 `-3` via fstat-on-opened-object).

## Concurrent tests (`./d113_test runs-cc`)

```text
C0   PASS    interpose live (3 ops)
C1   PASS    joined=1 io_in_lock=0                       <- 4x1000 records, held lock, zero fs ops
C2   PASS    attempts=4000 emitted=256 dropped=3744 file-records=256   <- conservation + exact drain
C3   PASS    producers_joined=1 drainer_blocked=1 io_in_lock=0 fill=65536  <- stall independence
C4   PASS    drainer_joined=1 total attempts=8000 emitted=256 dropped=7744 <- cumulative conservation
C5   PASS    io_in_lock=0 residue_dropped=5              <- shutdown under lock: no I/O, residue counted
C6   PASS    all phases completed inside deadline
```

- C3 detail: producers all joined while the drainer thread was blocked
  in `write()` on a full 65536-byte pipe — producer completion is
  provably independent of drain progress.
- C4: after `close(pipe_r)` the blocked write failed EPIPE (SIGPIPE
  ignored); every subsequent queued write failed and counted — attempts
  == emitted + dropped held exactly (8000 = 256 + 7744).

## Disclosed changes to the D112 control

- T10's `fsz==6` byte-exact check was reference-adapter-specific
  (verbatim writers). Framed transports legitimately embed identity
  metadata, so the case now asserts `nrecs==3 && em==3 && dr==0`
  (record-count contract). Reference still passes.
- `run_contract.sh` is now `[WORKDIR] SRC...` (multi-source adapters).
- Probe `g_in_lock` made `_Thread_local` in d113_probe.c — a concurrent
  drainer's legitimate unlocked writes must not count as in-lock.

## Honest gaps

- `d113_drain_fd` (raw-fd drain) exists solely for the stall test; the
  validated path is `d113_drain_open` + `d113_drain`.
- Interpose counts libc-boundary fs calls; a path deliberately using raw
  `syscall(2)` would evade — same documented limit as D112.
- "No fs syscall under lock" is proven at the libc boundary and by the
  visible record-path source; mach_absolute_time/pthread_self are
  documented syscall-free reads.
- Candidate production column remains **PENDING** everywhere.

(handover to codex now)
