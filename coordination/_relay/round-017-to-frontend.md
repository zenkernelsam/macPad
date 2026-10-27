# Frontend R017 — local-only evidence audit for the XPC bootstrap call

Read R010's crash result and `reviews/R010-backend-R016-frontend-20261018.md`.
Search existing local sources, binaries, crash reports and tool artifacts for
the exact 22F82 `libxpc.dylib` identity, `_xpc_bootstrap_services` entry,
prototype/call convention and any successful pre-R012 call evidence. Distinguish
exact 22F82 artifacts from 15.6 symbols and comments. Trace the current
`loadImageCallback` call's trigger scope and data construction from source;
do not infer the callee ABI from the name or from a different OS build.

Report either the exact identity-matched ABI evidence or the precise missing
artifact/register/disassembly inputs that Backend R011 must gather. Treat the
PAC/ABI explanation as THEORY unless corroborated by exact instructions and
runtime registers. Do not propose a constant return, blind role gate, or
removal of the setup call as a fix. No SSH/device, source/test/build edits,
build, dependencies or Git writes. Write only a dated evidence note and
`coordination/_relay/round-017-frontend-result.md`; last line exactly
`(handover to codex now)`.
