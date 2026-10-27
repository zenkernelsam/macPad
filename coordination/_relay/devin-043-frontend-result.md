# devin-043 frontend result — ABI/polarity audit + candidate review

Worker: macPad Devin D043 frontend 9589. Read-only/local. Deliverable:
docs/evidence/devin-d043-sender-observation-independent-review-20261019.md

## ABI/polarity — all three boundaries byte-verified

Gate (void)→w0{0 deny,1 allow}; dispatcher(token32,desc40,signed-impl)
→w0 synchronous {flag-byte / 1 self / cached-mask / impl_rc}; impl
(token32ptr,descriptor)→(preflight==0). Pins verified: 318464=
d503237f d101c3ff a9067bfd 910183fd; 3407ec=d503237f d102c3ff a90667fa
a9075ff8; 340da0=d503237f d10183ff a90357f6 a9044ff4.
rightDesc ABI decoded: [0]=service/right ptr→preflight arg,
[8]=deny-mask, [0x10]=allow-mask (proven by cached-path consistency),
[0x18]=decided-mask, [0x20]=pid==0 flag byte. Cache=getProcessCredential-
Cache CFDictionary; invalidation = notify com.apple.tcc.access.changed
sweep + keyed continuation (death-watch THEORY — not async TCC;
verdict fully synchronous). TCC_22F82 disasm: preflight out[0x18]
default1/error2/reply0 → **0=allowed,1=denied,2=unknown** → probe
self-token result=1 = DENIED — prior "1=allowed" readings inverted;
self-vs-sender asymmetry dissolves (token identity still unverified).

## Outcome matrix delivered (§2)

Full gate/disp/impl count+rc matrix covering arm A/B, contradiction
rows, D0/D1/D2, I0 resolve-fail (lazy 0→0), evaluated-deny (lazy→≠0,
impl_rc=0), fold anomaly, repeat-post cache rows. Boundary-residual
noted: raw preflight value (denied vs unknown vs transport) needs
intra-impl read or tccd correlation — NOT capturable at these sites.

## Candidate verdict — fix 2 live offset bugs, then sound

Design approved in structure: dedicated /tmp/macws_sender_observation
sentinel (unconfounded, not the umbrella), isWS+permit arming,
record-only/errno-safe/bounded 256/denominator-survives-disarm.
BLOCKERS: OFF_LAZY_CHECKER 0x5AC1B50→must be 0x53AC1B50 (poisoned
lazy discriminator); OFF_RIGHT_DESC 0x542E66C8→must be 0x4ECE66C8
(90MB-high pointer → garbage desc or WS fault on first record);
latent OFF_AUDIT_SLOT 0x5AC11B8→0x53AC11B8. Non-blocking: drop
"kTCCServicePostEvent CFStringRef" assertion (service identity
unverified; pointer logged only). xpc_bootstrap IA+0 fix correct;
provider relocator diff consistent w/ D036; MACWSHookPermitV2
4-arg contract matches. Requirements list in doc §3.

## Corrections applied to own D042 docs (history retained)

responsibility103: "REFUTED"→narrowed (no direct static call shown;
indirect/tccd-mediated not excluded — THEORY). Async-pending wording
withdrawn (sync verdict). impl signature refined. See doc §4.

No device/build/source/Git mutations; current desktop untouched.
Stopping after handover per packet.

(handover to codex now)
