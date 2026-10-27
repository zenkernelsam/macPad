# D111 backend — complete executable corrected-helper acceptance contract

D110 reproduced frozen-helper hazards but omitted its requested corrected
helper acceptance contract. Complete it with task-local reusable tests and
documented helper adapter interface under analysis/d111-trace-contract/.
Do not inspect moving frontend D109 or modify its artifacts.

Cover errno preserved on absent sentinel, successful emit and every failure;
regular-file type/owner/mode validation, symlink/FIFO/directory/wrong-owner
rejection; partial writes and dropped record observability; cleanup and
subprocess deadlines. Ensure no diagnostic filesystem I/O executes under
pace_lock: define how an eventual candidate proves this (instrumented
local lock-context callback or explicit reviewed call graph), do not confuse
O_NONBLOCK on regular files with a wall-time bound. Include a healthy write
control and verify caller result/output remains unchanged. No fabricated
candidate PASS: keep adapter-needed cases explicitly pending.

Bound all harness executions; task-owned paths only, no production markers,
device/TCC/KRW/source/build/deploy/restart/Git. Local tiny harness compilation
allowed. Return concrete files, commands/results and explicit handover.
