# R020 — provider publisher Identifier: dyld / trust / dependency scope audit

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-020-to-frontend.md`. This is a read-only audit.
No publisher, package, source/test/build input, device, SSH, installation or
Git write was performed. The only writes were throwaway files under
`/tmp/r020-*` used to reproduce ldid's basename rule; they are not part of the
repository, the package or the device.

## 1. Finding under audit and exact sources

R010 recorded the published provider's code-signing field as:

```text
Identifier=.CydiaSubstrate.new-67705
CDHash=7df54d9c6155ab46943c9dda4ef92852498ab60a        (arm64)
Identifier=.CydiaSubstrate.new-67705
CDHash=464c571b1b08d7c467b7316aaeb46a41a8f9c7a0        (arm64e)
```

Sources read:

| Artifact | Observation |
| --- | --- |
| `docs/evidence/r010-merged-provider-f1-package-20261018.md:82-91,246-247` | Published target, UUIDs, CDHashes, live trustcache; Identifier called a "publisher temp basename, secondary finding". |
| `analysis/r010-provider-f1/postinstall-verify.txt:59-67` | Device `ldid -arch <arch> -h` output for the published target: temp Identifier + both CDHashes. |
| `analysis/r010-provider-f1/generation.txt:135-141` | Same published file re-read during the failed generation. |
| `analysis/r010-provider-f1/baseline-inventory.txt:27-32` | Pre-install provider had `Identifier=CydiaSubstrate`. |
| `misc/macws_provider_contract.py:389-408` | Publisher creates `.{PROVIDER_NAME}.new-{pid}`, runs `lipo -create`, then two `ldid -S<plist> -M` passes with **no `-I`**, derives CDHashes, trusts them, and `os.replace`s the temp over the target. |
| `misc/test_substrate_package_contract.py:197,237` | Tests only assert that no `.CydiaSubstrate.new-*` file remains; no Identifier assertion. |
| `misc/macos_mshook_provider_probe.c:100-107,178-202` | Probe uses `dladdr`, `dlsym` and the image `LC_UUID`; it never reads the code-signing Identifier. |
| `layout/usr/macOS/bin/postinst.sh:573-598,204-216` | Trust helpers derive `CDHash=` only from `ldid -arch … -h`; Identifier is not parsed. |
| `layout/DEBIAN/postinst:516-550` | Identifier checks exist for specific system binaries, not for the provider. |
| `layout/usr/macOS/bin/macws_boot_trust.py:287-336` | Cold-boot restore scans CodeDirectory hashes plus file identity, not Identifier. |

The publisher file audited has SHA-256
`6ee59ec1cbf60cd5b5eb7f7bdad9ae1d17c1debb2160f052901a7a0591161ae0`;
the R010 evidence file has SHA-256
`dd599f3294718dde3844979f6e002ecb0e239f89d5e664b0b6cd717091752edb`.

## 2. What the Identifier field actually is

**FACT (Mach-O/code-signing structure):** the code-signing Identifier is a
field inside the embedded CodeDirectory (`ldid -h` prints it as
`Identifier=…`). It is not a Mach-O load command. It is therefore distinct
from `LC_UUID`, `LC_ID_DYLIB` and the CDHash.

**FACT (local reproduction of the publisher's exact ldid invocation):**
on throwaway copies under `/tmp`, host ldid `2.1.5-procursus7`:

- a file named `CydiaSubstrate` keeps `Identifier=CydiaSubstrate`;
- the same bytes named `.CydiaSubstrate.new-11111` become
  `Identifier=.CydiaSubstrate.new-11111`;
- the same bytes named `.CydiaSubstrate.new-22222` become
  `Identifier=.CydiaSubstrate.new-22222`;
- an explicit `ldid -ICydiaSubstrate` on a differently named file restores
  `Identifier=CydiaSubstrate`.

The input already carried a linker-signed `Identifier=CydiaSubstrate`, so
`-M` did not preserve it; the basename decided it. This is the same behavior
the device publisher exhibited in R010.

## 3. Identity contracts kept separate

| Field | Exact value / contract | Changed by publisher re-sign? | Consumer |
| --- | --- | --- | --- |
| `LC_UUID` arm64 | `B9199E06-3CE6-3116-80DB-5AA8B9DED5A5` | No | dyld image identity, crash reporting, probe UUID pin |
| `LC_UUID` arm64e | `A755DD35-D80C-3D90-9312-D566DE392770` | No | same |
| `LC_ID_DYLIB` (both slices) | `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` | No | `@rpath/CydiaSubstrate.framework/CydiaSubstrate` dependency matching |
| CDHash arm64 (R010 published target) | `7df54d9c6155ab46943c9dda4ef92852498ab60a` | Yes | trustcache admission |
| CDHash arm64e (R010 published target) | `464c571b1b08d7c467b7316aaeb46a41a8f9c7a0` | Yes | trustcache admission |
| CodeDirectory Identifier | `.CydiaSubstrate.new-67705` | Yes (temp basename) | no consumer found in load/trust/dependency paths |

The R010 device evidence already showed that the published target's UUIDs
matched the package companions and that both slices kept the exact install
name after re-signing. The Identifier is a fourth, independent field.

## 4. Audit answers

### 4.1 dyld load identity — no demonstrated effect

**FACT (runtime, exact 22F82 chroot):** the published provider with
`Identifier=.CydiaSubstrate.new-67705` loaded successfully in the R010
post-install probes. The probe reported:

```text
provider-image=/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate
provider-uuid=a7 55 dd 35-d8 0c-3d 90-93 12-d5 66 de 39 27 70
VERDICT=INSTALLED-ORIGINAL-CALLABLE
```

Both probe runs resolved the provider's symbols and installed/called hooks.
That load path is driven by the install name/rpath, and the provider's
`LC_ID_DYLIB` was exact; the Identifier did not prevent loading.

**FACT (structure):** dyld's dependency matching uses the install path /
`LC_ID_DYLIB`; code-signature registration uses the CDHash and signature
blob. A structural read of the repository's local dyld-1286.10 sources found
no code-signing-Identifier comparison in load or dependency matching. That
source is **not** 22F82/dyld-1066.8; the exact-version runtime probe above is
the primary evidence, and no exact 1066.8 disassembly was used.

**Limit:** no post-install WindowServer provider map exists because R010's
generation failed earlier at `_xpc_bootstrap_services`; both probes selected
the arm64e provider slice, so the arm64 slice has static contract evidence
only.

### 4.2 trustcache admission — no effect

**FACT (runtime):** `jbctl trustcache add` was given only the two CDHashes;
both were observed live at indices 42/43 (arm64) and 24/25 (arm64e) in the
device trustcache. The helper implementations in both postinst paths parse
and pass `CDHash=` only.

**FACT (structure):** the trustcache is keyed by CDHash, not by the embedded
Identifier. The temp Identifier does change the CDHash (see §4.5), but that
new hash was the one admitted and the one carried by the atomically renamed
file. The duplicate trustcache indices are the observed `jbctl` behavior and
are unrelated to the Identifier.

### 4.3 dependency closure — no effect

**FACT (static):** `libmachook`/`libmachook_arm64` carry
`@rpath/CydiaSubstrate.framework/CydiaSubstrate`; the provider's both slices
declare the exact absolute `LC_ID_DYLIB`, and the package/staging verifier
enforces it. Neither field is the code-signing Identifier.

**FACT (runtime):** R010 launched the probes through `launchdchrootexec`,
which inserts `libmachook`; the probes then resolved `MSHookFunction` from
the provider at the expected path and UUID. This proves the dependency was
satisfied with the temp Identifier present. The arm64 runtime case was not
separately observed (both probes were arm64e).

### 4.4 existing provider probes — not sensitive to the field

**FACT:** the probe source reads only the loaded image path, symbol addresses,
`LC_UUID` and target bytes. It never calls `SecCodeCopySigningInformation`,
never reads `Identifier=`, and never constructs an identifier-based
requirement. The R010 post-install run passed with the temp Identifier.

### 4.5 reproducibility — the real observed consequence

**FACT (local reproduction of the publisher's flow):** using the same staged
arm64/arm64e companions, `lipo -create` into two differently named temp files
followed by the publisher's two unsigned-identifier `ldid` passes produced:

| Temp basename | Identifier | arm64 CDHash | arm64e CDHash | SHA-256 |
| --- | --- | --- | --- | --- |
| `.CydiaSubstrate.new-33333` | `.CydiaSubstrate.new-33333` | `11c573cd…` | `91db164d…` | `d73d8318…` |
| `.CydiaSubstrate.new-44444` | `.CydiaSubstrate.new-44444` | `9d6ddc5c…` | `5aca2131…` | `17de084e…` |

Repeating the same test with `-ICydiaSubstrate` in both passes produced, for
both temp names, the identical result:

```text
Identifier=CydiaSubstrate
arm64  CDHash=b7bbd396ea7d1f1f10ba4f3e7d733552b7aa2330
arm64e CDHash=0445c5c6f81b361666d7b8d1de507e0eed8a7a4f
sha256 7c3df88e4b9b699f311383cf5791fabd23b358306ddb1729ff3baf206c4fe679
```

These local values come from the current local build and are not comparable
to R010/R012 device hashes. The conclusion is the dependency: because
`os.getpid()` is in the temp basename, every publish of identical inputs
yields a different Identifier, CDHash and final SHA, and adds another
trustcache entry. No current consumer pins the published CDHash, so this is
an evidence/reproducibility and trustcache-growth problem, not an observed
load failure.

### 4.6 THEORY-level residual risks

- **THEORY:** a future consumer could construct a `SecRequirement` or read
  `kSecCodeInfoIdentifier` expecting `CydiaSubstrate`. No such consumer
  exists in this repository, and no runtime evidence shows one. The only
  `SecCodeCopySigningInformation` consumer in the tree inspects PluginKit
  entitlements/CMS, not the provider.
- **THEORY:** an install-time observer could mistake the hidden temp file for
  a provider. The publisher's two known local failure paths remove it, and
  the R010 install left no temp file; a hard kill between trust admission and
  `os.replace` could still leave a trusted orphan temp. This is adjacent to,
  not caused by, the Identifier.
- The install-time `ExcUserFault_codesign` report contains only the shared
  cache and `codesign` images at `EXC_GUARD` reason 9; it has no provider
  image and is not evidence of an Identifier failure.

## 5. Smallest future patch (specified, not implemented)

1. In `misc/macws_provider_contract.py::publish`, add
   `-I" + PROVIDER_NAME` to **both** existing ldid passes for the temporary
   file. Do not change the temp name, `lipo` reconstruction, CDHash
   derivation, trust admission or atomic `os.replace`.
2. After signing and before trust admission, verify per arch that
   `ldid -arch <arch> -h` reports `Identifier=CydiaSubstrate`; fail closed if
   either slice differs. This mirrors the existing identifier gates in
   `layout/DEBIAN/postinst`.
3. Extend the focused test fixture so the fake ldid records/validates the
   explicit identifier and add one negative case (wrong identifier must abort
   before `jbctl` and leave the old target untouched). No package-contract or
   archive change is needed because the package's linker-signed main and thin
   companions already carry `Identifier=CydiaSubstrate`.

Do not sign after `os.replace` (it would lose fresh-inode atomicity), and do
not add a global identifier override to unrelated project signing.

### Acceptance witness

- Local: two different temp basenames produce the same final SHA/CDHash and
  both slices report `Identifier=CydiaSubstrate`.
- Device: re-publish, then `ldid -arch arm64|arm64e -h` shows
  `Identifier=CydiaSubstrate`; UUIDs remain
  `B9199E06-3CE6-3116-80DB-5AA8B9DED5A5` /
  `A755DD35-D80C-3D90-9312-D566DE392770`; both new CDHashes are live; the
  provider probe reaches `INSTALLED-ORIGINAL-CALLABLE`; the WindowServer
  loaded-provider map contains the path; no `.CydiaSubstrate.new-*` remains;
  each known failure path preserves the old target.

## 6. Limits

- Read-only: no device/SSH, no install, no build, no package build/deploy, no
  source/test/publisher edit and no Git write.
- R010's own limits remain: no post-install WindowServer map (the generation
  died at the unrelated `_xpc_bootstrap_services` PAC fault), both probes
  selected arm64e, and the device ldid version was not separately recorded.
- The local `/tmp` witness uses host ldid 2.1.5-procursus7; the device-side
  basename behavior itself is already FACT from R010's `Identifier=` output.
- No claim is made that the Identifier affects pixels, TCC or input.

(handover to codex now)
