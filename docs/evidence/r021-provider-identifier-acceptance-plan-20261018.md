# R021 — fixed provider Identifier: dual-arch stability and failure acceptance checklist

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-021-to-frontend.md`. Read-only preparation only.
The `-I` change was **not** implemented, no build/test/package/install/SSH and
no Git write was performed. This note defines the checks a future publisher
correction must pass.

Inputs read: R020
(`docs/evidence/r020-provider-identifier-audit-20261018.md`),
`misc/macws_provider_contract.py` (`publish` at 370-421; `_ldid_cdhash` at
316-324; `_trust_hashes` at 327-342), `misc/macws_artifact_contract.py`
(`PACKAGE_PATHS` 25-54, `SOURCE_PAYLOADS` 57-92, `verify_package` 191-251),
`misc/test_substrate_package_contract.py` (publish tests 180-260, wiring tests
260-300), `misc/provider_test_fixtures.py`, `misc/macos_mshook_provider_probe.c`,
`layout/usr/macOS/bin/postinst.sh` (573-598, 204-216) and
`layout/DEBIAN/postinst` (516-550).

## 0. Fix shape under test (future, not implemented here)

Only `misc/macws_provider_contract.py::publish` needs to change:

1. Add `-I` + `PROVIDER_NAME` (`CydiaSubstrate`) to **both** existing ldid
   passes on the temporary file. The exact argv becomes:

   ```text
   ldid -ICydiaSubstrate -S<entitlements.plist> -M <temporary>
   ldid -ICydiaSubstrate -S<entitlements.plist> -M <temporary>
   ```

   In Python list form:
   `[ldid, "-I" + PROVIDER_NAME, "-S%s" % entitlements, "-M", temporary]`.
2. After signing and **before** `_trust_hashes`, require exactly one
   `Identifier=CydiaSubstrate` for each of `arm64` and `arm64e`.
3. Fail closed with `ProviderError`/exit 1 on any identifier mismatch.
4. Keep the temp name, `lipo -create`, two-pass signing, CDHash derivation,
   trust admission ordering and `os.replace` unchanged.
5. Optionally re-run the identifier check on the final target after
   `os.replace` as a defensive assertion; the bytes do not change on rename.

Expected touched files for that future implementation: the publisher and its
focused tests (and the fake-ldid fixture). No provider source, provider
Makefile, postinst wrapper, archive contract or load-command change is
expected. `git diff --check` must exit 0; no new environment/runtime switch.

## 1. Identity fields — each check proves a different contract

| Field | Expected value / rule | Checked by | Must not be confused with |
| --- | --- | --- | --- |
| CodeDirectory `Identifier` | `CydiaSubstrate` for both slices | `ldid -arch <arch> -h` | `LC_UUID`, `LC_ID_DYLIB` |
| `LC_UUID` arm64 | `B9199E06-3CE6-3116-80DB-5AA8B9DED5A5` | `otool -l -arch arm64` | signature Identifier |
| `LC_UUID` arm64e | `A755DD35-D80C-3D90-9312-D566DE392770` | `otool -l -arch arm64e` | signature Identifier |
| `LC_ID_DYLIB` | `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` | `otool -l`, `verify_fat` | `Identifier=` |
| CDHash arm64 / arm64e | two distinct 40-hex hashes | `ldid -arch <arch> -h`, `jbctl trustcache info` | `LC_UUID` |
| Trustcache key | CDHash only | `jbctl trustcache add <hash>` | signature Identifier |

Changing the Identifier changes the CDHash. It does not change either
`LC_UUID`, the install name, or the rpath dependency. A fix must therefore
assert all four independently; a matching CDHash alone does not prove the
Identifier and vice versa.

## 2. Per-architecture Identifier acceptance

Run against the signed temporary file and (defensively) the final target:

```sh
ldid -arch arm64  -h "$TARGET" | grep -E '^(Identifier|CDHash)='
ldid -arch arm64e -h "$TARGET" | grep -E '^(Identifier|CDHash)='
```

PASS criteria, per architecture:

- exactly one `Identifier=CydiaSubstrate` line;
- exactly one `CDHash=` line;
- arm64 and arm64e CDHashes are different;
- no line matches `Identifier=.CydiaSubstrate.new-`;
- `otool -l -arch <arch>` still reports the expected `LC_UUID` and exact
  `LC_ID_DYLIB`.

FAIL criteria:

- missing Identifier line (treat unknown as failure, not as acceptable);
- more than one Identifier line for a single `-arch` invocation;
- either slice still reports `.CydiaSubstrate.new-<pid>`;
- only the arm64 or only the arm64e slice is checked;
- parsing `ldid -h` without `-arch` and accepting the first Identifier line
  (a wrong second slice could be missed).

The future verifier should use a helper shaped like `_ldid_cdhash`:
`[ldid, "-arch", arch, "-h", path]`, collect every `Identifier=` line,
require exactly one and require `PROVIDER_NAME`. Do not reuse a single
unfiltered fat-output match as the dual-arch gate.

## 3. Stability across two temporary names / PIDs

The publisher's temp name is `.{PROVIDER_NAME}.new-{os.getpid()}`
(`macws_provider_contract.py:389`). Acceptance must show the name no longer
enters the signed bytes.

### 3.1 Unit-level argv test (required)

The fake ldid must record every invocation. Assert both signing calls carry
`-ICydiaSubstrate` and the `-h` calls carry `-arch arm64` / `-arch arm64e`.
The existing fake only parses `-arch` and ignores other arguments, so it must
be extended before it can prove this.

### 3.2 Host integration test with real ldid (recommended)

Using the same staged `CydiaSubstrate.arm64` / `.arm64e` companions:

```sh
for pid in 33333 44444; do
  tmp="$DIR/.CydiaSubstrate.new-$pid"
  lipo -create CydiaSubstrate.arm64 CydiaSubstrate.arm64e -output "$tmp"
  ldid -ICydiaSubstrate -S"$ENT" -M "$tmp"
  ldid -ICydiaSubstrate -S"$ENT" -M "$tmp"
  ldid -arch arm64  -h "$tmp" | grep -E '^(Identifier|CDHash)='
  ldid -arch arm64e -h "$tmp" | grep -E '^(Identifier|CDHash)='
  shasum -a 256 "$tmp"
done
```

PASS: the two final SHA-256 values are identical, both per-arch CDHash pairs
are identical, and both report `Identifier=CydiaSubstrate`. Only the temp
filenames differ.

Control (sensitivity proof): the same two names **without** `-I` must produce
different Identifiers/hashes. R020 already observed this locally
(different identifier, CDHash and final SHA per temp name); with `-I` the two
runs collapsed to the same SHA/CDHash pair. R020's concrete values
(`7c3df88e…`, `b7bbd396…`, `0445c5c6…`) belong to that local build and must
not be hard-coded as universal expected values; compare run A to run B and
compare UUIDs/install names to their contracts instead.

### 3.3 Publisher-level two-PID test

Call `publish()` twice against two disposable rootfs copies with different
`os.getpid()` values (two subprocesses, or a patched getpid with real-ldid
integration). PASS: the two final target files have the same SHA-256 and the
same two CDHashes, both report `Identifier=CydiaSubstrate`, and no
`.CydiaSubstrate.new-*` remains after either run. The second trust admission
may observe the same hashes already live; membership, not a new hash value,
is the gate.

## 4. Package/archive contract coverage

Current coverage in `misc/macws_artifact_contract.py`:

- `PACKAGE_PATHS` includes the provider main, both thin companions, the
  publisher script and the publish wrapper.
- `SOURCE_PAYLOADS` maps `var/jb/usr/macOS/bin/macws_provider_contract.py`
  back to `misc/macws_provider_contract.py`.
- `verify_package` calls
  `provider_contract.verify_bundle(..., strict_slice_hashes=True)` before
  hashing, compares staged source payloads to the working tree when `--root`
  is supplied, compares every archived byte to staging, and checks the
  control-archive postinst.

Consequences and gates for the future fix:

- The publisher script bytes change, so the staged/archived script hash and
  the whole `.deb` SHA-256 change once. Record the new archive SHA in the
  implementation evidence; do not treat the old archive hash as a pass gate.
- `verify-package` must be run with `--root` so the source-payload comparison
  proves the archive contains the corrected publisher, not a stale copy.
- Provider main/companion bytes are not changed by this fix. If the package
  build reuses them, their hashes should stay stable; if the build regenerates
  the provider, do not pin old payload SHAs (build non-determinism is already
  visible between R010 and R012) — instead re-verify arch/platform/minos,
  exact `LC_ID_DYLIB`, the two UUIDs and the structural bundle contract.
- The archive contract does **not** inspect the code-signing Identifier and
  must not be claimed to do so. Identifier acceptance comes from the publisher
  `ldid -arch … -h` checks and the focused tests. The package's link-signed
  main/companions already report `Identifier=CydiaSubstrate`; that is not a
  substitute for checking the replaced target.

Suggested future commands (not run in R021):

```sh
python3 -m unittest -v misc.test_substrate_package_contract
python3 -m unittest -v misc.test_substrate_package_contract \
    misc.test_artifact_contract misc.test_cydiasubstrate_provider_contract \
    misc.test_app_input_route_contract
python3 misc/macws_provider_contract.py verify-build --binary <built>
python3 misc/macws_provider_contract.py verify-bundle --directory <staged>
python3 misc/macws_artifact_contract.py verify-package --root "$PWD" \
    --binary <MacWSWindowing> --manifest <manifest> \
    --package <deb> --staging <staging>
bash -n layout/usr/macOS/bin/macws_publish_substrate.sh
git diff --check
```

## 5. Failure behavior matrix

The following must hold **before** any `jbctl trustcache add` and before
`os.replace`:

| Injected condition | Required result |
| --- | --- |
| arm64 reports `.CydiaSubstrate.new-*`; arm64e correct | `ProviderError`; exit 1; no trust call; no replace; temp removed; old target bytes/inode unchanged |
| arm64e reports temp Identifier; arm64 correct | same |
| both slices temp Identifier | same |
| Identifier line missing for either arch | same (fail closed) |
| duplicate Identifier lines for one arch | same |
| correct CDHash but wrong Identifier | same — CDHash alone is not acceptance |
| `ldid` signing fails | existing failure behavior; temp removed; old target untouched |
| identifier valid, trust admission fails | existing `test_publish_trust_failure_keeps_old_target` behavior; old target untouched |
| valid pre-replace, post-replace defensive check fails | nonzero exit, no success report; investigate (should be unreachable because `os.replace` does not alter bytes) |

Additional requirements:

- Error text names the failing architecture and observed Identifier
  (without dumping entitlements).
- Identifier checks are ordered before `_trust_hashes`; a failed check must
  not add either CDHash.
- No fallback re-sign, no warning-and-continue, no environment override, no
  arch skipping.
- The `finally` cleanup still removes the temporary file on every path.
- No `.CydiaSubstrate.new-*` remains after success or failure.

## 6. Device acceptance (future backend, not this round)

1. Publish through the normal package/postinst path.
2. Read the target with `ldid -arch arm64|arm64e -h`: exactly one
   `Identifier=CydiaSubstrate` per slice; both CDHashes live in
   `jbctl trustcache info`.
3. Confirm UUIDs remain `B9199E06-…` / `A755DD35-…`, install name exact, and
   both slices platform 1 / minos 13.x.
4. Run both provider probes; require
   `VERDICT=INSTALLED-ORIGINAL-CALLABLE`, provider path
   `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`, and
   loaded UUID `A755DD35-…` for the arm64e probe.
5. Optional device stability witness: publish twice (different PIDs) and
   require identical final SHA/CDHash; if the second publish is skipped,
   the host A/B test in §3.2 supplies the stability evidence.
6. Require no `.CydiaSubstrate.new-*` file. Old trustcache hashes may remain
   live; that is not a failure.
7. The WindowServer loaded-provider map remains a separate endpoint: R010's
   generation failed at the unrelated `_xpc_bootstrap_services` PAC fault, so
   a successful WS map is still pending and is not created by this Identifier
   fix.

## 7. Limits / non-goals

- This round did not implement `-I`, did not edit the publisher or tests, did
  not build, package, install, SSH, or write Git.
- No new device evidence was collected; R010/R020 runtime facts are reused.
- R010's missing post-install WindowServer map and the arm64-only static
  provider slice remain open limits.
- The local ldid is host `2.1.5-procursus7`; device basename behavior is
  already FACT from R010. No exact dyld-1066.8 disassembly was used.
- No claim is made about pixels, TCC, input, or the separate XPC signing fix.

(handover to codex now)
