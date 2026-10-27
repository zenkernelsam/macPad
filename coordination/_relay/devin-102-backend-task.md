# D102 backend — exact token identity validation upstream

Offline only on pinned tccd_22F82_live. Follow D101's getter success path
through TCCDProcess allocation/identity initialization. Map exactly which
requesting/accessing token is passed to the getter, which returned token is
used afterward, and how PID, pidversion, audit-session and path identities
are validated (or not). Preserve raw disassembly and any ABI gaps. Resolve
whether a nil identity is a distinct earlier/later failure producer rather
than conflating it with observed getter #103. Reconcile with D060 path-first
ordering. Do not infer provider semantics from the caller or invoke setters.
No device/probes/TCC/KRW/build/source edits/Git. Save D102 evidence/result
and end with (handover to codex now).
