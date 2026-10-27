# D104 — token-bearing connection helper and actual WS call-site linkage

Offline exact binaries and saved logs only. Follow D103 R4: disassemble
_tccd at TCC_22F82 0x1850c9584 and its connection-cache/helper callers.
Determine what the second token-pointer argument actually controls: endpoint
selection, target credentials, cache keys, another purpose, or unused.
Do not infer kernel sender identity can be changed by a declared target.

Link this to the exact SkyLight PostEvent checker call (D043/D045 evidence)
and existing WS runtime artifacts showing gCurMsgAudit/argument registers.
Identify whether saved evidence establishes a non-null token passed to the
particular getter failure, or only a static potential route. Keep message
construction distinct from live emission and preserve full UUID/hash/raw
instruction excerpts for new findings. Do not repeat broad Indirect API map.

Clarify D103 len!=0x20: fallback alone is not a standalone failure producer;
only a subsequent failed attribution can explain an empty reply.
No device/probes/KRW/TCC/setters/token changes/source/build/Git operations.
Save D104 evidence/result and explicit (handover to codex now).
