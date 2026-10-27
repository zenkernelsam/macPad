# D047 — D046 Final Boundaries + Minimal Raw-Preflight Design (independent)

**Scope (devin-047-frontend-task.md):** independently review the stable D046
observer changes (log-open negatives, full errno envelope, wrapper-level
tests, live-disarm capture cessation, bounded budget + honest latency);
deliver a DESIGN-ONLY minimal fourth boundary on TCC's preflight returning
the raw {0,1,2}; do not touch device/backend files/Git; no relaxed
approval from prior checks. **No device access. No production edits.**

**Snapshot discipline:** backend D046 landed mid-review. The three
production files were STABLE across two hashes 20 s apart; the host test
file moved (two different hashes observed in-session) — exact snapshots
recorded below, no polling performed.

```
Reviewed-at snapshot (stable across the window):
  MacWSSenderObservation.h a7c8438b3b509ff6…  (9936 B, 01:25)
  MacWSSenderObservation.c d43c65d98b75eff5… (14846 B, 01:25)
  mac_hooks.m               7321d92c530368b7… (1397855 B, 01:25)
Pending/moving at review end:
  misc/test_sender_observation.c  observed 4c1dd088→2d768dcb→39d6791e
  (negatives section re-ordered mid-review; verdict on its mechanics
   below is per-mechanism, not per-current-line)
```

---

## 1. D046 review — all six criteria VERIFIED in the landed .c

### 1a. Safe nonblocking log open — ✓ (independently executed)

`macws_sender_obs_open_log` now opens
`O_WRONLY|O_CREAT|O_APPEND|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC, 0600` then
`fstat`-validates on the OPENED fd (no TOCTOU): `S_ISREG` + `st_uid==euid`,
and `(st_mode & 07777) != 0600` triggers `fchmod(fd,0600)` — race-free
correction on the validated fd, matching the "explicit safe correction
only after validation" allowance.

Independent execution proof (my own repro, `analysis`-free,
`/tmp/d047_fifo_repro` — forked child, mkfifo path, real emit):
`child: dropped=1 seq=1 … exit=0` — O_NONBLOCK FIFO open fails ENXIO,
**no hang**, drop counted. The production code is correct.

**Test-ordering artifact found (test bug, not production):** at the
moment I compiled the host test, the "log-open negatives" children ran
AFTER the armed/invoke sections — the parent's `pthread_once` was already
consumed by its own valid log, so every negative child inherited
done-once + a valid fd, wrote to the REAL log, and reported FAIL
(`fifo / device / 0644 → 3 FAILs`). The current file re-orders the
negatives BEFORE any armed parent emit (line 167 < 224), restoring the
"fork before parent once" invariant their own comment requires. The 3
FAILs were harness-order artifacts; the .c logic is verified correct by
my independent repro. (Backend file still moving — noted, not polled.)

### 1b. errno envelope across the WHOLE wrapper — ✓

All three `*_invoke` bodies implement the contract verbatim:
`in_errno=errno` at entry → helpers/captures/env-hooks may clobber →
`errno=in_errno` immediately before `orig()` → `call_errno=errno` post-orig
→ emit/chain_exit/post_orig clobber freely → `errno=call_errno` →
`return r`. Wrapper tests exercise: orig-leaves-errno (orig reads
restored incoming EDOM), orig-sets-errno (EBADF forwarded, hook clobber
EFAULT/EAGAIN hidden). Previously this was safe only by accident (pre-orig
ops happened to be pure); now it is structural.

### 1c. Wrapper tests (not emitter-only) — ✓

The new test section invokes the REAL forwarding bodies
(`gate/disp/impl_invoke`) with fake originals + fake env — the exact
code the mac_hooks.m hooks now forward to. Emitter-only coverage is
supplemented, not relied on.

### 1d. Live disarm ceases record capture — ✓

`armed` is sampled once at wrapper entry; ALL captures (chain ids,
token/desc memcpy, lazy reads, `g_obs_snaps++`) sit inside `if (armed)`.
Disarmed call = `enabled()` access → `orig` → emit → dropped++, snaps
flat — verified by test checks + the counter itself. Boundary note: the
gate's four SCALAR reads (ptr/size/euid/pid) run unconditionally for the
umbrella `#### CGXSYNTH` line — scalars only, no token/desc bytes, and
they belong to the separate runtime-diagnostics feature; the boundary is
documented, but it is the one piece of WS memory still read while
senderObs is disarmed.

### 1e. Bounded budget + honest latency — ✓ with precise caveats

- Ring ≤256, fixed 144 B records, no dynamic alloc ✓
- `write_all` ≤8 passes incl. EINTR — **bounded in PASS COUNT, not
  wall-clock**: each `write()` syscall itself may block on I/O (the only
  nonblocking element is the open). Any "bounded" claim must carry this
  qualifier.
- Per-call cost while DISARMED: one `access()` at wrapper entry +
  emit's mutex + a second `access()` + counter — i.e., disarmed calls are
  not free (uncached sentinel is the price of next-call disarm). While
  ARMED add: memcpy 72 B + seq atomic + write(s) under the emit mutex.
- Emit's `enabled()` runs `access()` INSIDE the global mutex — one
  syscall per record serialized; correct but the serialization cost is
  real (diagnostic-rate acceptable; gate fires per event post, not
  per frame).

### 1f. Sentinel independence — ✓

All observation paths gate on `/tmp/macws_sender_observation` only; the
umbrella `macws_runtime_diagnostics` appears solely inside the caller's
own post_orig hook (the legacy CGXSYNTH line) — no display coupling in
the observer.

### Residual non-blocking notes

- `g_obs_site_calls[]` sized 4 for sites 1..3 — a 4th site needs the
  bound widened (§2 notes it).
- `emit` rechecks `enabled()` inside the mutex after the wrapper already
  checked — deliberate (TOCTOU between check and write) — keep.
- Test file was mid-edit at review end: record pending snapshot
  `39d6791e…` — the negatives now precede armed emits (correct order);
  do not treat my earlier 3-FAIL run as a code defect.

## 2. Minimal fourth boundary — DESIGN ONLY (raw preflight {0,1,2})

### Target (all binary-verified)

| field | value |
|---|---|
| image | `analysis/TCC_22F82` (TCC.framework) |
| LC_UUID | `1238D7CA-719A-36A4-94B1-2F387E6B3BF1` |
| symbol | `_TCCAccessPreflightWithAuditToken` — **exported** (`N_SECT|N_EXT`) |
| vm addr | `0x1850d03bc` → image-relative **+0x93bc** (base `0x1850c7000`) |
| prologue | `d503237f d103c3ff 6d0b23e9 a90c57f6` — pacibsp + sub sp,#0x100 + stp d9,d8 + stp x22,x21 — **verbatim-safe, no PC-relative** |

### Reachability through the existing resolver — ✓ no resolver change

SkyLight's impl resolves the entry via its own `_tcc_lookup_symbol`
(dlopen TCC.framework → dlsym this name) and calls it `blraaz` — an
IA+0-authenticated indirect call into the FUNCTION ENTRY. A
prologue-level hook at +0x93bc intercepts every call **regardless of
when the lazy cell was filled**: the stored pointer IS the entry VA, so
pre-resolved and post-resolved calls both land in the trampoline —
identical mechanics to the three SkyLight sites (same provider permit:
uuid + image-relative offset + 4-word prologue pin, fail-closed).

### ABI at the hooked entry — callee-verified, correcting the "by-value" claim

SkyLight's callsite (`+0x340df4..0x340e08`):
`stp q0,q1,[sp]` (32 B token copy on stack) → **`mov x1, sp`** →
`mov x0, x19` (service) → `mov x2, #0` → `blraaz`.
TCC's own entry stores `x1` as a pointer-sized value (`stur x1,[x29,#-0x68]`)
and passes `&slot` onward — so the signature is **pointer**, not
by-value:

```
int64_t TCCAccessPreflightWithAuditToken(
    CFStringRef          service,   /* x0 = rightDesc[0] = CFSTR("kTCCServicePostEvent") */
    const audit_token_t *token,     /* x1 = &32B token — readable during the call   */
    CFDictionaryRef      options);  /* x2 = 0                                       */
/* w0 = out[0x18] : 0=allowed, 1=non-allow(default|denied|unreached), 2=error */
```

Capture window: `*x1`'s 32 B is the caller's stack copy — lifetime = the
call itself; a pre-orig memcpy inside the wrapper is the verified window.

### Correlation with the existing three-hook chain — free

The preflight call is synchronous inside impl's `blraaz`, which runs
inside `macws_obs_orig_tcc_check`, which runs inside
`macws_sender_obs_impl_invoke`'s armed chain — so a TCC-site wrapper
`chain_enter`s at depth+1 and **inherits the same TLS invocation id**.
One new site id (`SITE_TCC=4`), the existing 144 B record carries
`rc`=raw `w0`, `token[32]`, `arg_ptr`=service ptr, `lazy_*` unused.
Changes needed: emit guard `site > SITE_IMPL` → `> SITE_TCC`, site_calls
bound, and a 4th permit block in the install site (TCC-image header via
dyld image search — or, laziest correct variant: hook the
ptrauth-stripped value already in `*g_macws_obs_lazy`, trading the first
post-install call for zero image lookup).

### Decision matrix with 4 sites (what it actually buys)

| lazy_post | impl_rc | raw w0 | verdict |
|---|---|---|---|
| 0 | 0 | — | **preflight never ran** — resolve failure (dlopen/dlsym) |
| ≠0 | 0 | 1 | ran → **non-allow** (denied verdict OR never-contacted OR empty reply — still collapsed) |
| ≠0 | 0 | 2 | ran → **error/unknown** |
| ≠0 | 1 | 0 | ran → **allowed** |
| ≠0 | any | (absent raw) | today: {1,2} merged — the collapse this hook removes |

### What the 4th hook CANNOT resolve — stated plainly

Raw `1` still collapses three internal paths inside TCC: (a) conn-getter
NULL → request never sent, default survives; (b) reply block ran with
`w1==0` → default survives (denied verdict OR EMPTY reply — same code
path); (c) error block `w1==0` → default survives. Separating those
needs TWO MORE sites inside TCC (conn-getter return + the reply/error
block `w1`s) — deeper hooks on a second image's internals. **Not
recommended**: the existing bounded reply-dump channel (R007/R008 style)
already surfaces the actual tccd dictionary — pairing `raw=1` with an
EMPTY dictionary means "no verdict returned" (transport OK, tccd silent);
with an explicit denial field means "policy denied". **The 4th hook alone
never proves policy** — it removes SkyLight's `cset` collapse, nothing
more.

### Cost/benefit verdict

Gain: splits impl_rc=0 into never-ran / ran-non-allow / ran-error —
beyond what lazy_post alone shows, adds the error-vs-verdict split
without trusting TCC-side inference. Cost: one permit on a SECOND image
(TCC uuid+offset+prologue — all verified verbatim-safe), one wrapper
with identical errno/chain discipline, a one-line emit-guard widening,
and an image-header lookup. **Recommendation: implement only if the
first observation run yields `lazy_post≠0 && impl_rc=0` and the
experiment needs the raw split; otherwise the 3-hook set + reply-dump
already discriminates the decision classes that matter.**

Sentinel: same dedicated `/tmp/macws_sender_observation` (default-off,
record-only, no display coupling); or a nested sub-flag if finer arming
is wanted. Not implemented — design only.

## 3. Blockers / smallest next experiment

- **Code blockers: none found** in the landed .c/hooks snapshot — all
  six D046 criteria verified; the 3 FAILs were test-ordering artifacts
  (backend's reorder fixes them; test file still moving — pending
  snapshot recorded, not polled).
- **Residual honesty items** (record, don't fix-claim): write()
  wall-clock unbounded per syscall; disarmed per-call = access()+mutex+
  access(); gate scalar reads persist while disarmed (umbrella feature,
  documented).
- **Missing raw evidence**: the {0,1,2} split — design in §2 ready if the
  first run demands it.
- **Smallest next experiment** (unchanged, now on verified plumbing):
  authorized window → sentinel before WS launch → one canonical
  postEventRecord → read `…_observation.log` → read off the §2-style
  matrix (lazy_post × impl_rc × chain id; add raw w0 if site 4 is
  elected). Deployment/arming remains unauthorized by this review.
