# Backend R011 — read-only attribution of the R010 XPC bootstrap crash

Read `reviews/R010-backend-R016-frontend-20261018.md`, R010 result/evidence,
the exact source `libmachook/mac_hooks.m:6359-6366`, R007 activation audit,
and the current frontend R017 packet. The R010 device generation was cleaned
up and iOS services were healthy at handover. Start with a new read-only
inventory; do not assume that state remains current.

Goal: determine what the exact 22F82 `libxpc.dylib` symbol
`__xpc_bootstrap_services` expects and why the new call from
`loadImageCallback` correlates with the recorded tccd/authd/systemstatusd
crashes. Preserve exact cache/image UUIDs and hashes. Use existing read-only
device artifacts and extract only the exact image/ranges needed; preserve the
load command, symbol/export/fixup data, caller/callee disassembly, crash PC/LR
and available x0-x3/register arguments. Correlate the reported image UUID and
runtime slide. Compare the current function-pointer type and dictionary shape
against exact 22F82 evidence. If symbols or register state are absent from the
crash reports, state the exact additional bounded observation needed; do not
start a new process generation merely to manufacture a crash.

No source edits, builds, package install, launchd/service changes, WindowServer
start/restart, debugger attach to unrelated live generations, shared marker,
TCC policy/database/cache mutation, or compiler operation. Do not kill native
tccd/SpringBoard/backboardd. Do not label the fault PAC/ABI-confirmed from the
address alone. A role/identity gate, removing the setup call, or returning a
constant is not authorized as a diagnosis. Keep the R010 package installed
and preserve its baseline backups. Write only your dated evidence/artifact and
`coordination/_relay/round-011-backend-result.md`; finish with exact last line
`(handover to codex now)`.
