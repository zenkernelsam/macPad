# Frontend R018 — offline audit of existing arm64e pointer-signing contracts

Read R017 and the exact current source around `MSFindSymbol` and the XPC bootstrap call. Search existing local source, headers, runtime helpers and evidence for established arm64e function-pointer signing patterns used successfully by this project. Compare those patterns with `blraaz x19` and the raw pointer returned by `MSFindSymbol`.

Distinguish source-confirmed signing contracts, unrelated data-pointer authentication, and missing exact evidence. Do not propose or implement a fix, add a role gate, remove the setup call, return a constant, or infer the 22F82 callee ABI from 15.6. No device/SSH, source/test/build edits, dependencies or Git writes. Write only a dated evidence note and `coordination/_relay/round-018-frontend-result.md`; end exactly `(handover to codex now)`.
