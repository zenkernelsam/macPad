# D046 — observer final forwarding and log-open boundaries

Date 2026-10-19, packet `coordination/_relay/devin-046-backend-task.md`.
**Local source/build/tests only — no device, deploy, restart, commit,
package, install, marker or device action.** Record-only diagnostic
scaffold; no return value, cache, audit token, rightDesc, TCC state,
identity or policy is modified. display4 permits, relocator, PAC and
unrelated WIP preserved.

## 1. Log open hardening (was: type check after a blocking open)

`macws_sender_obs_open_log` now:

- opens with `O_WRONLY|O_CREAT|O_APPEND|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC`
  mode `0600`. **O_NONBLOCK makes the open itself bounded** — a FIFO at
  the path fails ENXIO immediately (or opens unblocked and is then
  rejected); a blocking `O_WRONLY` on a planted FIFO could otherwise
  stall WindowServer on first emit. Devices cannot stall a nonblocking
  open either.
- `fstat`s the **opened fd** (never a path stat — no TOCTOU):
  non-regular → refuse; `st_uid != geteuid()` → refuse.
- An owned regular file carrying anything other than `0600` (incl.
  suid/sgid bits) is **fchmod-corrected to 0600 on the validated fd** —
  race-free correction explicitly permitted by the packet; never a
  path chmod, never widened.
- Still `pthread_once` single attempt; failure latches `-1`, drops
  counted; exactly one open ever (no fd leak, no unbounded retry).

Negative coverage (forked children, pristine once-state): **FIFO**
refused without blocking; **device** (`/dev/null`) refused non-regular;
**over-permissive owned 0644 file** corrected to 0600 and used;
**symlink** refused (O_NOFOLLOW). Foreign-owned file: enforced by the
same `st_uid != geteuid()` check, untestable without a second uid —
documented gap.

## 2. Wrapper-equivalent invoke functions — errno proven end-to-end

The three wrapper bodies moved into `MacWSSenderObservation.c` as
`macws_sender_obs_{gate,disp,impl}_invoke()`; `mac_hooks.m` replacement
functions are thin forwarders passing the real original pointer.
Rationale: the packet required tests to exercise **the wrappers**, not
the emitter alone — the invoke functions ARE the production forwarding
path, host-testable with fake originals.

Errno envelope (identical at all boundaries):

1. capture incoming errno before ANY helper (chain/TLS/capture/env
   hook);
2. armed-gated captures + env hooks run — clobber freely;
3. **restore incoming errno immediately before the original**, so the
   original observes exactly the caller's errno;
4. capture the original's errno;
5. emit/chain_exit/post-diag run;
6. restore the ORIGINAL's errno at return; return its rc verbatim.

Host tests exercise: orig leaves errno (caller sees it), orig sets
errno (caller sees orig's EBADF, hooks' EAGAIN/EFAULT clobbers hidden),
orig reads errno mid-call and sees the restored incoming value, and
errno-clobbering env hooks inside the envelope.

## 3. Live disarm ceases record capture

Each invoke checks the dedicated sentinel once at entry. When absent:
no `chain_enter`, no token/desc/lazy reads, no record capture —
`g_obs_snaps` stays provably flat (new read-only counter, asserted in
test) — while the original still runs under the identical errno
envelope and the call is still counted via emit's disarmed-drop path.
Mid-call disarm is caught by emit's existing check (record dropped).
The gate's scalar audit reads (ptr/size/euid/pid feeding the
runtime-diagnostics `#### CGXSYNTH` line via `post_orig`) remain
governed by that separate sentinel inside the caller's callback —
they are not record snapshots and no token bytes are handed out.

## 4. Exact remaining latency limits (no "bounded time" claim)

- One emit mutex serializes fill→publish→log; hold time = one 144B
  memcpy + at most **8 `write()` syscalls** (bounded pass count,
  EINTR-in-budget). Wall-clock is NOT claimed bounded: a regular-file
  write may still block in the kernel on storage stalls.
- Log open: single nonblocking open attempt per process lifetime.
- Gate invoke adds: ≤4 container scalar reads + 32B/40B memcpys + TLS
  ops; disp/impl adds bounded arg memcpys + ≤2 lazy-cell reads. All are
  fixed-count plain memory accesses.
- Sentinel check is one `access(F_OK)` syscall per invoke — the same
  per-call filesystem touch the existing `runtime_diagnostics` gate
  already performs; no caching introduced.

## 5. TCC documentation correction — API default ≠ policy denial

Independently re-verified against `analysis/TCC_22F82` (LC_UUID
`1238d7ca-719a-36a4-94b1-2f387e6b3bf1`, `__TEXT` base `0x1850c7000`),
matching the frontend D045 audit:

- `TCCAccessPreflightWithAuditToken` writes **default result 1** before
  any tccd contact — `mov w8,#1` (`52800028`) + `str w8,[sp,#0x70]`
  (`b90073e8`) at va `0x1850d0400`.
- conn-NULL early-return preserves that default — `cbz` (`b4000300`)
  at `0x1850d049c`.
- The error block writes 2; the reply block writes 0 only on a granted
  verdict (w1≠0).

Therefore the earlier probe's self-token `preflight=1` is the **API
default path** — it collapses never-contacted / empty-reply /
denied-verdict. It is NOT evidence of an explicit policy-denial field.
SkyLight's impl further collapses `preflight!=0 → 0`. The three hooks
cannot distinguish those sub-cases; they DO separate lazy-resolve
failure (cell stays NULL) from an evaluated preflight (cell set).
Frontend's proposed 4th-site option (TCC-image preflight hook for the
raw `{0,1,2}`) is recorded as a gap only — **not authorized, not
installed, not claimed.**

## 6. Tests — ALL PASS (45 checks, x86_64 host)

`cc -DMACWS_SENDER_OBS_TEST -o /tmp/test_sender_obs
 misc/test_sender_observation.c libmachook/MacWSSenderObservation.c`

New coverage beyond D044's suite: real-invoke errno envelope (5
checks incl. hook clobbering + orig-observed errno), pre-orig audit
scalars to `post_orig`, armed-invoke capture counter, FIFO/device/
permissive/symlink negatives (children forked BEFORE the parent's own
once-open so each gets pristine state — an earlier revision of this
test raced that ordering), disarmed invokes (orig runs, snaps flat),
existing suite unchanged.

## 7. Build artifacts (local object only)

`.theos/obj/libmachook.dylib` sha256 `fddc64c19cde886795ee…`;
arm64 uuid `5B9F5C19-ACBF-3236-8C67-CD43D8C7C9CB`,
arm64e uuid `353600B3-51EB-338E-9188-000DA92108B5`.
Source sha256 prefixes: header `a7c8438b…`, .c `6641d9df…`,
test `39d6791e…`, mac_hooks.m `7321d92c…`. `git diff --check` clean.
These are unsigned-local identities — signed/deployed hashes would
differ by signing, per project hash discipline.

## 8. Remaining live gaps

- 4th-site raw-preflight option (frontend §5) requires a new reviewed
  packet; not installed.
- Foreign-owner log rejection is code-enforced but host-untestable
  without a second uid.
- Records remain ring+bounded-log; a WS crash loses un-drained records.
- Any runtime arming requires a coordinator-elected deployment review;
  nothing was armed, installed or restarted.
