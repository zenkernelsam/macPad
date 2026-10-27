# D105 — correlate later WS/tccd transactions, not only R005–R007

Offline saved-runtime evidence audit only. D104 establishes exact PostEvent
system routing and explains the old R005/R006 transport failures; its R007
correlation gap does not automatically apply to later D041/D050/D060 logs.
Find the latest verbatim runtime responsibility getter (#103) records and
extract source artifact, timestamp/message ID, sender/target PID, WS/tccd
generation, request service/function and reply type/key presence when saved.
Separate actual WindowServer-originated requests from disposable probes and
independent sessions. Never infer same-message correlation from same window.

Answer narrowly: is any post-remap WS PostEvent request causally tied to P0
getter failure, or only probes? Preserve exact excerpts and missing links.
Do not erase observed runtime #103 by calling it universally hypothetical;
do not promote an empty dictionary to explicit policy denial. No device,
probes/TCC/setters/KRW/source/build/Git. Save D105 result + explicit handover.
