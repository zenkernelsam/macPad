# D087 backend task

Offline final audit only. Do not execute runner v3, contact the device, use
SSH or credentials, perform KRW/TCC/MAC syscalls, deploy/build/restart, alter
trustcache, modify source, or write Git.

Audit D086's revision-3 authorization text against the frozen runner hash and
the D085 residuals. Check exact command whitelist/path ordering, one-attempt
semantics, process/panic/residue witnesses, and no-capture/no-sentinel
exclusions. Report only concrete residuals; do not execute or approve.

Write `docs/evidence/devin-d087-backend-20261019.md` and
`coordination/_relay/devin-087-backend-result.md`, ending exactly with:
`(handover to codex now)`
