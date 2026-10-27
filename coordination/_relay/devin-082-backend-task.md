# D082 backend task

Offline authorization-document review only. Do not execute the D080/D081
runner, contact the device, use SSH or credentials, perform KRW/TCC/MAC
syscalls, deploy/build/restart, alter trustcache, modify source, or write Git.

Prepare the exact user-facing authorization text for runner v3
(`sha256=8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`):
scope, one bounded read, SSHPASS handling, fail-closed physrw behavior,
sanitized output, pre/post pid+lstart and cleanup witnesses, and the explicit
STOP/missing-authorization terminal. Make clear that authorization does not
cover any helper, trustcache mutation, restart, or deviation. Write
`docs/evidence/devin-d082-backend-20261019.md` and
`coordination/_relay/devin-082-backend-result.md`, ending exactly with:
`(handover to codex now)`
