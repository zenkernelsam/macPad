# Frontend R021 result — fixed provider Identifier acceptance checklist

Read R020 and the current publisher/package contract. Read-only: no `-I`
implementation, no build/test/package/install, no SSH, no source/test/build
edit, no Git write.

Evidence:
`docs/evidence/r021-provider-identifier-acceptance-plan-20261018.md`
(SHA-256 `d2f4622cc417d78cf46b45f41b9ba2d6812669bf1b51b3dfdad277fe1a08932c`).

## Prepared checklist (future implementation)

1. **Exact invocation:** both publisher passes become
   `ldid -ICydiaSubstrate -S<entitlements.plist> -M <temporary>`; temp name,
   lipo, CDHash derivation, trust ordering and `os.replace` stay unchanged.
2. **Per-arch Identifier:** run `ldid -arch arm64 -h` and
   `ldid -arch arm64e -h`; require exactly one `Identifier=CydiaSubstrate`
   and one `CDHash=` per slice, distinct CDHashes, and no
   `.CydiaSubstrate.new-*`. A missing/duplicate Identifier or a check that
   only reads unfiltered fat output fails closed. Checks run before trust.
3. **Separation of fields:** Identifier is verified independently from
   `LC_UUID` (`B9199E06-…` arm64, `A755DD35-…` arm64e), exact
   `LC_ID_DYLIB`, and the two trustcache CDHashes. A matching CDHash alone is
   not Identifier acceptance.
4. **Two-name/PID stability:** unit test asserts `-I` in both signing calls;
   host integration signs the same staged companions as two different
   `.CydiaSubstrate.new-<pid>` names and requires identical final SHA-256 and
   identical per-arch CDHashes, with the no-`-I` control proving the test is
   sensitive. Publisher-level two-PID runs must produce identical targets.
   R020's concrete hashes are build-specific and not a universal gate.
5. **Archive coverage:** `macws_provider_contract.py` is in
   `PACKAGE_PATHS`/`SOURCE_PAYLOADS`; `verify-package --root` must prove the
   archive contains the corrected script. The `.deb` SHA changes once and is
   recorded. The archive contract does not inspect Identifier; that evidence
   comes from publisher and tests. Provider payload hashes are not pinned
   across a rebuild; arch/platform/minos, install name, UUIDs and structural
   bundle contract are.
6. **Failure matrix:** wrong Identifier on arm64 only, arm64e only, both,
   missing, duplicate, or CDHash-correct-but-Identifier-wrong must each raise
   `ProviderError`/exit 1 before `jbctl`, leave the old target bytes/inode
   untouched, add no CDHash, and remove the temp file. No fallback re-sign,
   warning-and-continue or env override. A post-replace defensive failure is
   nonzero and never a success report.

Device acceptance remains: both slices `Identifier=CydiaSubstrate`, unchanged
UUIDs/install name, both CDHashes live, both provider probes
`INSTALLED-ORIGINAL-CALLABLE`, no temp file. The WindowServer provider map is
still a separate endpoint blocked by the unrelated XPC PAC fault.

## Limits

No implementation or execution was performed. R010's missing WS map and the
arm64-only static provider slice remain open. Host ldid is `2.1.5-procursus7`;
device basename behavior is already FACT from R010.

(handover to codex now)
