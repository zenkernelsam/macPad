# devin-037 frontend result — final relocation review + A/B criteria

Worker: macPad Devin D037 frontend 9589. Read-only; no device/backend
contact. Deliverable:
docs/evidence/devin-d037-relocation-final-independent-review-20261018.md
— consolidates/supersedes the D035 P1 verdict and rolling D036 checks.

## Verdict

FINAL source/build approval for the settled relocator candidate
(provider fat 82e74519…e3a9; arm64 2a8da5bf…, arm64e 3bde9239…;
libmachook bf3ae467/f2143308 unchanged). Approval scope = the four
pinned targets and the tested classes only: verbatim position-
independent insns, cbz/cbnz translation + intra-window refusal,
non-returning register branches. b.cond/tbz/b/bl/adr/adrp/ldr-literal
stay REFUSED — approval does not extend to them.

All three frontend-filed defects are fixed + fixtured: imm19=5 (was
mid-.quad crash), brabz/blrabz real encodings (were dead constants),
intra-window cbz target refusal (was silent mis-relocation).
Independently re-verified: own x86_64 TESTONLY build → host test
ALL PASS; simulator lands every translated branch correctly.

## Coverage honestly scoped

Proven host-side: byte-exact thunk, all refusal classes live, uuid/
prologue/offset gate (-2/-1/-1), arm64e *result signing (asia,0)
matching blraaz callers, x17=IP1-only clobber.
NOT yet proven (backend D037 live probes, pending): exact X-register
cbz execution (existing live fixture used w0/w1 — mechanism proved,
exact encoding class not), braaz authenticated-tail execution against
an A-key/zero-signed target, live IOMFB slice uuid.

## Plan review

D034-plan-v3 + D037 gates validated: preserve/rollback, watchdog via
own stop path, one bounded generation, byte→counter→state→pixel ladder,
argv copied verbatim = no diagnostic-producer confound, bundle = one
declared variable. TWO mechanics gaps flagged in the deploy step:
scp-overwrite is the documented inode landmine (needs tmp+atomic mv)
and no explicit re-sign step — both must be in the deploy line.

## Acceptance/rejection criteria

Written into the evidence doc §5 — six ordered rungs (probe pass →
uuid+prologue match → changed bytes → counters → state=ready+advancing
→ pixels) and explicit reject/rollback triggers incl. any residual
named REFUSED on the four targets.

Stopping per packet — no pending frontend work.

(handover to codex now)
