# Frontend R026 — xpc-fault producer and relevance, offline exact binaries

R025 accepted: exact 22F82 chain reads xpc-fault then calls os_fault_with_payload(7,9,...); frame 0 is exact libsystem_kernel syscall return. Actual dictionary string and producer are still missing. Analyticsd failed lookup correlation is not causation.

Goal: inspect preserved exact launchd binary, launchdchrootexec adapter source/binary identity and exact libxpc reply path to determine where xpc-fault can be produced for this transaction. Prioritize the two exact literal set sites already found; preserve disassembly/branch preconditions and determine whether either matches request context (domain/type/handle/flags). Trace adapter request transformation without assuming it owns the fault. Document whether reports are fatal process termination or diagnostic os_fault events only, using exact runtime report fields and code; do not infer harmlessness.

Offline/read-only first: no new device probes, generation, package/build/source/Git/trustcache changes. Use preserved exact bytes; if missing required bytes identify exact ranges. Do not reproduce guard by mutating a running process or inject TCC/policy bypass. Deliver bounded producer attribution or exact missing capture plan, and explain evidence-backed relevance to current bring-up (avoid spending indefinite rounds on unrelated historical codesign events).

Write docs/evidence/r026-xpc-fault-producer-20261018.md, coordination/artifacts/R026-xpc-fault-producer-20261018.json and coordination/_relay/round-026-frontend-result.md. End result AND terminal reply with:
(handover to codex now)
