# R012 — provider as a reproducible package payload

Date: 2026-10-18. Scope: integrate the repaired
`misc/cydiasubstrate_macos.m` provider into the ordinary full package and the
postinstall runtime-copy/trust path. **No device/SSH, no dpkg install, no full
package build/deploy, no native `/var/jb/Library` substrate write.** Bounded
target builds and focused tests only.

Base HEAD: `7e5d771`. Code/test commit: `b717fb9`.

## 1. Theos output naming/install semantics (verified, not guessed)

The installed local Theos was inspected at
`/Users/ciscohe/theos/makefiles/instance/library.mk`:

- line 12: `_LOCAL_LIBRARY_EXTENSION = $(or
  $($(THEOS_CURRENT_INSTANCE)_LIBRARY_EXTENSION),$(TARGET_LIB_EXT))`
- lines 13-14: `_LIBRARY_EXTENSION = -` means **empty extension**
- line 24: `-dynamiclib -install_name "$(LOCAL_INSTALL_PATH)/$(1)"`
  (`targets/_common/darwin_head.mk:6`)
- line 58: the staged filename is
  `$(THEOS_OBJ_DIR)/$(THEOS_CURRENT_INSTANCE)$(_LOCAL_LIBRARY_EXTENSION)`

`LIBRARY_NAME` is used verbatim (it is not prefixed with `lib`). Therefore the
new subproject deliberately sets:

```make
LIBRARY_NAME = CydiaSubstrate
CydiaSubstrate_LIBRARY_EXTENSION = -
CydiaSubstrate_INSTALL_PATH = /usr/macOS/Frameworks/CydiaSubstrate.framework
CydiaSubstrate_LDFLAGS = -Wl,-install_name,/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate
```

Observed output (FACT): `.theos/obj/macosx/CydiaSubstrate` (no `.dylib`),
a fat Mach-O with arm64 + arm64e, `LC_BUILD_VERSION platform 1 minos 13.0`,
and `LC_ID_DYLIB = /System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`
for both slices. Direct subproject builds also produce
`CydiaSubstrateProvider/.theos/obj/macosx/CydiaSubstrate`; the root `after-stage`
accepts either location but requires one.

## 2. Path map

| Stage | Path |
| --- | --- |
| canonical source | `misc/cydiasubstrate_macos.m` |
| subproject | `CydiaSubstrateProvider/Makefile` (`LIBRARY_NAME = CydiaSubstrate`) |
| aggregate build output | `.theos/obj/macosx/CydiaSubstrate` |
| package staging | `.theos/_/var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework/{CydiaSubstrate,CydiaSubstrate.arm64,CydiaSubstrate.arm64e}` |
| archive (rootless) | `var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework/...` |
| installed package storage | `/var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework/...` |
| published chroot path | `/var/mnt/rootfs/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` (chroot `/System/Library/...`) |
| native iOS substrate | `/var/jb/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate` — **read-only trust only, never written** |

The two thin companions exist because Procursus dpkg can thin a fat Mach-O
payload while unpacking (the same behavior documented for libmachook in
`layout/DEBIAN/postinst`). The postinstall publisher reconstructs the fat image
from the companions instead of assuming the unpacked main file kept arm64.

## 3. Provider contract and atomic publish

New `misc/macws_provider_contract.py` (installed to
`/var/jb/usr/macOS/bin/macws_provider_contract.py`) verifies, from actual
Mach-O bytes:

- filetype `MH_DYLIB` for every slice;
- exactly `arm64` + `arm64e` in the fat main (and matching thin companions);
- each slice `LC_BUILD_VERSION platform == 1` (macOS) and `minos` major 13;
- each slice `LC_ID_DYLIB` exactly
  `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`;
- staged main slice hashes equal the thin companion hashes
  (`strict_slice_hashes=True` for package verification).

`publish` (invoked by `layout/usr/macOS/bin/macws_publish_substrate.sh`):

1. refuses a missing rootfs (`<rootfs>/System/Library` absent) or a missing /
   symlinked target framework directory; never creates a shadow tree;
2. rebuilds the fat image from the two companions with `lipo -create`;
3. verifies the full slice/platform/load-command contract before touching the
   target;
4. signs twice with the **existing** project entitlement contract
   (`ldid -S/var/jb/usr/macOS/bin/entitlements.plist -M`), no new or broadened
   entitlement file;
5. requires a CDHash for both slices, runs `jbctl trustcache add` for each and
   verifies live membership from `jbctl trustcache info`; a failed mandatory
   admission aborts and leaves the old target untouched;
6. `os.replace`s the verified temporary file over the target (same directory,
   fresh inode), then re-verifies slices and the final SHA-256.

`layout/DEBIAN/postinst` publishes before the chroot provider is trusted and
then calls the existing strict `trust_installed_macho ... 1`. When the rootfs
is not mounted it preserves the historical no-rootfs install path; when mounted
and the framework directory is missing, publication fails the install.
`layout/usr/macOS/bin/postinst.sh` publishes before its trustcache snapshot and
re-asserts both CDHashes through the existing `trust_cdhash` helper. Neither
path writes the native `/var/jb/Library` substrate.

## 4. Package verification extension

`misc/macws_artifact_contract.py` now includes:

- `var/jb/usr/macOS/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`
- `.../CydiaSubstrate.arm64`
- `.../CydiaSubstrate.arm64e`
- `var/jb/usr/macOS/bin/macws_provider_contract.py`
- `var/jb/usr/macOS/bin/macws_publish_substrate.sh`

in `PACKAGE_PATHS`/`SOURCE_PAYLOADS`. `verify_package` calls
`provider_contract.verify_bundle(staging / SUBSTRATE_DIR,
strict_slice_hashes=True)` before hashing the archive, so a self-consistent
archive with a missing companion, iOS platform, wrong minos or wrong
`LC_ID_DYLIB` is rejected. The existing archive-to-staging byte comparison then
proves the `.deb` contains exactly those verified provider bytes.

`misc/build_on_ios.sh` FAST guardrail now includes the provider subproject and
the canonical source so a FAST build cannot silently keep an old provider.

## 5. Focused tests and bounded builds

New `misc/test_substrate_package_contract.py` (30 tests) covers:
missing source/slice, missing rootfs/target directory, symlink target, fresh
inode replacement, trust failure preserving the old target, missing CDHash,
architecture/platform/install-name mismatch, staged bundle and archive
omission/change, strict main-vs-companion hash substitution, and postinst/
Makefile/shell wiring.

`misc/test_artifact_contract.py` was updated to create a valid provider bundle
and adds a real-dpkg omission case (skipped on this host: `dpkg-deb` is absent).

Commands and true exits:

| Command | Exit | Result |
| --- | --- | --- |
| `make THEOS=/Users/ciscohe/theos -C CydiaSubstrateProvider -j4` | 0 | fat arm64+arm64e provider built |
| `make THEOS=/Users/ciscohe/theos -C CydiaSubstrateProvider THEOS_PROJECT_DIR=$PWD _THEOS_LOCAL_DATA_DIR=/tmp/r012-theos all` | 0 | aggregate-path provider `.theos/obj/macosx/CydiaSubstrate` |
| `python3 misc/macws_provider_contract.py verify-build --binary /tmp/r012-theos/obj/macosx/CydiaSubstrate` | 0 | both slices, macOS 13.0, install name |
| `python3 misc/macws_provider_contract.py stage --binary ... --destination /tmp/r012-stage/... --lipo /usr/bin/lipo` | 0 | fat + both thin companions |
| `python3 misc/macws_provider_contract.py verify-bundle --directory /tmp/r012-stage/...` | 0 | strict bundle valid |
| `python3 misc/macws_provider_contract.py publish ... --ldid /usr/local/bin/ldid --jbctl /tmp/r012-tools/jbctl` | 0 | host witness: fresh inode 9195137→9195179, both hashes live |
| `python3 -m unittest -v misc.test_substrate_package_contract` | 0 | 30 tests OK |
| `python3 -m unittest -v misc.test_substrate_package_contract misc.test_artifact_contract misc.test_cydiasubstrate_provider_contract misc.test_app_input_route_contract` | 0 | 59 tests OK, 13 skipped (`dpkg-deb` absent) |
| `python3 misc/audit_runtime_switches.py` | 0 | 309 env / 86 flags / 467 entries |
| `bash -n layout/DEBIAN/postinst layout/usr/macOS/bin/postinst.sh layout/usr/macOS/bin/macws_publish_substrate.sh misc/build_on_ios.sh` | 0 | syntax OK |
| `python3 -m py_compile misc/macws_provider_contract.py misc/provider_test_fixtures.py misc/test_substrate_package_contract.py misc/macws_artifact_contract.py` | 0 | syntax OK |
| `git diff --check` | 0 | clean |

## 6. Hashes / identities

| Item | SHA-256 |
| --- | --- |
| `misc/cydiasubstrate_macos.m` (canonical source) | `6ac796c78f11fce18806840a20276308c782dbc77603c7781fc55d35eeaa7c2b` |
| built provider `.theos/obj/macosx/CydiaSubstrate` (fat) | `c215bb87d8e235f508e778d936196ee44c07d2ff7f3a2af7d62e44463c68d1d7` |
| staged `CydiaSubstrate.arm64` | `47e088994b86781ea206f68b95b422865536caa790b0af1012f1e4c4ed8f9061` |
| staged `CydiaSubstrate.arm64e` | `7b2b14632dc12e1172aaf6e13b910ec05b780b904a8bf7efcf22d13a3c1385ca` |
| host publish result (re-signed fat) | `1d5625504d80aa64ee14c9742c77973452a9f676c135260bd1d8ae8ab4d408ec` |
| host publish CDHash arm64 | `a5c83b036e071ded1d8bcf07530b32bb23c55c9f` |
| host publish CDHash arm64e | `4e63bb6fd6a523d8547b8015fbcbab36e588e3cc` |

The host publish hashes are a bounded local witness only; the device postinst
re-derives its own ldid CDHashes and trust membership.

## 7. Limits

- No device/SSH, `dpkg -i`, package build or deployment. The package archive
  path is verified through synthetic tar streams in tests; a real `.deb`
  archive test is skipped because this host has no `dpkg-deb`.
- Procursus dpkg's fat-thinning behavior is handled by design (thin companions
  are authoritative) but was not executed on this host.
- The device ldid/Python versions were not run; the publisher uses the same
  project ldid contract as libmachook and only standard Python 3 features.
- `misc/install_rootfs_13.sh` still harvests a rootfs substrate when building a
  rootfs; running it after a package install can overwrite the published
  provider. That rootfs-preparation path is outside this packet.
- Settings-extension panes continue to stage their separate native
  `/var/jb/usr/lib/libellekit.dylib` copy; not changed or written here.
- No pixels/input/TCC acceptance is claimed.

(handover to codex now)
