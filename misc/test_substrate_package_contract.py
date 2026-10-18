"""Focused package/staging/installer contract for the macOS provider."""
from __future__ import annotations

import io
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import macws_artifact_contract as artifact
import macws_provider_contract as contract
import provider_test_fixtures as fixtures

ROOT = HERE.parent
LIPO = shutil.which("lipo") or "/usr/bin/lipo"


class MachOContractTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_macos_fat_bundle_is_accepted(self):
        directory = fixtures.write_bundle(self.root / "bundle")
        report = contract.verify_bundle(directory, strict_slice_hashes=True)
        self.assertEqual(report["main_sha256"],
                         contract.sha256_file(directory / "CydiaSubstrate"))
        self.assertEqual(set(report["companions"]), {"arm64", "arm64e"})

    def test_missing_slice_is_rejected(self):
        directory = fixtures.write_bundle(
            self.root / "bundle", include_arm64e=False)
        with self.assertRaisesRegex(contract.ProviderError, "missing provider"):
            contract.verify_bundle(directory, strict_slice_hashes=True)

    def test_ios_platform_is_rejected(self):
        directory = fixtures.write_bundle(self.root / "bundle", platform=2)
        with self.assertRaisesRegex(contract.ProviderError, "not macOS"):
            contract.verify_bundle(directory, strict_slice_hashes=True)

    def test_wrong_minimum_macos_is_rejected(self):
        directory = fixtures.write_bundle(
            self.root / "bundle", minos=(12, 0, 0))
        with self.assertRaisesRegex(contract.ProviderError, "expected 13.x"):
            contract.verify_bundle(directory, strict_slice_hashes=True)

    def test_wrong_install_name_is_rejected(self):
        directory = fixtures.write_bundle(
            self.root / "bundle",
            install_name="@rpath/CydiaSubstrate.framework/CydiaSubstrate")
        with self.assertRaisesRegex(contract.ProviderError, "LC_ID_DYLIB"):
            contract.verify_bundle(directory, strict_slice_hashes=True)

    def test_thinned_main_from_dpkg_still_uses_both_companions(self):
        directory = fixtures.write_bundle(self.root / "bundle")
        arm64e = fixtures.thin_bytes("arm64e")
        (directory / "CydiaSubstrate").write_bytes(arm64e)
        report = contract.verify_bundle(directory, strict_slice_hashes=False)
        self.assertEqual(set(report["companions"]), {"arm64", "arm64e"})

    def test_strict_slice_hash_detects_substituted_main(self):
        directory = fixtures.write_bundle(self.root / "bundle")
        fixtures.write_fat(directory / "CydiaSubstrate", [
            fixtures.thin_bytes("arm64", minos=(13, 4, 0)),
            fixtures.thin_bytes("arm64e"),
        ])
        with self.assertRaisesRegex(contract.ProviderError,
                                    "differs from thin companion"):
            contract.verify_bundle(directory, strict_slice_hashes=True)


class StageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_stage_creates_exact_fat_and_thin_layout(self):
        binary = self.root / "built"
        fixtures.write_fat(binary, [
            fixtures.thin_bytes("arm64"), fixtures.thin_bytes("arm64e")])
        destination = self.root / "stage" / "CydiaSubstrate.framework"
        report = contract.stage(binary, destination, Path(LIPO))
        self.assertEqual(report["build"]["sha256"], contract.sha256_file(binary))
        self.assertTrue((destination / "CydiaSubstrate").is_file())
        self.assertTrue((destination / "CydiaSubstrate.arm64").is_file())
        self.assertTrue((destination / "CydiaSubstrate.arm64e").is_file())
        contract.verify_bundle(destination, strict_slice_hashes=True)

    def test_stage_rejects_missing_slice(self):
        binary = self.root / "built"
        fixtures.write_thin(binary, arch="arm64")
        with self.assertRaisesRegex(contract.ProviderError, "exactly arm64"):
            contract.stage(binary, self.root / "stage", Path(LIPO))

    def test_stage_rejects_ios_platform(self):
        binary = self.root / "built"
        fixtures.write_fat(binary, [
            fixtures.thin_bytes("arm64", platform=2),
            fixtures.thin_bytes("arm64e", platform=2)])
        with self.assertRaisesRegex(contract.ProviderError, "not macOS"):
            contract.stage(binary, self.root / "stage", Path(LIPO))


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bundle = fixtures.write_bundle(self.root / "bundle")
        self.rootfs = self.root / "rootfs"
        self.framework = (self.rootfs / "System/Library/Frameworks/"
                          "CydiaSubstrate.framework")
        self.framework.mkdir(parents=True)
        self.entitlements = self.root / "entitlements.plist"
        self.entitlements.write_bytes(plistlib.dumps({}))
        self.ldid = self.make_ldid(two_hashes=True)
        self.jbctl = self.make_jbctl(fail=False)
        self.state = self.root / "trust-state"

    def make_ldid(self, two_hashes: bool) -> Path:
        script = self.root / "ldid"
        output = [":"]
        if two_hashes:
            output = [
                'case "$arch" in',
                '  arm64) echo "CDHash=1111111111111111111111111111111111111111" ;;',
                '  arm64e) echo "CDHash=2222222222222222222222222222222222222222" ;;',
                '  *) exit 3 ;;',
                'esac',
                'exit 0',
            ]
        script.write_text(
            "#!/bin/sh\n"
            'if [ "$1" = "-arch" ]; then\n'
            '  arch="$2"; shift 2\n'
            '  for arg in "$@"; do\n'
            '    if [ "$arg" = "-h" ]; then\n'
            + "".join("      " + line + "\n" for line in output) +
            '    fi\n'
            '  done\n'
            'fi\n'
            'exit 0\n')
        script.chmod(0o755)
        return script

    def make_jbctl(self, fail: bool) -> Path:
        script = self.root / "jbctl"
        script.write_text(
            "#!/bin/sh\n"
            'state="' + str(self.root / 'trust-state') + '"\n'
            'case "$1 $2" in\n'
            '  "trustcache add")\n'
            + ('    exit 1\n' if fail else
               '    echo "$3" >> "$state"\n    exit 0\n') +
            '    ;;\n'
            '  "trustcache info")\n'
            '    [ -f "$state" ] && cat "$state"\n'
            '    exit 0 ;;\n'
            'esac\n'
            'exit 1\n')
        script.chmod(0o755)
        return script

    def publish(self, **overrides):
        values = dict(source_dir=self.bundle, rootfs=self.rootfs,
                      entitlements=self.entitlements, lipo=Path(LIPO),
                      ldid=self.ldid, jbctl=self.jbctl)
        values.update(overrides)
        return contract.publish(**values)

    def test_publish_replaces_existing_target_with_fresh_inode(self):
        target = self.framework / "CydiaSubstrate"
        target.write_bytes(b"old-provider")
        target.chmod(0o755)
        old_inode = target.stat().st_ino
        report = self.publish()
        new_inode = target.stat().st_ino
        self.assertNotEqual(old_inode, new_inode)
        self.assertEqual(report["target"], str(target))
        contract.verify_fat(target)
        self.assertEqual(report["cdhashes"]["arm64"],
                         "1111111111111111111111111111111111111111")
        self.assertEqual(report["cdhashes"]["arm64e"],
                         "2222222222222222222222222222222222222222")
        self.assertFalse(list(self.framework.glob(".CydiaSubstrate.new-*")))

    def test_publish_requires_mounted_rootfs(self):
        bare = self.root / "bare-rootfs"
        bare.mkdir()
        with self.assertRaisesRegex(contract.ProviderError, "not mounted"):
            self.publish(rootfs=bare)
        self.assertFalse((bare / "System").exists())

    def test_publish_requires_existing_target_directory(self):
        (self.framework).rmdir()
        with self.assertRaisesRegex(contract.ProviderError,
                                    "target directory is missing"):
            self.publish()
        self.assertFalse(self.framework.exists())

    def test_publish_rejects_missing_slice(self):
        shutil.rmtree(self.bundle)
        fixtures.write_bundle(self.bundle, include_arm64=False)
        with self.assertRaisesRegex(contract.ProviderError, "missing provider"):
            self.publish()

    def test_publish_rejects_architecture_or_platform_mismatch(self):
        shutil.rmtree(self.bundle)
        fixtures.write_bundle(self.bundle, platform=2)
        with self.assertRaisesRegex(contract.ProviderError, "not macOS"):
            self.publish()
        self.assertFalse((self.framework / "CydiaSubstrate").exists())

    def test_publish_trust_failure_keeps_old_target(self):
        target = self.framework / "CydiaSubstrate"
        target.write_bytes(b"old-provider")
        target.chmod(0o755)
        old_inode = target.stat().st_ino
        failing = self.make_jbctl(fail=True)
        with self.assertRaisesRegex(contract.ProviderError,
                                    "trustcache add failed"):
            self.publish(jbctl=failing)
        self.assertEqual(target.read_bytes(), b"old-provider")
        self.assertEqual(target.stat().st_ino, old_inode)
        self.assertFalse(list(self.framework.glob(".CydiaSubstrate.new-*")))

    def test_publish_rejects_missing_cdhash(self):
        ldid = self.make_ldid(two_hashes=False)
        with self.assertRaisesRegex(contract.ProviderError, "no CDHash"):
            self.publish(ldid=ldid)
        self.assertFalse((self.framework / "CydiaSubstrate").exists())

    def test_publish_rejects_symlink_target_directory(self):
        symlink = self.root / "other-framework"
        symlink.mkdir()
        self.framework.rmdir()
        self.framework.symlink_to(symlink)
        with self.assertRaisesRegex(contract.ProviderError, "symlink"):
            self.publish()


class WiringTests(unittest.TestCase):
    def setUp(self):
        self.debian = (ROOT / "layout/DEBIAN/postinst").read_text()
        self.deep = (ROOT / "layout/usr/macOS/bin/postinst.sh").read_text()
        self.shell = (ROOT /
                      "layout/usr/macOS/bin/macws_publish_substrate.sh").read_text()
        self.makefile = (ROOT / "Makefile").read_text()

    def test_package_postinst_publishes_before_mandatory_trust(self):
        publish_at = self.debian.index("macws_publish_substrate.sh")
        trust_at = self.debian.index(
            'trust_installed_macho \\\n        "$ROOTFS/System/Library/'
            'Frameworks/CydiaSubstrate.framework/CydiaSubstrate" 1')
        self.assertLess(publish_at, trust_at)
        self.assertIn('if [ -d "$ROOTFS/System/Library" ]; then', self.debian)
        self.assertNotIn(
            "install_chroot_runtime /var/jb/usr/macOS/Frameworks/"
            "CydiaSubstrate.framework", self.debian)

    def test_deep_postinst_publishes_and_rechecks_both_slices(self):
        publish_at = self.deep.index("macws_publish_substrate.sh")
        dump_at = self.deep.index('TRUSTCACHE_FILE="/tmp/postinst_trustcache_$$"')
        self.assertLess(publish_at, dump_at)
        self.assertIn("PROVIDER_TARGET=", self.deep)
        self.assertIn('for provider_arch in arm64 arm64e; do', self.deep)
        self.assertIn('trust_cdhash "$provider_hash" "$PROVIDER_TARGET"', self.deep)

    def test_native_ios_substrate_is_only_trusted_not_written(self):
        for text in (self.debian, self.deep):
            lines = text.splitlines()
            for index, line in enumerate(lines):
                if "/var/jb/Library/Frameworks/CydiaSubstrate" not in line:
                    continue
                context = " ".join(lines[max(0, index - 2):index + 1])
                self.assertTrue(
                    "trust" in context or "add_all" in context,
                    "unexpected native substrate write: " + context)

    def test_makefile_stages_provider_with_contract(self):
        self.assertIn("CydiaSubstrateProvider", self.makefile)
        self.assertIn("macws_provider_contract.py", self.makefile)
        self.assertIn("macws_provider_contract.py\" stage", self.makefile)
        self.assertIn(
            "$(THEOS_STAGING_DIR)/usr/macOS/Frameworks/"
            "CydiaSubstrate.framework", self.makefile)

    def test_shell_wrapper_uses_fixed_package_paths_and_exec(self):
        self.assertIn(
            "SOURCE_DIR=/var/jb/usr/macOS/Frameworks/"
            "CydiaSubstrate.framework", self.shell)
        self.assertIn(
            "ENTITLEMENTS=/var/jb/usr/macOS/bin/entitlements.plist",
            self.shell)
        self.assertIn("exec \"$PYTHON\" \"$VERIFIER\" publish", self.shell)
        executable = "\n".join(
            line for line in self.shell.splitlines()
            if not line.lstrip().startswith("#"))
        self.assertNotIn("/var/jb/Library", executable)


class PackageArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.staging = self.root / "staging"
        self.binary = self.root / "MacWSWindowing.dylib"
        self.binary.write_bytes(b"windowing")
        self.provider_dir = self.staging / artifact.SUBSTRATE_DIR
        fixtures.write_bundle(self.provider_dir)
        for name in artifact.ARCHIVE_PATHS:
            if name.startswith(artifact.SUBSTRATE_DIR + "/"):
                continue
            path = self.staging / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if name.endswith(".plist"):
                path.write_bytes(plistlib.dumps(
                    {"EnvironmentVariables": {"MACWS_TEST": "1"}},
                    fmt=plistlib.FMT_BINARY))
            elif name == artifact.WINDOWING_PATH:
                path.write_bytes(self.binary.read_bytes())
            else:
                path.write_bytes(name.encode())
        (self.staging / "DEBIAN").mkdir()
        (self.staging / "DEBIAN/postinst").write_text("true\n")

    def tar_bytes(self, omit=None, replace=None):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w") as archive:
            for name in artifact.ARCHIVE_PATHS:
                if omit and name == omit:
                    continue
                data = (replace or {}).get(name,
                                           (self.staging / name).read_bytes())
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o755
                archive.addfile(info, io.BytesIO(data))
        return stream.getvalue()

    def ctrl_bytes(self):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w") as archive:
            data = (self.staging / "DEBIAN/postinst").read_bytes()
            info = tarfile.TarInfo("postinst")
            info.size = len(data)
            info.mode = 0o755
            archive.addfile(info, io.BytesIO(data))
        return stream.getvalue()

    def fake_popen(self, fsys):
        class FakeProcess:
            def __init__(self, data):
                self.stdout = io.BytesIO(data)

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

            def wait(self):
                return 0

        def factory(argv, *args, **kwargs):
            if "--fsys-tarfile" in argv:
                return FakeProcess(fsys)
            if "--ctrl-tarfile" in argv:
                return FakeProcess(self.ctrl_bytes())
            raise AssertionError("unexpected command: %r" % (argv,))

        return factory

    def verify(self, fsys):
        with mock.patch.object(artifact.subprocess, "Popen",
                               side_effect=self.fake_popen(fsys)):
            artifact.verify_package(self.root / "candidate.deb", self.staging,
                                    self.binary)

    def test_complete_archive_with_provider_is_accepted(self):
        self.verify(self.tar_bytes())

    def test_missing_provider_main_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing runtime payload"):
            self.verify(self.tar_bytes(omit=artifact.SUBSTRATE_PATH))

    def test_missing_provider_companion_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing runtime payload"):
            self.verify(self.tar_bytes(omit=artifact.SUBSTRATE_ARM64E_PATH))

    def test_missing_provider_publisher_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing runtime payload"):
            self.verify(self.tar_bytes(
                omit="var/jb/usr/macOS/bin/macws_publish_substrate.sh"))

    def test_archive_provider_change_is_rejected(self):
        replace = {artifact.SUBSTRATE_PATH: b"different-provider"}
        with self.assertRaisesRegex(ValueError,
                                    "package differs from staged runtime"):
            self.verify(self.tar_bytes(replace=replace))

    def test_staging_provider_platform_mismatch_is_rejected(self):
        shutil.rmtree(self.provider_dir)
        fixtures.write_bundle(self.provider_dir, platform=2)
        with self.assertRaisesRegex(contract.ProviderError, "not macOS"):
            self.verify(self.tar_bytes())

    def test_staging_provider_slice_substitution_is_rejected(self):
        (self.provider_dir / "CydiaSubstrate.arm64e").write_bytes(
            fixtures.thin_bytes("arm64"))
        with self.assertRaisesRegex(contract.ProviderError,
                                    "expected arm64e"):
            self.verify(self.tar_bytes())


if __name__ == "__main__":
    unittest.main()
