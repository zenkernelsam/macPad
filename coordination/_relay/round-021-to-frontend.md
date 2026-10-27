# Frontend R021 — prepare fixed provider Identifier acceptance checks

Read R020 and the current publisher/package contract. Produce a read-only
acceptance checklist for a future publisher Identifier correction: exact ldid
invocation(s), both arm64/arm64e CodeDirectory Identifier checks, expected
CDHash/SHA stability across two different temporary filenames/PIDs, archive
contract coverage, and failure behavior if either slice keeps a temporary
Identifier. Keep the distinction between Identifier, LC_UUID, LC_ID_DYLIB and
trustcache CDHash explicit. Do not implement the `-I` change, rebuild, install,
SSH, modify tests/source/build inputs or Git. Write only a dated evidence note
and `coordination/_relay/round-021-frontend-result.md`; end exactly
`(handover to codex now)`.
