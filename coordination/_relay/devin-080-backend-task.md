# D080 backend task

Offline documentation-only correction. Do not contact the device, use SSH or
credentials, execute the runner, perform KRW/TCC/MAC syscalls, deploy/build/
restart, alter trustcache, modify production source, or write Git.

Revise the D078 pinned runner as v3 using D079's required fix: compare
`xpc_get_type()` to the address of `_xpc_type_dictionary` via
`ctypes.addressof(ctypes.c_void_p.in_dll(...))`, not the type object's first
qword. Re-extract and verify the new embedded script hash, keep the packet
NOT APPROVED and requiring a new authorization cycle, and record any
remaining defects.

Write `docs/evidence/devin-d080-backend-20261019.md` and
`coordination/_relay/devin-080-backend-result.md`, ending exactly with:
`(handover to codex now)`
