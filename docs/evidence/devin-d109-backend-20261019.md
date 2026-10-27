# D109 — independent review: frozen D107 pacing diagnostic patch + parser

Independent read-only review of `analysis/d107-pacing-candidate/`
(`d107-pacing-diagnostic.patch` 17 KB against mac_hooks.m `7321d92c…`;
`pace_trace_parse.py`; `test_pace_trace_parse.py` 15/15). No frontend
successor inspected; no source applied or rewritten; no runtime execution.
Line numbers are patch-file lines / parser-file lines.

## Findings

### F1 — errno is not preserved (behavior defect)

`macws_frame_pace_write` (patch 23–29) runs `open()`/`write()`/`close()`;
`macws_frame_pace_diagnostics_enabled` runs `access()` (patch 50). None
saves/restores `errno`. The **disarmed** path alone calls `access()` on a
nonexistent sentinel → `ENOENT` every ≥1 Hz evaluation; every armed emit's
syscalls overwrite errno as well. The swapend call-site emits (patch
316–326, 334–344) sit **between `result` computation and `return result`** —
a failed `access`/`open` replaces the error a caller would inspect when
`result` signals failure, or leaves a stale `ENOENT` after a successful
call whose errno the caller misreads as failure.
Counterexample: `ioctl` returns `result=-1` legitimately → emit's
`open()` fails `EACCES` → caller reads `errno==EACCES`, not the real
ioctl errno.
Fix shape (not applied): `int saved=errno; … errno=saved;` around each
diagnostic block.

### F2 — unbounded-blocking I/O inside `pace_lock` (bound defect)

`wake`/`exit` emits (patch 258–283, 286–294, 301–307) run **while holding
`pace_lock`** (`pthread_mutex_lock` at patch 221, unlock at 309). Each emit
is `open()+write()+close()` — `open()` on a FIFO or stalled filesystem
blocks **unboundedly**, serializing every other thread's pace wait behind
it. The 8192-line budget bounds record *count*, not syscall *time*.
Counterexample: `mkfifo /tmp/macws_frame_pace.trace` (world-writable
chroot `/tmp`) → `open(O_WRONLY)` blocks forever → the pace thread
never reaches `usleep`, swap deadlines missed — a live-lock introduced
purely by the diagnostic.
Mitigation shape: `O_NONBLOCK` (or single open outside lock, writes
buffered until unlock).

### F3 — silent-loss channel (measurement defect)

Every write failure is dropped: `if (fd < 0) return;` + `(void)write`
(patch 26–28), including the `trace-cap` marker itself (patch 82–91).
A trace that is armed but failing (`EACCES` on owner mismatch, `EISDIR`,
`EMFILE`, partial writes) produces **zero lines** — the parser then
reports "armed window, 0 records" indistinguishable from genuinely idle
waits. The cap marker exists to separate quiet-vs-exhausted, but no
marker separates quiet-vs-write-failure.

### F4 — missing `pid` on `wake`/`call`/`mode`/`trace-cap` (scope defect)

Only `sentinel-armed`/`enter`/`exit` carry `pid=`; `wake` (patch
259–265), `call` (322–325, 340–343), `mode` (172–180) and `trace-cap`
do not. The file path is shared across every hooked process; the seq
counter `g_macws_frame_pace_seq` is **per-process** → two instrumented
processes emit colliding seq spaces into one file. Parser then:
- joins `wake`/`call` to waits by `seq` alone (parser 128–139, 163–172)
  → cross-process mis-pairing reported as normal;
- on duplicate `enter` seq, the second process's record **overwrites**
  the first's `{pid,tid,enter_ts}` (parser 120–124) with only a
  `dup-seq` anomaly — first record silently lost;
- `cur_window['seqs']` mixes both pids' seqs (parser 125–126).

### F5 — `call` join key is thread-local seq; wrong-thread mislabeled (correlation defect)

`call` emits read `g_macws_frame_pace_last_seq` (TLS), zeroed on consume
(patch 112–119, 317–325, 335–343). A call arriving on a thread other than
the waiter's yields `seq=0` → parser classifies `'skipped-pre-lock'`
(parser 163–166) **even when `skipped=0`** — wrong-thread calls are
folded into the skipped class, hiding a real pairing failure.

### F6 — dropped-vs-capped attribution is global, not per-window (parser defect)

`cap_windows` aggregates `cap_hit` across **all** windows (parser
207–212): `if missing: if cap_windows: 'capped-dropped' += len(missing)`.
A single capped window re-labels *every* seq gap — including gaps in
uncapped windows — as cap-related. False attribution.
Correct shape: attribute each missing seq only to windows whose
`cap_hit` and seq range contain it.

### F7 — disabled-path perturbation is nonzero (measurement defect)

With the sentinel absent, each invocation still runs `clock_gettime` +
two atomic loads (patch 39–49) plus an `access()` at ≤1 Hz; inside the
wait, `pace_iters++` (patch 228) and `pace_reason` stores (248) execute
unconditionally in the hot loop. The original code has none of these.
The delta is small (µs-scale + one stat/sec) but real — "off" is not
identical to uninstrumented.

### F8 — enabled-path observer effect not self-accounted (measurement defect)

Emit I/O under `pace_lock` delays the **next** wait's `start_ns`; because
`target_ns` is anchored absolutely (`base + interval`), the injected
latency is absorbed into the *next* record's `slept_us` (shorter sleep
or deadline overshoot). The parser never reports
`exit_ts → next enter_ts` gaps, so the instrumentation cost is invisible
in the output — it lands inside the measured quantity by construction.

### F9 — trace file ownership/creation hazards (ownership defect)

`O_CREAT|O_APPEND` at `0600` in a world-writable chroot `/tmp`, no
`O_NOFOLLOW`/`O_EXCL`/`O_NONBLOCK`: (a) first writer owns the file — a
mobile-uid probe creating it before WS locks WS writes out (`EACCES`
→ F3 silent loss); (b) symlink redirect possible; (c) FIFO case feeds
F2. `access(F_OK)` on the sentinel is TOCTOU but consequence-free
(write simply fails).

### F10 — `service_tid_candidate` is histogram inference over possibly
mixed-pid records (correlation caveat)

Most-common tid across complete waits (parser 216–217). With F4's mixed
records the candidate can be a non-WS thread; flagged "inference" in the
output but no pid cross-check exists to detect the mix.

### F11 — dead/ignored anomaly accounting (parser cosmetic)

`anomalies['skipped-call'] += 0` inserts a permanent zero-valued key
(parser 165); `bad = sum(...)` is computed then ignored — `main` returns
nonzero **only** on `malformed` (parser 295–297), so a trace full of
orphans/dropped seqs still exits 0.

### F12 — `req_us` mislabeled (semantics)

`enter` records `interval_us` (the caller's request) as `req_us`; the
actual enforced interval is `effective_interval_us`, visible only via
`wake eff_us`/`exit target_ns`. A no-wake wait leaves the effective
pace unrecorded except through `target_ns - enter_ts` arithmetic —
the parser does not reconstruct it.

## What holds

Independent gating (`call`/`mode` emits are siblings of the umbrella
`if`, not nested — verified by brace structure); snapshot-gated
enter/exit pairing survives mid-wait disarm (`pace_diag` captured at
entry, patch 193); 0→1-edge `atomic_exchange` gives exactly one
`sentinel-armed` per arm window; TLS seq consumption pairs wait→call
correctly on the same thread; truncation guards (`bytes = min(n, size-1)`)
are correct on every emit path; `orphan`/`reordered`/`late_enter`
handling in the parser is directionally right.

## Net

Diagnostic-only, observation-shaped — no return-value or pacing-arithmetic
changes (patch intent preserved). Defects F1–F9 are concrete and
independent of each other; none requires runtime access to confirm —
they follow from the code alone.
