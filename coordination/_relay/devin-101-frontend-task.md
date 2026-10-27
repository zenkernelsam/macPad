# D101 frontend — actual deferred reply send and pacing relation

Continue exact SkyLight D100 analysis offline. Trace _push_out_message
0x1854214fc and all direct/indirect callers, identifying when the small
0x74a7 reply leaves _gOutMsg and reaches mach_msg. Preserve instruction bytes
and UUID/offsets. Determine whether existing code establishes any relationship
between flush, the server event loop and the frame completion/pacing path.

Correct timing labels: rendezvous entry to impl is handler prelude, not
time spent queued before entry; _CGXPostReplyMessage enqueue is not send.
Identify a complete per-request receipt/dispatch/enqueue/actual-send chain,
or exact remaining gaps. No runtime causal conclusions from static ordering.
No device/input/probes/source edits/builds/restarts/Git. End result with
(handover to codex now).
