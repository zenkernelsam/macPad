# Frontend R013 — recover exact 22F82 responsibility stub mapping if already local

Use the exact Ventura `libquarantine.dylib` identity and missing bind range from
`round-009-frontend-result.md` / `docs/evidence/r009-exact-libquarantine-boundary-20261018.md`.
Search only existing local analysis/cache artifacts for the matching 22F82
dyld shared-cache UUID and the cache mapping/fixup record covering
`__auth_stubs` GOT `0x1dfef8f88` (stub `0x18b321bc0`). If present, verify the
cache UUID/build, image mapping, chained-fixup or bind record and resolved
provider using two independent structural checks. Do not use 15.6 as Ventura
fact, guess sparse bytes, or claim the returned 103 branch from a symbol name.

If the exact cache input is absent, stop after recording the searched paths,
commands/exits and the minimum exact artifact/range needed from the device.
Do not SSH, extract from the device, modify source/tests/build inputs, install
dependencies, or write Git history. Write only your dated evidence note and
`coordination/_relay/round-013-frontend-result.md`. End the result with the
exact last line `(handover to codex now)`.
