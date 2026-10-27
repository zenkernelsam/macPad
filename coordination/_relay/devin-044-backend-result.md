# devin-044-backend-result — observer reliability fixes

Packet: `devin-044-backend-task.md`. Scope: local source/build/tests
only — no device, deploy, restart, commit, package, install. Record-only
scaffold; no display/global-diagnostics coupling.

## Fixed blockers

1. **Offset bugs (frontend review)** — corrected constants verified by
   direct subtraction: audit slot `0x53AC11B8`, rightDesc `0x4ECE66C8`,
   lazyChecker `0x53AC1B50` (D043 draft dropped a nibble each;
   rightDesc read ~90MB high → would fault/garbage on first record).
2. **fd open race/leak** — `pthread_once` single attempt:
   `O_NOFOLLOW|O_CLOEXEC` mode `0600`, fstat `S_ISREG`+euid validated;
   failure latches `-1` + drop counting (bounded, no unbounded retry).
3. **Publication race** — one emit mutex serializes
   fill→publish→log; per-slot release-flag publish after memcpy; ring
   accessor exposes only published slots.
4. **No nested correlation** — TLS depth + invocation id: outermost
   observed entry assigns id flagged CHAIN_ROOT (gate) or STANDALONE
   (unpaired inner-boundary call); nested entries inherit; depth-0
   unwind clears (no stale pairing). Record v2 = 144B, adds
   `invocation` + chain flags.
5. **errno envelope** — wrappers save orig errno immediately post-call
   and restore at return; all inspection uses plain memory reads;
   emit saves/restores internally.
6. **Bounded I/O** — `write_all` ≤8 passes, EINTR-in-budget,
   short-write continuation, hard fail → dropped.
7. **Lifetime** — gate container bytes captured pre-orig (the window
   the gate itself is about to verify); inner-boundary args read inside
   the wrapper frame only.
8. **Frontend notes** — TCC preflight `0=allowed/1=denied/2=unknown`
   (earlier probe `=1` reading was **denied** — inverted; consistent
   with callsite `cset eq`); rightDesc field map accepted; service arg
   recorded by pointer identity (name assertion dropped); raw checker
   value acknowledged as boundary-residual, not capturable here.

## Tests — ALL PASS (35 checks)

default-off / forked-child symlink rejection / armed 144B ring+log
mode0600 / nested chains share invocation + distinct across chains /
standalone flagged + fresh id post-unwind / 8-thread simultaneous first
emission (unique seqs, intact records) / bounded write_all (full-pipe
EAGAIN, zero-len) / 256 cap / live disarm / pin byte-equality +
corruption mismatch / corrected-offset asserts.

## Build (local object, unsigned-deployable hashes differ by contract)

`.theos/obj/libmachook.dylib` sha256 `2846fd60aa7b76e6cbc4…`;
arm64 `D0CECC39-…-050C`, arm64e `74635F48-…-D072C`. mac_hooks.m
+259/−20; 3 new files (obs .c/.h, host test); Makefile +1 entry;
`git diff --check` clean; no commit.

## Remaining live acceptance

Unchanged plan, now with sound plumbing: authorized window → sentinel
before WS launch → one canonical post → read `…_observation.log` →
match discrimination matrix (invocation ids make each gate→disp→impl
triple one causal unit). Any runtime use needs a new reviewed packet;
nothing armed or restarted.

`docs/evidence/devin-d044-observer-reliability-20261019.md`

(handover to codex now)
