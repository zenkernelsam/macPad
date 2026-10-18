# Frontend R012 result — provider is now an ordinary package payload

Read-only device policy respected: no SSH/device, no `dpkg -i`, no full package
build/deploy, no write to native `/var/jb/Library/Frameworks/CydiaSubstrate`.
Code/test/package changes only; bounded target builds and focused tests.

Base HEAD: `7e5d771`. Code/test commit: `b717fb9`.

## Theos output naming (verified before writing the Makefile)

From `/Users/ciscohe/theos/makefiles/instance/library.mk`:

- `LIBRARY_NAME` is used verbatim as the output basename;
- the extension is `TARGET_LIB_EXT` (`.dylib`) unless
  `_LIBRARY_EXTENSION = -`, which means empty (lines 12–14);
- staging copies
  `$(THEOS_OBJ_DIR)/$(THEOS_CURRENT_INSTANCE)$(_LOCAL_LIBRARY_EXTENSION)`
  (line 58);
- the default install name is
  `$(LOCAL_INSTALL_PATH)/$(1)` (`targets/_common/darwin_head.mk:6`).

The subproject therefore uses `LIBRARY_NAME = CydiaSubstrate` plus
`CydiaSubstrate_LIBRARY_EXTENSION = -`, producing exactly `CydiaSubstrate`
(no `.dylib`) at `.theos/obj/macosx/CydiaSubstrate` in an aggregate build.
Both slices were observed with `LC_BUILD_VERSION platform 1 minos 13.0` and
`LC_ID_DYLIB /System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`.

## Path map

| Stage | Path |
| --- | --- |
| source | `misc/cydiasubstrate_macos.m` |
| subproject | `CydiaSubstrateProvider/Makefile` |
| build | `.theos/obj/macosx/CydiaSubstrate` (fat arm64+arm64e) |
| staging | `.theos/_/var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework/{CydiaSubstrate,CydiaSubstrate.arm64,CydiaSubstrate.arm64e}` |
| archive | `var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework/...` |
| installed storage | `/var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework/...` |
| published chroot | `/var/mnt/rootfs/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` |
| native iOS substrate | `/var/jb/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` — trust only, never written |

Two thin companions are staged because Procursus dpkg can thin a fat payload
at unpack; the postinstall publisher rebuilds the fat image from them and does
not assume the main file kept arm64.

## What changed

- `CydiaSubstrateProvider/Makefile`: macOS 13, arm64+arm64e, exact canonical
  source `../misc/cydiasubstrate_macos.m`, exact output name and chroot
  `LC_ID_DYLIB`.
- Root `Makefile`: adds the subproject to `SUBPROJECTS`; `after-stage` installs
  the verifier, stages the provider, and fails if the build/bundle is missing.
- `misc/macws_provider_contract.py`: parses actual Mach-O bytes; requires
  `MH_DYLIB`, exactly arm64+arm64e, macOS platform/minos 13, exact
  `LC_ID_DYLIB`, and main-vs-companion slice identity. `publish` reconstructs,
  verifies, signs twice with the existing project entitlement contract, adds
  and verifies both CDHashes as live, then atomically replaces the target on a
  fresh inode and re-verifies hash/slices. Missing rootfs, missing/symlinked
  target directory, missing slice, platform/arch mismatch and trust failure all
  fail closed; the old target is untouched on failure.
- `layout/usr/macOS/bin/macws_publish_substrate.sh`: fixed package paths; calls
  the verifier via `exec`.
- `layout/DEBIAN/postinst`: publishes before mandatory provider trust and uses
  the existing strict `trust_installed_macho ... 1`; skips only when the rootfs
  is not mounted.
- `layout/usr/macOS/bin/postinst.sh`: publishes before the trustcache snapshot;
  re-checks both CDHashes through the existing `trust_cdhash` helper.
- `misc/macws_artifact_contract.py`: the archive now must contain the provider
  main, both companions and the two scripts; the staged bundle is verified
  before archive hashing.
- `misc/build_on_ios.sh`: FAST guardrail includes the provider subproject and
  canonical source.

No new entitlement file, no share-package copy, no native substrate write.

## Verification

- Built the subproject directly and with an aggregate-style build dir (exit 0).
  Fat provider SHA `c215bb87…d1d7`; arm64 companion `47e08899…9061`;
  arm64e companion `7b2b1463…85ca`.
- Staged and re-verified with the contract (exit 0).
- Host publish witness with real `ldid` and a fake `jbctl`: fresh inode
  changed 9195137→9195179; final SHA `1d562550…08ec`; both CDHashes live.
  These are host-only identities, not device deployment acceptance.
- `python3 -m unittest -v misc.test_substrate_package_contract`: 30 tests OK.
- Combined focused run (provider package + artifact + provider source + route):
  59 tests OK, 13 skipped because this host has no `dpkg-deb`.
- `python3 misc/audit_runtime_switches.py`: exit 0 (309 env / 86 flags / 467).
- `bash -n` on both postinsts, publisher and `build_on_ios.sh`: exit 0.
- `python3 -m py_compile` on the new Python files: exit 0.
- `git diff --check`: exit 0.

## Limits

- No real `.deb` archive test on this host (`dpkg-deb` absent). Package archive
  behavior is covered with synthetic tar streams + a fake `dpkg-deb` Popen;
  real-dpkg omission tests are present but skipped here.
- Procursus dpkg thinning was not executed; companions are the authoritative
  source by design.
- Device ldid/Python and trustcache were not run; host CDHashes are not the
  deployed identity.
- Full package build/deploy and pixels/input/TCC acceptance remain out of
  scope. `install_rootfs_13.sh` can still re-harvest a rootfs substrate if run
  after install; that path was not changed.

(handover to codex now)
