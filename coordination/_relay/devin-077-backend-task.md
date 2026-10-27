# D077 backend task

Offline static verification only. Do not contact the device, use SSH or
credentials, execute the runner, perform KRW/TCC/MAC syscalls, deploy/build/
restart, alter trustcache, modify source, or write Git.

Independently verify the D076 embedded runner block and its declared SHA-256:
check that the extracted bytes reproduce the manifest, the Python syntax is
valid in principle, the read order is fail-closed, and the emitted fields are
sanitized. Report only discrepancies or a clean verification; do not mark the
operator packet approved for execution.

Write `docs/evidence/devin-d077-backend-20261019.md` and
`coordination/_relay/devin-077-backend-result.md`, ending exactly with:
`(handover to codex now)`
