# D044 — observer concurrency, correlation and forwarding fixes

Date 2026-10-19, packet `coordination/_relay/devin-044-backend-task.md`.
**Local source/build/tests only — no device, deploy, restart, commit,
package or install.** Fixes the D043-review blockers plus the frontend
final review's offset bugs. Record-only diagnostic scaffold; no return
value/cache/audit/desc/TCC/identity/policy modification.

## 1. Blockers fixed

### Corrected global offsets (frontend review, verified by subtraction)

| symbol | va | D043 draft (WRONG) | corrected |
|---|---|---|---|
| `gCurMsgAudit` slot | `0x1d8ba61b8` | `0x5AC11B8` | `0x53AC11B8` |
| `rightDesc` | `0x1d3dcb6c8` | `0x542E66C8` | `0x4ECE66C8` |
| lazyChecker cell | `0x1d8ba6b50` | `0x5AC1B50` | `0x53AC1B50` |

The draft constants dropped a nibble; rightDesc would have read ~90MB
high (garbage desc or WS fault on first record). The pre-existing
`gCurMsgAudit` computation in mac_hooks.m was never affected (literal
subtraction at compile time) — only the new `#define`s were wrong.
Header now carries corrected values + the arithmetic in the comment;
the host test asserts all three.

### Concurrent fd open / leak / shared-log race

Was: `atomic fd=-2; load; open; store` — two threads could both open
(leak) and publishes could interleave mid-write. Now: `pthread_once`
single open attempt for process life — `O_NOFOLLOW | O_CLOEXEC`,
mode `0600`, then `fstat`-validated `S_ISREG && st_uid==geteuid()`.
Failure latches `-1` permanently and counts drops — bounded, no
unbounded retry, exactly one open attempt ever (no leak).

### Ring publication before memcpy completes

Was: `seq` fetch_add exposed the slot index before the record was
written. Now: one emit mutex serializes fill+publish+log; per-slot
`_Atomic` publish flag stored release-after-memcpy; the ring accessor
counts only published flags, so readers can't observe a half-written
record. `seq` remains a monotone emit counter for accounting.

### Shared nested-invocation correlation

Was: per-record `seq+pthread_self` — not an invocation identity. Now:
TLS `{depth, invocation_id}` — outermost observed entry assigns a
fresh global id flagged `CHAIN_ROOT` (gate) or `STANDALONE` (a
dispatcher/impl call with no observed chain); nested entries inherit
the id with no flag; unwinding to depth 0 clears the id so a later
call can't pair to a stale gate. Records carry `invocation` + flags;
a gate→disp→impl chain on one thread shares one id even when
interleaved with another thread's chain.

### errno / forwarding envelope

Every wrapper: `chain_enter` → (safe-lifetime pre-orig captures: gate
container bytes, arg snapshots; plain memory reads only — no
errno-touching calls) → `orig` → `saved_errno=errno` → post reads
(lazy cell) → emit (which saves/restores internally) → `chain_exit` →
`errno=saved_errno` → `return orig's r`. Original results returned
verbatim; errno at return == orig's errno, covering the ENTIRE wrapper
span including pre/post inspection.

### Bounded I/O + private validated log

`write_all`: ≤8 passes, EINTR retries within budget, short writes
continue the remainder, w==0/real error → fail (-1) counted as dropped
— never spins. Log: `O_NOFOLLOW|O_CLOEXEC|O_APPEND`, mode 0600, fstat
regular+owner validated. Symlinked or wrong-owner path → refuse once,
latch, count drops.

### Verified-readable lifetimes

Gate record now captures container bytes (size@+4, token@+0x14, 32B)
BEFORE the original — the same window the gate dereferences at its own
entry — instead of re-reading a possibly-recycled container after.
Dispatcher/impl arg bytes are read inside the wrapper frame (the only
lifetime in which those stack copies are valid).

## 2. Frontend review notes incorporated

- TCC_22F82 preflight decode: `0=allowed, 1=denied, 2=unknown` → the
  D041 probe's self-token `preflight=1` = **denied** (earlier
  "1=allowed" reading inverted). Consistent with the impl callsite's
  `cset eq` (pass iff 0). Documented as the corrected polarity.
- rightDesc ABI accepted as decoded: `[0]=service ptr, [8]=deny-mask,
  [0x10]=allow-mask, [0x18]=decided-mask, [0x20]=pid==0 flag`.
- Service arg recorded by pointer identity only — the
  "kTCCServicePostEvent CFStringRef" assertion dropped.
- Raw checker value (denied/unknown/transport) is NOT capturable at
  SkyLight boundaries — boundary residual documented, no guess.

## 3. Tests — ALL PASS (35 checks, x86_64 host)

`cc -DMACWS_SENDER_OBS_TEST` build of the core + test:

- default-off: nothing stored/logged, calls+drops counted, errno kept
- forked child: symlinked log refused (O_NOFOLLOW), record still
  stored in ring, drop counted; parent's once-state pristine
- armed: 144B records verbatim in ring; log created mode `0600`
  regular file holding exactly the written records
- nested chains on 2 threads: gate flagged CHAIN_ROOT, all three
  records share one invocation id, ids distinct across chains
- standalone inner call: own fresh id + STANDALONE flag; id cleared
  after unwind (no stale pairing)
- 8-thread simultaneous first emission: all published, unique seqs,
  intact records — single-fd/ordered by the mutex+once contract
- `write_all`: full nonblocking pipe → bounded -1 (no spin);
  zero-length trivially ok
- budget: 300+ emits → 256 published cap, drops counted
- live disarm: sentinel removed → dropped, calls continue
- pins: all three prologue quads byte-equal the image; corrupted pin
  provably mismatches; all three corrected global offsets asserted

## 4. Build artifacts (local only)

`libmachook/.theos/obj/libmachook.dylib` — sha256 `2846fd60…` (prefix),
arm64 uuid `D0CECC39-C7B1-3A79-86B6-652CB331050C`, arm64e uuid
`74635F48-5B20-31AF-93C7-EDB3C99D072C`. Source sha256 prefixes:
mac_hooks.m `f0720802…`, MacWSSenderObservation.h `b0dd2eb5…`,
.c `54b6b3d2…`, test `4c1dd088…`, Makefile unchanged-functionally
(+1 file entry). Diff scope: mac_hooks.m +259/−20 (net), 2 new source
files, 1 new test; `git diff --check` clean; no commit.

## 5. Remaining live acceptance (unchanged plan, now sound)

A future authorized window arms the dedicated sentinel before a WS
launch, makes one canonical `SLSPostEventRecord` post, reads
`macws_sender_observation.log`, and matches the discrimination matrix —
now with per-invocation chains so each post's gate→disp→impl records
are provably one causal unit. Any runtime use requires a new reviewed
packet and user-session handling; nothing was restarted or armed.
