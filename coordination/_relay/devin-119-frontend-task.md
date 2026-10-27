# D119 frontend — independent audit of D117 cache-slot conclusion

Read `docs/evidence/devin-d117-frontend-20261019.md` and compare it with
`docs/evidence/r013-quarantine-stub-cache-search-20261018.md`,
`docs/evidence/r009-exact-libquarantine-boundary-20261018.md`,
`analysis/r005-responsibility-inputs/metadata.json`, and all exact local
dyld-cache/image inputs. Read-only; do not edit D116 transport or backend
files.

Audit these claims independently:

1. Does decoding all 30 22F82 `__auth_stubs` plus existing cache UUID/slide
   evidence actually prove their target VAs are in a mapped shared-cache
   auth-pointer region, or only that they fall outside sparse image
   segments? Identify the exact metadata needed to prove file/subcache
   ownership and fixup semantics.
2. Verify the D117 assertion that R005's `device_full` libsystem_kernel
   copy is available locally. Hash/UUID it if present; do not infer from
   `metadata.json` alone.
3. Check whether any existing cache header, mapping table, `.01`, `.a2s`,
   `.map`, or fixup artifact was missed. Do not retrieve from the device.
4. Separate facts, calculations, and theories. In particular, do not call
   the 8-byte slot sufficient until chained-fixup/PAC decoding and target
   image identity are explained.

No device/SSH, build, source changes, Git or kernel/TCC operations. Return
one concise evidence report with exact paths, hashes, commands/exits and
the smallest justified next artifact. Keep all responsibility/policy
attributions open where evidence is missing. End with a separate line:
`(handover to codex now)`.
