# D100 frontend — exact service dispatch timing boundaries

Offline only: use exact analysis/SkyLight_22F82 and existing symbols/RE to
identify a read-only ordinary service request's receive/dispatch/reply
boundaries that could later correlate with the existing pacing wait.
Prefer an existing query (not input injection); establish exact routine/msgid,
thread/queue and reply linkage where the binary supports them. Save raw
disassembly with UUID/offsets and explain where a bounded future timing
observation would go. Do not claim a request's client elapsed time measures
server queue delay. If imported receiver/dispatcher binaries are missing,
name the exact missing input rather than guessing the private ABI.

Do not repeat screenshot inventory or implement another reopen path.
No device, source edits, instrumentation, build/deploy/restart, or Git writes.
Save D100 evidence/result and end with an independent line:
(handover to codex now)
