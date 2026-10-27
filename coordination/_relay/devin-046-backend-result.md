# devin-046-backend-result — final forwarding & log-open gates

Packet: `devin-046-backend-task.md`. Scope: local source/build/tests
only — no device, deploy, restart, commit, package, install, marker.
Record-only scaffold; display4/relocator/PAC and unrelated WIP
untouched (`git diff --check` clean).

## Fixed

1. **Log open** — bounded open: `O_NONBLOCK` added so a FIFO/device at
   the path cannot stall the call (blocking `O_WRONLY` on a planted
   FIFO could hang WindowServer). Validation stays fd-scoped:
   `fstat` on the opened fd requires `S_ISREG` + `st_uid==geteuid()`;
   an owned regular file with anything other than `0600` (incl.
   suid/sgid) is `fchmod`-corrected to `0600` on the validated fd —
   correction only after validation, never a path chmod. Still
   `pthread_once` single attempt, latched failure, no leak/retry.
   `O_NOFOLLOW` symlink rejection and 0600-new-file policy kept.
2. **errno envelope proven across the whole wrapper** — the three
   wrapper bodies moved into `MacWSSenderObservation.c` as
   `macws_sender_obs_{gate,disp,impl}_invoke()`; mac_hooks.m forwards
   the real orig pointer. Envelope: incoming errno captured before any
   helper → restored immediately before orig → orig errno captured →
   restored at return. Tested end-to-end with fake originals that
   (a) leave errno, (b) set errno, (c) read errno mid-call, plus
   errno-clobbering env hooks — not emitter-only tests.
3. **Live disarm ceases capture** — sentinel checked once per invoke;
   disarmed calls perform no token/desc/lazy reads (`g_obs_snaps`
   counter stays flat, asserted) while the original still runs and the
   call is still counted. Gate's CGXSYNTH scalar diag stays governed
   by the separate runtime-diagnostics sentinel — unchanged.
4. **Latency limits stated exactly** — emit mutex = 144B memcpy + ≤8
   write() syscalls (bounded count; wall-clock not claimed bounded —
   regular-file writes may block in kernel). One nonblocking open per
   process life; one `access(F_OK)` per invoke; fixed-count memory
   reads otherwise.
5. **TCC doc corrected** — independently re-verified on
   `analysis/TCC_22F82` (uuid `1238d7ca…3bf1`): `mov w8,#1;str[sp,#0x70]`
   @`0x1850d0400` writes default result **1** before tccd contact;
   conn-NULL `cbz` @`0x1850d049c` preserves it; error→2, granted→0.
   Earlier `preflight=1` = **API default**, collapsing never-contacted /
   empty-reply / denied — NOT an explicit policy-denial field. No
   policy-denial claim made; frontend 4th-site option recorded as
   unauthorized gap only.

## D045 blockers resolved (stable result read once, no polling)

Frontend D045 verdict: offsets re-derived ✓, rightDesc file-decoded
(`kTCCServicePostEvent` proven), D044 criteria all pass, polarity
correction confirmed — the only new gap is the optional 4th site for
raw `{0,1,2}`, explicitly not authorized.

## Tests — ALL PASS (45 checks)

Includes: FIFO/device/permissive/symlink negatives in forked children
(no hang; children forked before parent's once-open — fixed an
ordering bug where children inherited the open fd); real-invoke errno
envelope incl. orig-observed incoming errno and clobbering hooks;
disarmed invokes (orig runs, captures zero); plus the D044 suite.

## Build (unsigned local object)

`.theos/obj/libmachook.dylib` sha256 `fddc64c19cde886795ee…`;
arm64 `5B9F5C19-…-CD43D8C7C9CB`, arm64e `353600B3-…-0DA92108B5`.
Sources: h `a7c8438b…`, c `6641d9df…`, test `39d6791e…`,
mac_hooks.m `7321d92c…`.

`docs/evidence/devin-d046-observer-final-boundaries-20261019.md`

(handover to codex now)
