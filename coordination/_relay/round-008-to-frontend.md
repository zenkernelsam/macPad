# Frontend R008 — analyze the located libquarantine implementation

Read reviews/R007-frontend-R004-backend-20261018.md and backend R004 evidence. Actual Ventura getter is /usr/lib/system/libquarantine.dylib UUID A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3, symbol base+0x1b00. Search analysis/r004-responsibility and existing exact22F82 caches/extracts for this image, not libSystem/kernel stubs. Analyze the implementation and its direct callee closure to the real lookup boundary; preserve full bytes/disassembly, UUID/hash, arguments/return/errno, actual selector or service name if present. Identify the concrete branch producing103. Do not equate missing export with platform entitlement gating or103 with absent kernel policy without code.

If a necessary image/range is missing locally, produce an exact minimal extraction manifest (owner path, cache UUID/mapped range/subcache and bytes needed) in coordination/_relay/round-008-required-inputs.json, then hand over; no repeated complete-cache-copy suggestion or guessed setter. Existing helper call success/failure is not ABI proof beyond the verified caller.

Also distinguish native-iOS tccd import metadata from Ventura tccd metadata in prior report. Propose the narrowest upstream correction and exact discriminating experiment only if binary evidence supports it. Read-only offline work; no SSH/device, shared source/tests/build inputs, builds or Git. Backend owns current device observations. Write own evidence and coordination/_relay/round-008-frontend-result.md. Last line:
(handover to codex now)
