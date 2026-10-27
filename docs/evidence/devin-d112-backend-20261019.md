# D112 — hardened acceptance framework: TOCTOU, probe integrity, negative control

D112 hardens the D111 contract against its three structural weaknesses.
All artifacts remain task-local under `analysis/d111-trace-contract/`
(`CONTRACT.md`, `d111_helper.h`, `d111_reference.c`, `d111_bad.c`,
`d111_contract_test.c`, `run_contract.sh`). No frontend files, no device,
no production marker paths.

## Hardening changes

**1. TOCTOU closed (was: lstat-then-open race).** Required pattern is now
`open(O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK, 0600)`
with `fstat(fd)` as the **sole validation authority**
(S_ISREG + st_uid==owner + mode 0600). `O_NOFOLLOW` kills the symlink race
at the open instant; `O_NONBLOCK` makes FIFO open fail `ENXIO` instead of
hanging. `lstat`/`stat`/`access` are documented as never-authoritative —
anything they approve can be replaced before open.

**2. O_NONBLOCK limit documented.** It bounds `open()` only; regular-file
`write()` latency is unbounded regardless of the flag (fs backpressure).
That is why the contract forbids I/O under lock context rather than
trusting a flag. Reference clears the flag via `fcntl` post-validation.

**3. Probe integrity — no self-reported passes.** The D111 model trusted
the adapter to call `d111_set_iocall_probe` per syscall — a silent
adapter passes T8 by reporting nothing. Now the harness **defines**
`open/openat/write/writev/pwrite/close/lstat/fstat/stat/access/read/
pread/unlink/rename/symlink/mkdir/fsync/fcntl/dup` in the executable;
adapter objects statically bind these definitions, so every filesystem
call is counted regardless of adapter cooperation. Real calls go through
`dlsym(RTLD_NEXT)` — with explicit `$INODE64` names for stat-family
symbols (platform maps SDK calls to `fstat$INODE64` etc.; the legacy
variants have a different `struct stat` layout — resolving the plain
name produced garbage `st_mode` and a false rejection before the fix).
Lock context is **harness-driven**: T8 wraps the emit call in
enter/exit (stand-in for caller-held `pace_lock`); the adapter may only
*query* `d111_lock_context_active()`. T0 asserts interpose liveness each
run (≥3 ops counted on a harness-local open/write/close).

**4. Deterministic race injection via injectable open hook.** The
interposed `open`/`lstat` carry three harness flags — `g_subst_path`
(substitute the open path), `g_swap_after_open` / `g_swap_after_lstat`
(rename→`.orig`, symlink target→path around validation), plus
`g_short_write` one-shot write clamp. No threads, no sleeps, no luck.

**5. Drop-marker reliability.** Marker is now
` <DROPPED-RECORD rec=%zu wr=%zd>` appended to the **same line** as the
partial record — a short-write suffix can never read as a complete
record (valid record = `\n`-terminated line without `DROPPED-RECORD`,
checked by `nrecs`). T7 asserts marker present AND valid-record count
equals the emitted counter.

**6. Per-case subprocess deadlines.** Every case runs in a forked child:
inner `alarm(4)` writes a FAIL verdict then `_exit(124)`; parent
`SIGKILL`s after 10 s and prints `deadline/no verdict` if the pipe is
empty. A blocking adapter can no longer stall the suite (D111's
global alarm killed the whole run mid-output).

## Results — reference adapter (`./run_contract.sh d111_reference.c`)

```text
T0   PASS    syscall interpose live (3 ops)
T1   PASS    errno after absent-sentinel check (errno=34)
T2   PASS    errno after successful emit (errno=35 rc=0)
T3   PASS    dir rejected rc=-1 errno=9 (want <0, 9)
T4   PASS    symlink rc=-1 fifo rc=-1 victim_size=0
T5   PASS    wrong-owner rejected rc=-2 errno=13
T6   PASS    short write: rc=-1 em=0 dr=1
T7   PASS    marker present=1 valid-records=0 em=0
T8   PASS    0 I/O ops under lock
T9   PASS    caller result+errno unchanged
T10  PASS    3 records written, counters 3/0 valid-records=3
T12  PASS    substituted 0644 file: rc=-3 subst_size=0
T13  PASS    substituted symlink: rc=-1 victim_size=0
T14  PASS    post-open swap: victim_size=0 orig=has-rec
T15  PASS    post-lstat swap: victim_size=0
T11  PASS    all cases completed inside deadline
```

Notable: T5 `rc=-2` and T12 `rc=-3` prove `fstat` validates the opened
object (uid, then mode) — not the requested path. T14's post-open swap
lands the record fd-bound in `t14.real.orig` while the victim symlink
stays empty.

## Results — negative adapter (`./run_contract.sh d111_bad.c`)

Deliberately omits errno restore, follows symlinks, does per-emit
open/write/close under the lock window, ignores write() results:

```text
T0   PASS    syscall interpose live (3 ops)
T1   FAIL    errno after absent-sentinel check (errno=2)      <- access() clobber
T2   PASS    errno after successful emit (errno=35 rc=0)      incidental
T3   FAIL    dir rejected rc=-1 errno=21 (want <0, 9)         <- open() clobber
T4   FAIL    inner deadline (4s)                            <- FIFO open blocked; bounded
T5   FAIL    wrong-owner rejected rc=0 errno=13              <- no uid check
T6   FAIL    short write: rc=0 em=1 dr=0                     <- write() result ignored
T7   FAIL    marker present=0 valid-records=0 em=1           <- no marker
T8   FAIL    3 I/O ops under lock                            <- interposed count, not self-report
T9   PASS    caller result+errno unchanged                   incidental
T10  PASS    3 records written, counters 3/0 valid-records=3 control
T12  FAIL    substituted 0644 file: rc=0 subst_size=0        <- no fstat
T13  FAIL    substituted symlink: rc=0 victim_size=0         <- symlink followed
T14  FAIL    post-open swap: victim_size=6 orig=missing-rec  <- per-emit reopen followed swap
T15  PASS    post-lstat swap: victim_size=0                  incidental (adapter never lstats)
T11  PASS    all cases completed inside deadline
```

Every injected sin produces at least one independent FAIL; the
incidental passes match the CONTRACT.md expectation table. T4's
`inner deadline (4s)` line is itself evidence: the bad adapter's FIFO
`open()` blocked, the child's alarm fired, the verdict was written, and
the remaining 11 cases still ran — subprocess containment works.

## Candidate column

Unchanged: **PENDING on all cases.** `./run_contract.sh <adapter.c>`
runs this identical suite against any `d111_helper.h` implementation.

(handover to codex now)
