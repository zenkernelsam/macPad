# D113 backend result — bounded-memory diagnostic transport prototype

Doc: `docs/evidence/devin-d113-backend-20261019.md`. Files (task-local,
`analysis/d113-trace-transport/`): `d113_transport.h/.c`, `d113_adapter.c`,
`d113_probe.c`, `d113_concurrent_test.c`, `README.md`. D112 harness
updated: `run_contract.sh` is now `[WORKDIR] SRC...`; T10 uses the
record-count contract (framed transports embed identity metadata).

## Delivered

- **Bounded MPMC ring** (256×160 B, allocated once at init): `d113_record`
  does zero filesystem syscalls, zero heap, never waits — bounded CAS
  publish (≤3 tries) then drop+count. In-band identity per record:
  `g<gen> p<pid> t<tid> s<seq> m<mono> | payload`.
- **Drain** unlocked-only: D112 TOCTOU-safe open (O_NOFOLLOW|O_NONBLOCK|
  O_CLOEXEC + fstat regular/uid/0600), fd-bound writes, partial → dropped
  + same-line ` <DROPPED-RECORD>` marker.
- **O_NONBLOCK limit documented** — bounds open() only, never write
  latency; lock exclusion is the bound.
- **Shutdown**: `d113_shutdown` memory-only residue→dropped; close under
  lock deliberately leaks 1 fd + ring to exit rather than issue a
  `close()` that can block (caught by C5 during development and fixed
  into a documented limit).

## Verification (verbatim)

D112 contract suite — three columns:

- reference: 16/16 PASS
- bad adapter: FAIL on T1,T3,T4(deadline),T5,T6,T7,T8,T12,T13,T14
- **prototype adapter: 16/16 PASS** (incl. T8 `0 I/O ops under lock`,
  T14 post-open swap fd-bound)

Concurrent test `C0–C6` all PASS, bounded by alarm(60):

```text
C1   PASS    joined=1 io_in_lock=0                        (4x1000 records under held lock)
C2   PASS    attempts=4000 emitted=256 dropped=3744 file-records=256
C3   PASS    producers_joined=1 drainer_blocked=1 io_in_lock=0 fill=65536
C4   PASS    drainer_joined=1 attempts=8000 emitted=256 dropped=7744
C5   PASS    io_in_lock=0 residue_dropped=5
```

C3 proves producers complete while the drainer is blocked inside
`write()` on a full pipe — completion independent of drain. C2/C4 prove
exact conservation `attempts == emitted + dropped`.

## Honesty state

- Prototype passes every executable check — **but it is a prototype**;
  production integration not performed or claimed.
- Candidate production column: **PENDING** everywhere.
- `d113_drain_fd` raw-fd path is a stall-test fixture only.
- Interpose covers libc-boundary fs calls (raw `syscall(2)` evasion is
  the documented limit, same as D112).
- No frontend files, device, production markers, source/deploy/Git ops.

(handover to codex now)
