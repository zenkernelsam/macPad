# devin-117 frontend result — responsibility/TCC upstream fault-closure audit

Read-only audit complete. Evidence doc:
`docs/evidence/devin-d117-frontend-20261019.md`. No edits/device/build/Git;
backend D116 files untouched.

## Narrowest still-unresolved edge — bounded tighter than D106

**Stub `0x18b321bc0` → slot `0x1dfef8f88` → provider.** New fact: decoding
all 30 `__auth_stubs` in the exact 22F82 libquarantine (`A2D17FF6…`) shows
every slot targets the shared cache auth-pointer region
`0x1dfef8???`–`0x1dfef9b10` (permuted) — NOT the image's nominal
`__auth_got` @`0x1dd78c578` (zero hole, spans past extraction EOF). So the
missing bytes are the cache's bind-applied auth table; re-extracting the
image's auth_got cannot close it. Minimal artifact: **8 bytes at cache VA
`0x1dfef8f88`** (or the fixup record for that stub entry) from the Ventura
cache recorded in `metadata.json`; slot low bits then name provider
image/VA, and the device-side `libsystem_kernel` full copy (UUID
`34A49B54…`) settles whether it is `__sandbox_ms`/`__mac_syscall` or
reroutes one hop.

## Verified FACT (re-derived this audit, exact image)

- helper `0x18b320b2c` request: `{pid=val[5], pidversion=val[7], out*,
  flag*, x3}` at sp+0x10; `x0="Quarantine"` (bytes @0x2e0b re-verified);
  `movz w1,#182` (`0x528016c1`); `bl 0x18b321bc0`
- getter/helper propagate w0 verbatim; own sweep of full `__TEXT` found
  zero `movz #0x67` → errno=103 produced in the unresolved provider/deeper,
  not synthesized by libquarantine

## Preserved open distinctions

- Kernel 103 source: (a) `mac_proc_check_mac_syscall` hook return /
  (b) post-scan default :1587 no-match / (c) matched "Quarantine" handler
  returning ENOPOLICY — all live; frozen policy-snapshot v3 discriminates
  (b) only.
- Historical #103 ↔ empty-dict `count=0` ↔ `sls_rc=1002` remains
  msgID-correlated for WS 48891→tccd 48750 only; WS-10447 has no tccd
  window; empty reply ≠ policy denial.
- Rejected: launchd-hosting-sufficient (R006), token-rewrite-by-interpose
  (R005 §3.2), 15.6 selector/provider promoted to 22F82 fact.

(handover to codex now)
