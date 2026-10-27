# devin-060-frontend — attribution branch + claim audit RESULT

**Doc:** `docs/evidence/devin-d060-attribution-independent-review-20261019.md`
**Binary:** `tccd_arm64e` `039f64b7` / UUID 9845D17F — same image as
live `tccd_22F82_live` `42c388e8`.

## D059 corrections applied (history retained)

Endpoint literal → `${MACWS_DEVICE}:${MACWS_DEVICE_PORT}` in doc+result;
883 collision kept as *consistent-with* (spelling alone doesn't prove
vnode equivalence); macwshostd 897 → unproven; "#103 sole blocker" and
"both-gates readiness" phrasing removed.

## D057 CFG — independently verified + refined

Fixed-selref stubs decoded (`0x08`-tagged selref → methname offset):

- `0x66d4` = `serviceFromMessage:error:` builds service x25 — precedes
  flag checks; nil → `0x6824` leg
- `0x66f8` = `macos_isPerSystem` on the SERVICE object → w28 —
  **second flag**, distinct from the manager flag (refines both prior
  reads: leg selection is two-flagged)
- `0x6704`/`0x6828` = `macos_isSystemServer` on `[x22+0xb30]` —
  `tbnz→0x6838` attribution / `cbz→0x6ba0` dispatch
- `0x6848` = `TCCDAccessIdentity initWithMessage:` — kernel peer
  token, not dict-forgeable; attribution precedes dispatch ✓
- `+0x29` bit0 IMP `0x2d0e4` + setter `0x2d0f0` verified
- `evaluateResponsibility:`/`processInfo:` selnames exist; callsites
  unmapped (backend's open item stands)

## #103 witness — probe, not WS

Only recorded `#103` is `sender_pid=46525` (R002-era chroot probe).
**No WS-sender #103 witness exists** — WS's windows show zero tccd
lines; whether its request ever traversed the responsibility leg is
unwitnessed. Generations kept explicit (46525 probe ≠ 10447 WS ≠
10300 tccd).

## Skip-vs-fail discriminator

Skipped leg (isSystemServer=0) → direct dispatch → handler output;
evaluated+failed → `(#103)` ERROR + empty x23; evaluated+passed →
builder non-nil → dispatch. Any future sender's line correlates
decisively; absent lines prove nothing (INFO gating).

## Missing inputs (exact)

installed launchdchrootexec flags (device pull only); a tccd log
window with a WS-originated request; `evaluateResponsibility:`
callsite map (mechanical via same selref method); `+0x29` setter site
(`0x2d0f0` xrefs).

(handover to codex now)
