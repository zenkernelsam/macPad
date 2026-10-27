# Frontend R020 result — provider publisher Identifier audit

Read `coordination/_relay/round-020-to-frontend.md`, R010's provider/package
evidence, the current publisher, both postinst trust paths, boot-trust reader,
provider probes and focused tests. Read-only: no source/test/build/package
edit, no device/SSH/install, no Git write. Throwaway ldid experiments used
`/tmp/r020-*` only.

Evidence:
`docs/evidence/r020-provider-identifier-audit-20261018.md`
(SHA-256 `b2d82fe3e6cf189d21f1fd15e1984ae686ef6736e50baa8287bd0d7c021b97fc`).

## Verdict

The published temp Identifier is real but does **not** affect dyld load
identity, trustcache admission, dependency closure or the existing provider
probes in the available evidence:

- **dyld:** the R010 exact-22F82 probes loaded the provider with
  `Identifier=.CydiaSubstrate.new-67705`, resolved its symbols, and reported
  `INSTALLED-ORIGINAL-CALLABLE`.
- **trust:** both postinst paths and the boot-trust reader consume `CDHash=`
  only; R010's two CDHashes were live. `jbctl trustcache add` receives no
  identifier.
- **dependency:** `libmachook`'s `@rpath/CydiaSubstrate.framework/CydiaSubstrate`
  matched the provider's exact `LC_ID_DYLIB`; the probes' provider load proves
  closure succeeded.
- **probes:** `misc/macos_mshook_provider_probe.c` reads path, symbols and
  `LC_UUID`, never the Identifier.

Kept separate: UUIDs arm64 `B9199E06-3CE6-3116-80DB-5AA8B9DED5A5` / arm64e
`A755DD35-D80C-3D90-9312-D566DE392770`, exact install name, and the CDHash
pair versus the CodeDirectory `Identifier=` field.

## Real consequence (FACT, local reproduction)

The publisher names the temp `.{PROVIDER_NAME}.new-{pid}` and signs it with
two `ldid -S… -M` passes and no `-I`. ldid derives the Identifier from that
basename, so identical inputs published under different PIDs produce
different Identifiers, CDHashes and final SHA-256 values, and each install
adds a trustcache entry. The current local build is already linker-signed
`Identifier=CydiaSubstrate`; `-M` does not preserve it.

## Smallest future patch (not implemented)

1. Add `-ICydiaSubstrate` (i.e. `-I` + `PROVIDER_NAME`) to both existing
   publisher ldid passes.
2. Verify `Identifier=CydiaSubstrate` per arch before trust admission and
   fail closed otherwise.
3. Add fixture/negative tests; keep temp naming, lipo, trust and atomic
   `os.replace` unchanged.

Acceptance: two temp names give one identical final hash; device re-publish
shows `Identifier=CydiaSubstrate` for both slices, unchanged UUIDs, both new
CDHashes live, provider probe `INSTALLED-ORIGINAL-CALLABLE`, provider present
in the WindowServer map, no temp file left.

## Limits

R010 has no post-install WindowServer provider map (the generation failed at
the unrelated XPC PAC fault) and both probes selected arm64e. Local ldid is
host 2.1.5-procursus7; device basename behavior is already FACT from R010.
Exact 1066.8 dyld disassembly was not used; the 1286.10 source was structural
reference only.

(handover to codex now)
