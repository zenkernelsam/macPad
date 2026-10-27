# D112 — harden acceptance framework, not reference-only success

Review D111 reference/contract weaknesses and improve isolated framework:
pre-open lstat followed by open is TOCTOU; require O_NOFOLLOW/nonblocking
open plus post-open fstat regular-type/uid/mode validation, and test path
replacement around validation deterministically via injectable open hook.
O_NONBLOCK does not bound regular-file filesystem latency: document limit.
Check drop marker reliability when writes fail; a short-write suffix must not
be treated as a complete valid record. Ensure no false PASS from a reference
adapter claiming callback events while actual syscalls bypass probes.

Adversarial negative adapter must deliberately omit errno restore, follow
symlink, perform I/O in lock, and ignore partial writes; contract should fail
these independently with bounded subprocess execution. Keep candidate results
PENDING. Add timeout to concurrent harness patterns (do not touch frontend
moving files/processes). No device/production/source/deploy/Git operations;
local task-only tests allowed. Save concrete D112 results and handover.
