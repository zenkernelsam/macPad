# D107 frontend — concrete offline pacing diagnostic candidate

Produce a reviewable diagnostic candidate, not another summary. Read the
accepted D088/D090 sentinel plan and independent-pacing-quarantine-review
20261019. Create only an isolated unified patch plus small trace-consumer
prototype under analysis/d107-pacing-candidate/; do not modify production
files. Patch must apply to the current frozen source fingerprint.

Include bounded dedicated trace, default-off diagnostic registration,
post-launch arming acknowledgement and existing <=1Hz arm checks. Record
monotonic timestamps, PID/TID, wait entry/deadline/exit, wake reason, and
sequence. No return changes, fake success, umbrella diagnostics or pacing
adaptation. Reuse current wake implementation and diagnostic registry.
Supply an exact-source diff plus focused parser verification with missing,
reordered and dropped-record handling. Label the candidate diagnostic-only;
no build/device/runtime acceptance claimed.

Correct D102 measurement: recv-return -> dispatch cannot measure time the
request waited before recv-return while pacing occupied the service thread.
Define separate same-clock client-send boundary, pacing interval and actual
recv/dispatch/reply-send linkage; report IPC/scheduling residual, not pure
queue delay. Do not invent kernel enqueue timestamps or hook unknown ABI.
Document remaining exact-image attachment gaps without implementing unsafe
hooks. No device, deployment, trustcache, WS restart, production edits/Git.
End explicit (handover to codex now).
