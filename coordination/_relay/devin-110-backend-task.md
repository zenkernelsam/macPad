# D110 backend — executable diagnostic noninterference counterexamples

D109 frozen-D107 review found errno contamination and blocking trace I/O.
Create small independent verification artifacts under analysis/d110-trace-review/
using the extracted frozen D107 helper, without reading moving frontend D109.
Demonstrate saved errno before/after: absent sentinel, unwritable trace,
successful trace; demonstrate FIFO/no-reader and symlink/directory behavior.
Use only task-owned temporary paths, bounded subprocess timeouts and cleanup;
no production /tmp marker paths, no real app/WS injection or runtime hook.
Never let a FIFO test block the coordinator or create orphan processes.

Define a diagnostic-only corrected helper acceptance contract preserving
caller errno, rejecting nonregular/wrong-owner/symlink targets, preventing
I/O while pace_lock is held, and exposing missing/lost-record state. State
that O_NONBLOCK on a regular file does not guarantee bounded filesystem
latency. No production edits/build/deploy/device/KRW/TCC/Git. A tiny local C
test-harness compile is allowed only within analysis/task scratch and is not
a libmachook or device build. Return exact commands/results + handover.
