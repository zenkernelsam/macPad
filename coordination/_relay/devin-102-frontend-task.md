# D102 frontend — pacing thread and lock ownership evidence

Offline only: use D101's exact services-loop receive/send chain, current
macws_coexist_wait_for_completion_slot callers, and existing preserved WS
samples/crash stacks to identify the pacing thread(s) and lock ownership.
Can any saved stack or exact call chain establish that the service loop
executes pacing, or waits on a lock held across pacing? Preserve the exact
stack lines and source/disassembly citations. Separate historical generation
stacks from current generation10447; no missing stack means absent behavior.
Do not label client-send-to-receive-return as pure kernel queue residence;
it also includes IPC/scheduling. Identify precise missing observation for a
future one-variable measurement. No device/instrumentation/source edits/
build/restart/Git. Save D102 result and explicit handover.
