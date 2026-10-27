# D084 backend task

Offline documentation-only correction. Do not execute runner v3, contact the
device, use SSH or credentials, perform KRW/TCC/MAC syscalls, deploy/build/
restart, alter trustcache, modify source, or write Git.

Revise D082's user-facing runner-v3 authorization text using D083 frontend
findings: pin device dependency identities and SSH principal, define arming
scope and process-exit/run timeout, state stderr/host-key/SSHPASS exposure,
strengthen pid+lstart/sequence/panic and completed-walk witnesses, declare
one-attempt semantics and the userspace command whitelist, and name the
operator and explicit no-injection/no-capture/sentinel exclusions. Keep the
runner bytes and hash unchanged and the packet NOT APPROVED.

Write `docs/evidence/devin-d084-backend-20261019.md` and
`coordination/_relay/devin-084-backend-result.md`, ending exactly with:
`(handover to codex now)`
