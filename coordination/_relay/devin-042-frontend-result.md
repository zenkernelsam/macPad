# devin-042 frontend result — independent input audit + gate map

Worker: macPad Devin D042 frontend 9589. Read-only; no device/backend.
Deliverable: docs/evidence/devin-d042-input-independent-audit-20261019.md

## D041 verification — exact AppInput path ACCEPTED (reviewer-inspected)

frame-after-text.png personally verified: live Finder "Searching This
M…" window with 'd' in focused search field, Finder menubar, dead
Terminal confirming target rejection. after-d≡after-right byte-identical
(no-op pair consistent); trace internally consistent (kind 11/12 pairs,
keycodes match action set, honest route=uncorrelated/post-to-app=-1).
Scope honesty: injector bypasses Host production stages — PROVEN half =
inputd→endpoint→bridge→sendEvent→AppKit→pixels; pointer/IME/clipboard/
global correctly not claimed.

## Gate interior — complete symbol-resolved map

_XPostEventRecord → CGXSenderCanSynthesizeEvents →
WSAuditTokenHoldsCachedProcessRight (cached-right + blraaz TCC
callback) → _audit_token_check_tcc_access = lazy-resolved
TCCAccessPreflightWithAuditToken, cset-eq polarity flip, PAC-signed
async arm exists (first post may deny while request in flight).
Second error class −304 for structural fails; observed 1002 = passed
structure, failed gate specifically.

## Corrections to prior claims

- responsibility103: ENOPOLICY confirmed; in-gate claim NARROWED (D043
  correction): checked import table shows no direct static call, but
  does NOT exclude indirect/dynamic/tccd-mediated involvement —
  responsibility stays THEORY, only plausible seat is tccd attribution.
- "TCC PostEvent refuses" unsupported — empty dict + gate0 carries no
  denial field. Leading replacement theory: gate evaluates the MIG-
  trailer sender token, which may carry different identity than the
  probe's self-token (accessor1 vs 1002 asymmetry = different tokens,
  not contradiction).
- Both D041 prep-note prologue claims verified byte-exact; both new
  observation targets also verbatim-safe + permit-installable.

## Design verdict: backend 2-point plan insufficient → 3-point minimal

Gate+XPostEventRecord counters alone don't discriminate arms. Minimal
set: gate (container ptr+size+ret), inner (entry+ret+blraaz-fires),
tcc-callback (entry+resolved-fn return+**sender token bytes**) — the
token dump is the single top discriminator. All prologues verified;
3 new static permit pins needed. Negative/acceptance criteria listed
in §3 — counter presence alone proves nothing.

Unobserved in current generation: sender token bytes, TCC-cb
invocation, cached-rights object state, ctx contents (outside slice —
kTCCServiceListenEvent/secureTap strings nearby, proximity≠proof),
HIDEvents domain question.

Stopping after handover per packet.

(handover to codex now)
