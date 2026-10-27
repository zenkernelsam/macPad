# D121 frontend — minimal evidence plan for the unresolved responsibility provider

D119 independently confirms the cache slot VA is inside the Ventura arm64e
cache map's RW mapping but outside every listed image segment. The map does
not supply mappingInfos/file offsets or chained-fixup semantics; the full
exact libsystem_kernel image is device-only. R013 already rejects using the
8 slot bytes alone as a resolved PAC pointer. Do not repeat a broad cache
search or make a policy/TCC attribution.

Read-only task; do not edit production or D113 transport/backend files. No
SSH/device operations, KRW, kernel snapshot, TCC setters, GUI start/retry,
build, deploy, trustcache, restart, or Git operations.

1. Cross-check D119's exact artifact inventory against R013, R025, R009,
   D117 and local metadata. Resolve only contradictions or missing locally
   available evidence; cite paths/hashes/commands and exact identities.
2. Produce the smallest acquisition checklist that can resolve the provider
   slot: cache header mappingInfos or equivalent exact VA-to-file/subcache
   ownership, chained-fixup/bind record for the slot, exact slot bytes, and
   exact target image identity/disassembly if it resolves into libsystem_kernel.
   Clearly distinguish a live slid-VA read from file bytes and state what each
   proves. Do not request broad dumps or privileged mutation.
3. Tie the checklist to the current boot/rootfs evidence without claiming the
   GUI-start exit-1 is caused by TCC/responsibility. Label each statement as
   FACT, calculation, THEORY, or missing evidence.
4. Save one concise dated evidence note and relay result; stop with a separate
   final line `(handover to codex now)`.
