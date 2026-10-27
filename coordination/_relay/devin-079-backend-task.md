# D079 backend task

Offline static ABI review only. Do not contact the device, use SSH or
credentials, execute the runner, perform KRW/TCC/MAC syscalls, deploy/build/
restart, alter trustcache, modify source, or write Git.

Review D078 runner v2's ctypes declarations and read alignment against the
already saved D069 helper/disassembly. Check only whether symbol names,
restypes/argtypes, `kread32`/`kread64` alignment, and the libxpc type check are
internally consistent. Record any remaining fail-closed defect; do not run or
approve the packet.

Write `docs/evidence/devin-d079-backend-20261019.md` and
`coordination/_relay/devin-079-backend-result.md`, ending exactly with:
`(handover to codex now)`
