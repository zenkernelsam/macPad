# D066 backend result — getter discriminator design

## Result

Consolidated the verified R004/R009 getter ABI, corrected the D065
overclaim, and delivered two safe discriminators that separate the three
ENOPOLICY producers. Zero device contact; no syscalls, requests, setters,
deploys, or scans beyond targeted lookups.

## Corrections applied

- **"Unreachable by construction" retracted**: the system endpoint exists
  and is live (`com.apple.macosbooter.tccd.system`, active=1). Role logic
  rules out only the *user-domain* route for `isPerSystem` services; if the
  provider hypothesis were false, the current route could produce verdicts.
  Global input is blocked at the attribution stage on an existing endpoint —
  under the provider hypothesis, not by role absence.
- **Sandbox/0xb6 A/B withdrawn**: selector semantics are policy-specific;
  `0xb6` on a registered Sandbox policy is an unknown, potentially mutating
  operation. Same for alternate Quarantine selectors — safe only if the
  policy is absent, which is the question itself. Neither is read-only.

## What R004 already proved (reused, not repeated)

Live **native** process token (pid 51948, alive during call) and the chroot
helper's own token both returned `errno=103 "Policy not found"` through the
real Ventura getter. Chroot-namespace causes excluded; native launchd-tracked
identity does not help. Result is consistent with all three kernel arms —
precheck / no-match / handler — so producer separation requires a new
observation, not more tokens.

## Safe discriminators designed (not executed)

1. **KRW `mac_policy_list` enumeration** (preferred): the existing Dopamine
   `libjailbreak.dylib` `kread64/kread32` pattern (`misc/agx_iogpu_probe.c`)
   reads registered `mpc_name`s + `mpo_policy_syscall` presence directly —
   zero policy syscalls, answers all three arms at once. Missing input:
   `mac_policy_list` static address (targeted anchors identified: two
   `"mac_policy_register failed"` strings at kc_raw file offsets `0x34ef3e`/
   `0x832f84`, or syscall-381 via `sysent`) + KRW helper authorization.
2. **Calibrated no-match control**: `__mac_syscall("<guaranteed-unregistered
   name>", 0, NULL)` — cannot invoke a handler by construction; calibrates
   the default arm's errno on the live kernel. Missing input: helper deploy
   authorization.

Explicitly rejected: Sandbox-name probes, alternate Quarantine selectors,
more token-kind getter calls (R004 already covered native+chroot), and
`p_responsible_pid` userspace reads (no such flavor exists — D061/D064).

## Missing inputs recorded

- `mac_policy_list` address in `kc_raw` (targeted RE, anchors listed).
- Authorization for either helper (KRW read or nonsense-name probe).
- Exact 22F82 stub-provider name (GOT slot `0x1dfef8f88` outside extracted
  segments).

## Artifacts

- `docs/evidence/devin-d066-getter-discriminator-20261019.md` — full ABI
  consolidation, three-arm table, discriminator designs, missing inputs.

Desktop untouched throughout.

(handover to codex now)
