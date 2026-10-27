# D106 backend result — libquarantine getter request/errno trace

Offline exact-image audit. Doc: `docs/evidence/devin-d106-backend-20261019.md`;
raw disasm: `analysis/libquarantine-d106/getter-…-stub-0x18b321bc0.txt`
(sha256 `9b9c0ba6…`).

## Established on the exact 22F82 image

- Image: `analysis/r005-responsibility-inputs/libquarantine.dylib`, sha256
  `f5989c8f…`, UUID `A2D17FF6-…` — re-verified; matches R004 live `dladdr`.
  (The `device-leftovers` copy is 15.6 `8CE8DAD0` — version-adjacent only.)
- Request: getter copies caller's 32B token to stack; helper extracts
  **pid (val[5]) + pidversion (val[7])** into an arg struct
  `{pid, pidversion, out-token-ptr, flag-ptr, x3}`; `x0="Quarantine"`
  (fileoff `0x2e0b`, bytes verified), `w1=0xb6`, `bl 0x18b321bc0`.
  Selector `0xb6` CONFIRMED. Output token is caller-owned, filled on success.
- Errno: **propagation, not synthesis** — getter/helper return the stub's `w0`
  verbatim; zero `__error` calls and zero `mov w*,#0x67` anywhere in `__TEXT`
  (only 0x22/0x16/0xc in other wrappers; the adjacent `get_responsible_for_pid`
  DOES synthesize ERANGE via `__error` — machinery exists, unused on the 0xb6
  path). So `#103` is produced inside the provider, propagated out.

## Explicitly unresolved — no guessing

- Stub `0x18b321bc0` is `adrp/add/ldr/braa` through GOT slot `0x1dfef8f88`,
  which lies outside every sparse segment; no fixup records survive.
  Provider name unresolvable from this image — `__mac_syscall` stays
  version-adjacent THEORY (15.6 shows `___sandbox_ms`≡`___mac_syscall`).
- Even under that theory, three ENOPOLICY kernel branches remain possible
  (precheck veto / post-scan no-match / matched-handler ENOPOLICY). Only the
  frozen v3 `mac_policy_list` snapshot discriminates the no-match branch.

## D105 preserved

msgid correlation (48891.17/.19 → #103 → count=0 → 1002) stands as fact for
those targets; WS 10447 first-miss remains unobserved. Provenance verified:
`R003-tccd-key-chains` is a labeled 74-line excerpt of a device-resident 13MB
oslog (not locally re-verifiable); D105 quoted it verbatim with its stated
limits.

(handover to codex now)
