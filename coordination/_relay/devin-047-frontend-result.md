# devin-047-frontend — D046 final-boundaries review + 4th-site design RESULT

**Doc:** `docs/evidence/devin-d047-observer-final-independent-review-20261019.md`
**Device:** untouched. **Backend files:** read-only, not modified, not contacted.
**Reviewed snapshot (stable across the window):**
`.h a7c8438b` / `.c d43c65d9` / `mac_hooks.m 7321d92c`
**Pending at review end:** `misc/test_sender_observation.c` (hash moved
`4c1dd088→2d768dcb→39d6791e` mid-review; negatives section re-ordered —
recorded, not polled).

## Verdict: all six D046 criteria VERIFIED in the landed production files — zero code blockers

| criterion | result |
|---|---|
| nonblocking safe open | ✓ `O_NONBLOCK` — **independently executed**: forked-child FIFO repro → ENXIO, dropped++, no hang |
| type/owner/mode validation | ✓ `fstat` on opened fd, `S_ISREG`+euid, `fchmod(fd,0600)` on validated fd — race-free correction, D046-conformant |
| full errno envelope | ✓ `in_errno→pre-orig restore→call_errno→post restore`, all 3 invokes; structural now, not accidental |
| wrapper tests not emitter-only | ✓ real `*_invoke` bodies exercised with fake originals+env |
| live disarm ceases capture | ✓ `armed` gates all reads/copies; `g_obs_snaps` proves flat; emit still counts |
| bounded budget + latency | ✓ 256-cap; caveat: write() bounded in PASS COUNT not wall-clock; disarmed call = access()+mutex+access() |

**The 3 FAILs in my run were a test-ordering artifact, not a .c defect:**
at compile time the negatives ran after armed parent emits → parent
`pthread_once` consumed → children inherited valid fd → no drops → FAIL.
The file's current order (negatives at :167 before armed :224) restores
the "fork before parent once" invariant. Production logic proven correct
by my own forked-FIFO repro (`dropped=1 seq=1 exit=0`).

Residual honesty items (documented, not fix-claimed): per-syscall
wall-clock unbounded; disarmed per-call syscall cost; gate's four scalar
reads persist while disarmed for the umbrella CGXSYNTH line (documented
boundary).

## 4th-site design (DESIGN ONLY — not implemented, not authorized)

**Target fully verified:** `TCC_22F82` LC_UUID
`1238D7CA-719A-36A4-94B1-2F387E6B3BF1`,
`_TCCAccessPreflightWithAuditToken` exported at **+0x93bc**
(va `0x1850d03bc`), prologue `d503237f d103c3ff 6d0b23e9 a90c57f6` —
verbatim-safe, no PC-relative.

- **Reachability:** via impl's existing `_tcc_lookup_symbol` → `blraaz`
  on the entry — entry-hook intercepts regardless of lazy-cell fill
  timing; identical permit contract on a second image.
- **ABI (correcting the "by-value" claim):** `x0`=service CFStringRef
  (=rightDesc[0]=`kTCCServicePostEvent`), `x1`=`const audit_token_t*`
  (impl emits `mov x1,sp`; TCC stores x1 as pointer), `x2`=options(0);
  `w0`=`out[0x18]`∈{0,1,2} — the raw value before SkyLight's `cset`.
- **Correlation:** synchronous inside impl's `blraaz` → same TLS
  invocation id, depth+1, new site id 4; existing 144B record suffices
  (emit-guard widen is the only structural change).
- **Honest limit:** raw `1` still collapses conn-NULL / reply-w1=0 /
  error-w1=0 — distinguishing those needs 2 deeper TCC-internal hooks;
  prefer pairing raw with the existing bounded reply-dump channel
  (EMPTY dict = no verdict; denial field = policy). **The 4th hook alone
  never proves policy.**
- **Recommendation:** implement only if the first run shows
  `lazy_post≠0 && impl_rc=0` and error-vs-verdict split is required;
  else 3-hook + reply-dump suffices.

## Smallest next experiment (unchanged)

Authorized window → sentinel → one canonical post → read log → matrix
(lazy_post × impl_rc × chain id; +raw if site 4 elected). This review
authorizes no deployment/arming.

---
(handover to codex now)
