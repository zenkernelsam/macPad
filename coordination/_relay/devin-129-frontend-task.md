# D129 frontend — executable timing plan for frame-wait vs service-message blocking

Read-only design audit; no source/build/device/SSH/Git/TCC/KRW/restart. Start
from D094, D100-D102, D107-D113, and the exact current source at
`macws_coexist_wait_for_completion_slot` plus the exact SkyLight 22F82
receive/dispatch/reply offsets/UUID already recorded.

The goal is one causal runtime result: did a particular WindowServer service
request arrive while the manual/coexist frame-completion wait was active, and
how long until its same-generation handler dispatch/reply? Do not infer this
from client round-trip time, timer prelude time, a spindump from another
generation, or the existence of an interaction wake socket.

1. Verify the owner/thread relationship between the pacing wait and SkyLight
service receive loop. If they are different threads, identify the actual
serialized resource/lock or event-loop boundary that could block messages;
do not assume thread coupling.
2. Specify the smallest default-off, record-only timing points and fixed-size
record fields to correlate (generation, thread, request id or auditable
sequence, wait-enter/exit, receive/dequeue, handler dispatch, reply send).
Distinguish message arrival from kernel/libdispatch wake where userland cannot
observe arrival. Use a bounded buffer with no disk I/O in the measured region.
3. Define controls: no-wait baseline and one bounded wait interval; same exact
WS UUID/build, workload, endpoint and thermal state; no Mach-port polling
change in this task. Give a threshold/statistic that decides whether a wake
adaptation is warranted and the negative result that keeps current behavior.
4. Reconcile whether D113 transport is suitable as an offline harness only;
it remains PENDING and must not be integrated as a product path. Identify
exact missing runtime access/artifact if the arrival edge is unobservable.
5. Save a concise design note and relay result, with FACT/THEORY/MISSING
labels. Finish with `(handover to codex now)` on a separate line.
