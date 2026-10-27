# D078 backend task

Offline documentation-only correction. Do not contact the device, use SSH or
credentials, execute the runner, perform KRW/TCC/MAC syscalls, deploy/build/
restart, alter trustcache, modify production source, or write Git.

Revise the D076 runbook as runner v2 using the D077 findings: load libxpc by
the verified absolute install path (or an equivalently justified mapped
handle), use exported `kread32` for every 4-byte-aligned mac_policy_list
header field, keep `kread64` only for naturally aligned reads, re-extract and
verify the embedded script hash, and mark the new packet as NOT APPROVED and
requiring a new authorization cycle. Record any remaining defects.

Write `docs/evidence/devin-d078-backend-20261019.md` and
`coordination/_relay/devin-078-backend-result.md`, ending exactly with:
`(handover to codex now)`
