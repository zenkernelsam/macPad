"""Guard the iOS-side repairs required by a filtered macOS rootfs restore."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BIND = (ROOT / "layout/usr/macOS/bin/ensure_jb_usr_bind.sh").read_text()
AUTOSIGND = (ROOT / "layout/usr/macOS/bin/restart_autosignd.sh").read_text()
CONTROL = (ROOT / "control").read_text()
MAKEFILE = (ROOT / "Makefile").read_text()
PACKAGE_POSTINST = (ROOT / "layout/DEBIAN/postinst").read_text()
MACOS_GUI = (ROOT / "layout/usr/macOS/bin/macos_gui.sh").read_text()


class RestoreBootContract(unittest.TestCase):
    def _run_restore(self, build):
        """Run the real restore function with only its external edges stubbed."""
        with tempfile.TemporaryDirectory() as td:
            rootfs = Path(td) / "rootfs"
            plist = rootfs / "System/Library/CoreServices/SystemVersion.plist"
            plist.parent.mkdir(parents=True)
            plist.write_bytes(
                b"<?xml version='1.0' encoding='UTF-8'?>"
                b"<plist version='1.0'><dict><key>ProductBuildVersion</key>"
                + b"<string>" + build.encode() + b"</string>"
                + b"</dict></plist>"
            )
            helper = Path(td) / "helper"
            helper.write_text(
                "import os, sys\n"
                "open(os.environ['MACWS_HELPER_LOG'], 'w').write('\\n'.join(sys.argv[1:]))\n"
            )
            source = Path(td) / "macos_gui.sh"
            # Keep all production definitions, but do not execute the command
            # dispatch at the end of the script while sourcing the function.
            source_text = MACOS_GUI.rsplit('\ncase "$CMD" in', 1)[0]
            source_text = source_text.replace(
                "ROOTFS=/var/mnt/rootfs", f"ROOTFS='{rootfs}'")
            source_text = source_text.replace(
                "/var/jb/usr/macOS/bin/macws_boot_trust.py", str(helper))
            source_text = source_text.replace(
                "/var/jb/usr/bin/python3", "python3")
            source.write_text(source_text + "\n")
            helper_log = Path(td) / "helper.log"
            env = os.environ | {
                "MACWS_ROOTFS": str(rootfs),
                "MACWS_BOOT_TRUST_HELPER": str(helper),
                "MACWS_PYTHON": "python3",
                "MACWS_HELPER_LOG": str(helper_log),
            }
            shell = """
set -e
source "$MACWS_TEST_SCRIPT"
application_trust_thermally_safe() { return 0; }
log() { :; }
restore_cold_boot_trust "$@"
"""
            result = subprocess.run(
                ["bash", "-c", shell, "bash"],
                env=env | {"MACWS_TEST_SCRIPT": str(source)},
                text=True,
                capture_output=True,
            )
            args = helper_log.read_text().splitlines() if helper_log.exists() else []
            return result, args

    def test_restore_selects_cache_pair_and_preserves_arguments(self):
        for build, expected in {
            "24G90": {
                "2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e",
                "8c7ba7e588b0edd43f7334e2de11688cd4732192",
            },
            # 22F82 stock cryptex pair — verified with the documented method
            # (codesign -vvv -d) against UniversalMac_13.4.1_22F82_Restore.ipsw,
            # sha256 5ac144d1…, on 2026-10-06.
            "22F82": {
                "7a3e85f1ddcb90e7d785bbfd6232fd058b4de317",
                "2573536d64cbd47872f3d318bf0efc6273d7cf20",
            },
            # The historical pair belongs to the author's 22F66-era cache
            # (patched or stock; never reproduced here). Keep it pinned to
            # 22F66 only — it must not be reused for a stock 22F82 rootfs.
            "22F66": {
                "b5da39409492ac85e5a8e8ab618fe77e2d7a2980",
                "bbb765988e2677b98d47a549d612fa0d4af25f69",
            },
            # Unreadable plist still falls back to the current proven build:
            # our deployed rootfs is 22F82 stock.
            "": {
                "7a3e85f1ddcb90e7d785bbfd6232fd058b4de317",
                "2573536d64cbd47872f3d318bf0efc6273d7cf20",
            },
        }.items():
            result, args = self._run_restore(build)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(set(args[i + 1] for i, value in enumerate(args)
                             if value == "--hash"), expected)
            self.assertIn("--manifest", args)
            self.assertIn("--resource-index", args)
            self.assertGreater(args.index("--hash"), args.index("--resource-index"))
            self.assertTrue(any(item.endswith("/usr/lib/dyld") for item in args))

    def test_restore_rejects_unknown_nonempty_build_before_helper(self):
        result, args = self._run_restore("25A100")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(args, [])

    def test_bind_probe_uses_the_ios_system_mount_binary(self):
        self.assertIn("system_mount=/sbin/mount", BIND)
        self.assertIn('[ -x "$system_mount" ]', BIND)
        self.assertEqual(BIND.count('if "$system_mount" | grep -Fq'), 1)
        self.assertIn('   "$system_mount" | grep -Fq', BIND)
        self.assertIn('grep -Fq " on $canonical_target ("', BIND)
        self.assertIn('grep -Fq " on $canonical_parent ("', BIND)
        self.assertNotIn(
            'grep -Fq "/var/jb/usr on $canonical_target ("', BIND)

    def test_bind_probe_accepts_only_the_exact_helper_fallback_symlink(self):
        symlink_probe = 'if [ -L "$target_dir" ]; then'
        directory_creation = '[ -d "$target_dir" ] || mkdir -p "$target_dir"'
        self.assertIn(symlink_probe, BIND)
        self.assertIn(
            '[ "$(readlink "$target_dir")" = "$source_dir" ]', BIND)
        self.assertIn(
            '[ -x "$target_dir/$proxy_relative" ]', BIND)
        self.assertLess(BIND.index(symlink_probe), BIND.index(directory_creation))

    def test_filtered_restore_recreates_only_the_volatile_tmp_directory(self):
        self.assertIn("ROOTFS=/var/mnt/rootfs", AUTOSIGND)
        self.assertIn(
            '[ ! -f "$ROOTFS/System/Library/CoreServices/SystemVersion.plist" ]',
            AUTOSIGND)
        self.assertIn('mkdir -p "$ROOTFS/private/tmp" || exit 1', AUTOSIGND)
        self.assertIn('chmod 1777 "$ROOTFS/private/tmp" || exit 1', AUTOSIGND)
        self.assertIn('ln -s private/tmp "$ROOTFS/tmp" || exit 1', AUTOSIGND)
        self.assertLess(
            AUTOSIGND.index("SystemVersion.plist"),
            AUTOSIGND.index('mkdir -p "$ROOTFS/private/tmp"'))

    def test_package_declares_ios_tools_used_during_postinstall(self):
        depends = next(
            line.removeprefix("Depends:").strip()
            for line in CONTROL.splitlines() if line.startswith("Depends:"))
        self.assertEqual(
            {item.strip() for item in depends.split(",")},
            {"gawk", "ldid", "odcctools", "plutil", "python3"})

    def test_package_survives_dpkg_fat_macho_thinning(self):
        # 2026-10-07: dual-slice binaries declare one dependency name on
        # both slice subtypes, so the package stages the FAT libmachook
        # under both names and lets dyld pick the matching slice.
        self.assertIn(
            'arm64="$(THEOS_STAGING_DIR)/usr/macOS/lib/libmachook_arm64.dylib"',
            MAKEFILE)
        self.assertIn('cp "$$fat" "$$arm64"', MAKEFILE)
        self.assertIn('Staged fat libmachook under both dependency names',
                      MAKEFILE)
        self.assertIn('elif [ -f "$LIBMACHOOK_ARM64" ]; then', PACKAGE_POSTINST)
        self.assertIn(
            '"$MACHO_PATCHER" "$LIBMACHOOK_ARM64" || exit 1',
            PACKAGE_POSTINST)

    def test_package_rejects_bootstraps_without_dynamic_trustcache_support(self):
        self.assertIn(
            'if [ ! -x /var/jb/usr/bin/jbctl ]; then',
            PACKAGE_POSTINST)
        self.assertIn(
            'NathanLR cannot admit the macOS shared-cache closure',
            PACKAGE_POSTINST)

    def test_package_restores_native_host_trust_before_publishing_it(self):
        declaration = (
            'HOST_APP=/var/jb/Applications/MacWSHost.app/MacWSHost')
        trust = 'trust_installed_macho "$HOST_APP"'
        publish = (
            '/var/jb/usr/bin/uicache -p '
            '/var/jb/Applications/MacWSHost.app')
        self.assertIn(declaration, PACKAGE_POSTINST)
        self.assertEqual(PACKAGE_POSTINST.count(trust), 1)
        self.assertLess(PACKAGE_POSTINST.index(trust),
                        PACKAGE_POSTINST.index(publish))

    def test_office_helper_gets_project_policy_before_trust_restore(self):
        postinst = (ROOT / "layout/usr/macOS/bin/postinst.sh").read_text()
        helper = (
            "/var/mnt/rootfs/Library/PrivilegedHelperTools/"
            "com.microsoft.office.licensingV2.helper")
        self.assertIn(
            "ensure_project_signature_and_trustcache \\\n    " + helper,
            postinst,
        )
        self.assertNotIn("add_all_trustcache \\\n    " + helper, postinst)


if __name__ == "__main__":
    unittest.main()
