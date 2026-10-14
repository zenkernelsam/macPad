"""R008 contract tests for the private Ventura system-tccd transport.

Two layers are checked:

1. Behavioral fixtures — the *real* start_tccd_system,
   tccd_transport_ready and probe_tccd_transport bodies are extracted from
   layout/usr/macOS/bin/macos_gui.sh and executed under mocked OS edges
   (launchctl/kill/$CHROOTEXEC), the same harness style the R007 reviewer
   used in coordination/artifacts/R007-readiness-repro.py.  Each R007
   false-ready defect must now exit nonzero:

     warm path, job running but no endpoint      -> not ready
     cold path, unrelated-domain substring match -> not ready
     reported stale/dead generation pid          -> not ready
     launchctl lookup error                      -> not ready
     protocol probe fails / no dict marker       -> not ready

   Positive witnesses required just as much:

     warm job with exact domain+endpoint+live pid+dict reply -> ready,
         and must NOT re-load the plist
     cold load ending in the same ready state                -> ready
     loaded job with no `pid =` field (legitimate pressured
         exit, endpoint still claimable on demand) answering a
         real dictionary                                      -> ready

2. Static ordering/admission contracts — grep-level assertions that
   start_tccd_system is invoked on every dependent-rebuild path after the
   service publications and before dependent client work, that the stock
   tccd is a mandatory cold-trust target covered by the manifest probe,
   and that both postinst entry points sign it through the established
   native-merge helper with the real native marker + identifier.
"""

from pathlib import Path
import json
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GUI = (ROOT / 'layout/usr/macOS/bin/macos_gui.sh').read_text()
PKG_POSTINST = (ROOT / 'layout/DEBIAN/postinst').read_text()
DEEP_POSTINST = (ROOT / 'layout/usr/macOS/bin/postinst.sh').read_text()
MAKEFILE = (ROOT / 'Makefile').read_text()
TCCD_PLIST = ROOT / 'layout/usr/macOS/LaunchDaemons/com.macwsguide.tccd-system.plist'
TCCD_NATIVE_ENT = ROOT / 'layout/usr/macOS/bin/tccd-native-entitlements.plist'


def _extract_function(source, name):
    start = source.index(name + '() {')
    end = source.index('\n}\n', start)
    return source[start:end + 3]


FUNCTIONS = '\n'.join(_extract_function(GUI, name) for name in (
    'launchd_job_pid', 'probe_tccd_transport', 'tccd_transport_ready',
    'start_tccd_system'))

PREFIX = r'''
TCCD_SYSTEM_LABEL=com.macwsguide.tccd-system
TCCD_SYSTEM_PLIST=@TD@/tccd.plist
P_TCCD_SYSTEM='TCC.framework/Support/tccd system'
LOGDIR=@TD@
ROOTFS=@TD@/rootfs
CHROOTEXEC=@TD@/chrootexec
CALLS=@TD@/calls
rm() { :; }
# Keep a short real delay: the production probe loop polls a background
# process and a pure no-op sleep turns that bounded wait into a busy race
# the fixture can never win.
sleep() { /bin/sleep 0.05; }
tail() { :; }
log() { printf 'LOG %s\n' "$*"; }
proc_running() { return 1; }
# Only pid 4242 (the generation pid our launchctl fixtures report) is a
# fixture answer; every other pid — including the real $probe_pid child the
# watchdog polls — must go through the real builtin or the probe loop sees a
# zombie/live child as alive forever and false-times-out.
kill() {
    if [ "$1" = -0 ] && [ "$2" = 4242 ]; then
        [ "$FIXTURE_PID_LIVE" = 1 ] && return 0 || return 1
    fi
    builtin kill "$@"
}
'''

GOOD_PRINT = '''user/501/com.macwsguide.tccd-system = {
\tactive count = 1
\tpid = 4242
\tstate = running
\tservices = {
\t\tcom.apple.macosbooter.tccd.system = {
\t\t\tport = 0x18f03
\t\t\tactive = 1
\t\t}
\t}
}
'''

NOPID_PRINT = '''user/501/com.macwsguide.tccd-system = {
\tactive count = 0
\tstate = waiting
\tservices = {
\t\tcom.apple.macosbooter.tccd.system = {
\t\t\tport = 0x18f03
\t\t\tactive = 0
\t\t}
\t}
}
'''

NO_ENDPOINT_PRINT = '''user/501/com.macwsguide.tccd-system = {
\tactive count = 1
\tpid = 4242
\tstate = running
\tservices = {
\t}
}
'''

UNRELATED_PRINT = '''unrelated = com.apple.macosbooter.tccd.system.other
pid = 4242
'''

LIST_RUNNING = '{\n\t"PID" = 4242;\n\t"Label" = "com.macwsguide.tccd-system";\n};\n'


def _fixture_chrootexec(reply_marker=True, exit_code=0):
    """Shell body for the fake $CHROOTEXEC binary."""
    lines = ['#!/bin/sh']
    if reply_marker:
        lines.append("echo 'TCCPING reply-type=dictionary service=com.apple.tccd.system'")
    else:
        lines.append("echo 'TCCPING transport-error service=com.apple.tccd.system'")
    lines.append(f'exit {exit_code}')
    return '\n'.join(lines) + '\n'


class TccdReadinessFixtures(unittest.TestCase):
    """Execute the real production functions against mocked OS edges."""

    def _run(self, launchctl_body, *, pid_live=True, chrootexec_ok=True,
             chrootexec_rc=0):
        with tempfile.TemporaryDirectory(prefix='tccd-fixture-') as td:
            td = Path(td)
            (td / 'chrootexec').write_text(
                _fixture_chrootexec(reply_marker=chrootexec_ok,
                                    exit_code=chrootexec_rc))
            os.chmod(td / 'chrootexec', 0o755)
            (td / 'calls').touch()
            (td / 'tccd.plist').touch()
            (td / 'rootfs').mkdir()
            script = td / 'fixture.sh'
            script.write_text(
                PREFIX.replace('@TD@', str(td)) +
                'FIXTURE_PID_LIVE=%d\n' % (1 if pid_live else 0) +
                launchctl_body.replace('/fixture', str(td)) + '\n' +
                FUNCTIONS +
                '\nstart_tccd_system\n')
            result = subprocess.run(['/bin/bash', str(script)],
                                    capture_output=True, text=True,
                                    timeout=30)
            calls = (td / 'calls').read_text() if (td / 'calls').exists() else ''
            return result, calls.replace(str(td), '/fixture')

    def test_warm_ready_reuses_job_without_reload(self):
        body = '''
launchctl() {
    echo "$*" >> "$CALLS"
    case "$1 $2" in
        "list $TCCD_SYSTEM_LABEL") printf '%b' ''' + repr(LIST_RUNNING) + '''; return 0 ;;
        "print "*) case "$2" in *$TCCD_SYSTEM_LABEL) printf '%b' ''' + repr(GOOD_PRINT) + '''; return 0;; esac; return 113 ;;
        "print "*) return 113 ;;
    esac
    return 0
}
'''
        result, calls = self._run(body)
        self.assertEqual(result.returncode, 0,
                         f'warm-ready must succeed: {result.stdout}{result.stderr}')
        self.assertNotIn('load /fixture/tccd.plist', calls,
                         'warm-ready must not reload the job')

    def test_warm_no_endpoint_is_not_ready(self):
        body = '''
launchctl() {
    echo "$*" >> "$CALLS"
    case "$1 $2" in
        "list $TCCD_SYSTEM_LABEL") printf '%b' ''' + repr(LIST_RUNNING) + '''; return 0 ;;
        "print "*) printf '%b' ''' + repr(NO_ENDPOINT_PRINT) + '''; return 0 ;;
    esac
    return 0
}
'''
        result, calls = self._run(body)
        self.assertNotEqual(result.returncode, 0,
                            'warm job without its endpoint must not be ready')
        self.assertIn('load /fixture/tccd.plist', calls,
                      'a failed warm path must attempt a reload first')

    def test_unrelated_domain_substring_is_not_ready(self):
        body = '''
launchctl() {
    echo "$*" >> "$CALLS"
    case "$1" in
        list) return 1 ;;
        print) printf '%b' ''' + repr(UNRELATED_PRINT) + '''; return 0 ;;
        load) return 0 ;;
    esac
    return 0
}
'''
        result, _ = self._run(body)
        self.assertNotEqual(result.returncode, 0,
                            'unrelated-name substring must not prove the endpoint')

    def test_stale_reported_pid_is_not_ready(self):
        body = '''
launchctl() {
    echo "$*" >> "$CALLS"
    case "$1 $2" in
        "list $TCCD_SYSTEM_LABEL") printf '%b' ''' + repr(LIST_RUNNING) + '''; return 0 ;;
        "print "*) printf '%b' ''' + repr(GOOD_PRINT) + '''; return 0 ;;
    esac
    return 0
}
'''
        result, _ = self._run(body, pid_live=False)
        self.assertNotEqual(result.returncode, 0,
                            'a reported dead generation pid must not be ready')

    def test_lookup_error_is_not_ready(self):
        body = '''
launchctl() {
    echo "$*" >> "$CALLS"
    case "$1" in
        list) return 1 ;;
        print) return 113 ;;
        load) return 0 ;;
    esac
    return 0
}
'''
        result, _ = self._run(body)
        self.assertNotEqual(result.returncode, 0,
                            'launchctl lookup failure must fail closed')

    def test_protocol_failure_is_not_ready(self):
        body = '''
launchctl() {
    echo "$*" >> "$CALLS"
    case "$1 $2" in
        "list $TCCD_SYSTEM_LABEL") printf '%b' ''' + repr(LIST_RUNNING) + '''; return 0 ;;
        "print "*) printf '%b' ''' + repr(GOOD_PRINT) + '''; return 0 ;;
    esac
    return 0
}
'''
        result, _ = self._run(body, chrootexec_ok=False, chrootexec_rc=2)
        self.assertNotEqual(result.returncode, 0,
                            'transport-error reply must fail readiness')

    def test_cold_load_reaches_ready(self):
        body = '''
launchctl() {
    echo "$*" >> "$CALLS"
    case "$1 $2" in
        "list $TCCD_SYSTEM_LABEL")
            if grep -q "load /fixture/tccd.plist" "$CALLS"; then
                printf '%b' ''' + repr(LIST_RUNNING) + '''; return 0
            fi
            return 1 ;;
        "print "*) printf '%b' ''' + repr(GOOD_PRINT) + '''; return 0 ;;
    esac
    return 0
}
'''
        result, calls = self._run(body)
        self.assertEqual(result.returncode, 0,
                         f'cold load must reach ready: {result.stdout}{result.stderr}')
        self.assertIn('load /fixture/tccd.plist', calls)

    def test_pressured_exit_without_pid_still_ready_via_endpoint(self):
        body = '''
launchctl() {
    echo "$*" >> "$CALLS"
    case "$1 $2" in
        "list $TCCD_SYSTEM_LABEL") printf '%b' ''' + repr(LIST_RUNNING) + '''; return 0 ;;
        "print "*) printf '%b' ''' + repr(NOPID_PRINT) + '''; return 0 ;;
    esac
    return 0
}
'''
        result, _ = self._run(body)
        self.assertEqual(result.returncode, 0,
                         'on-demand job answering a real dictionary is ready')


class TccdOrderingContracts(unittest.TestCase):
    """Static ordering: tccd is a checked prerequisite on every path that
    republishes dependent services before client/preflight work."""

    def _function_body(self, name):
        return _extract_function(GUI, name)

    def test_shared_replacement_path_orders_tccd_before_clients(self):
        body = self._function_body('start_ws_dependents_after_replacement')
        publish = body.index('publish_desktop_operation_services')
        tccd = body.index('start_tccd_system')
        self.assertLess(publish, tccd)
        # every dependent client job/service is restored AFTER the tccd call
        for dependent in ('launchctl load "$LSD_SYSTEM_PLIST"',
                          'launchctl load "$LSD_PLIST"',
                          'start_sharedfilelistd',
                          'ensure_desktop_job',
                          'launchctl load "$INPUT_PLIST"'):
            if dependent in body:
                self.assertLess(tccd, body.index(dependent), dependent)

    def test_desktop_session_rebuild_orders_tccd_before_ws_relaunch(self):
        # The rebuild block is the only place that logs this exact error.
        anchor = GUI.index('desktop-operation contracts were not ready for the replacement WindowServer')
        region = GUI[anchor:anchor + 4000]
        self.assertIn('start_tccd_system', region)
        relaunch = region.index('launchctl start "$WINDOWSERVER_LABEL"')
        self.assertLess(region.index('start_tccd_system'), relaunch)

    def test_cold_start_keeps_tccd_before_launchservicesd(self):
        anchor = GUI.index('start_tccd_system || return 1')
        self.assertIn('launchservicesd', GUI[anchor:anchor + 2000])

    def test_stop_ws_dependents_retires_tccd(self):
        body = self._function_body('stop_ws_dependents')
        self.assertIn('$TCCD_SYSTEM_PLIST', body)
        self.assertIn('$TCCD_SYSTEM_LABEL', body)
        self.assertIn('$P_TCCD_SYSTEM', body)

    def test_probe_invokes_injected_client_context(self):
        body = self._function_body('probe_tccd_transport')
        self.assertIn('$CHROOTEXEC', body)
        self.assertIn('macwstccdprobe', body)
        self.assertIn('ping', body)


class TccdTrustManifestContracts(unittest.TestCase):
    """F2: the tccd target must be mandatory in cold trust and cannot be
    skipped by a cached manifest that predates it."""

    def test_tccd_in_mandatory_restore_list(self):
        self.assertIn(
            '$ROOTFS/System/Library/PrivateFrameworks/TCC.framework/Support/tccd',
            GUI)

    def test_manifest_probe_runs_after_list_with_mandatory_paths(self):
        anchor = GUI.index('trust_closure_already_live '
                           '"$boot_trust_cache/hashes.json" "$@"')
        # the probe must run AFTER the build-version case completes the list
        self.assertIn('esac', GUI[:anchor])
        self.assertLess(GUI.index('esac\n    # Cheap live-membership probe'
                                  .replace('esac\\n', 'esac\n')), anchor + 1)

    def test_manifest_probe_checks_coverage(self):
        body = _extract_function(GUI, 'trust_closure_already_live')
        self.assertIn('os.path.isfile(path)', body)
        self.assertIn('os.path.isdir(path)', body)
        self.assertIn('files.get(path)', body)

    def test_manifest_coverage_semantics(self):
        """Run the real heredoc against fixture manifests + fake jbctl."""
        with tempfile.TemporaryDirectory(prefix='tccd-manifest-') as td:
            td = Path(td)
            fake_bin = td / 'fake-bin'
            fake_bin.mkdir()
            jbctl = fake_bin / 'jbctl'
            live_hash = 'a' * 40
            jbctl.write_text(f'#!/bin/sh\necho "hash {live_hash}"\n')
            os.chmod(jbctl, 0o755)

            manifest_dir = td / 'manifest-dir'
            manifest_dir.mkdir()
            target = td / 'tccd'
            target.write_bytes(b'\xcf\xfa\xed\xfe')
            other = td / 'other'
            other.write_bytes(b'\xcf\xfa\xed\xfe')

            def manifest(entries):
                return {'schema': 1, 'files': entries}

            full = manifest({
                str(target): {'identity': [1, 2, 3, 4, 5],
                              'codes': [{'arch': 'arm64e', 'type': 1,
                                         'hash': live_hash}]},
                str(other): {'identity': [1, 2, 3, 4, 5],
                             'codes': [{'arch': 'arm64', 'type': 1,
                                        'hash': live_hash}]},
            })
            missing = manifest({
                str(other): {'identity': [1, 2, 3, 4, 5],
                             'codes': [{'arch': 'arm64', 'type': 1,
                                        'hash': live_hash}]},
            })
            dead = manifest({
                str(target): {'identity': [1, 2, 3, 4, 5],
                              'codes': [{'arch': 'arm64e', 'type': 1,
                                         'hash': 'b' * 40}]},
                str(other): {'identity': [1, 2, 3, 4, 5],
                             'codes': [{'arch': 'arm64', 'type': 1,
                                        'hash': live_hash}]},
            })

            # pull the heredoc python out of the real function verbatim
            body = _extract_function(GUI, 'trust_closure_already_live')
            py = body.split("<<'PY'\n", 1)[1].split('\nPY', 1)[0]
            script = td / 'probe.py'
            script.write_text(py)
            manifest_path = td / 'hashes.json'

            def run_probe(entries, paths):
                manifest_path.write_text(json.dumps(entries))
                env = dict(os.environ)
                env['PATH'] = str(fake_bin) + os.pathsep + env['PATH']
                return subprocess.run(
                    ['python3', str(script), str(manifest_path)] + paths,
                    capture_output=True, text=True, env=env,
                    timeout=15).returncode

            # patched: jbctl lookup inside the probe uses the absolute
            # device path; redirect it to the fixture via PATH shim.
            shimmed = py.replace('/var/jb/usr/bin/jbctl', str(jbctl))
            script.write_text(shimmed)

            inner = manifest_dir / 'inner'
            inner.write_bytes(b'\xcf\xfa\xed\xfe')
            full_with_dir = manifest({
                str(target): {'identity': [1, 2, 3, 4, 5],
                              'codes': [{'arch': 'arm64e', 'type': 1,
                                         'hash': live_hash}]},
                str(other): {'identity': [1, 2, 3, 4, 5],
                             'codes': [{'arch': 'arm64', 'type': 1,
                                        'hash': live_hash}]},
                str(inner): {'identity': [1, 2, 3, 4, 5],
                             'codes': [{'arch': 'arm64', 'type': 1,
                                        'hash': live_hash}]},
            })

            self.assertEqual(run_probe(full, [str(target), str(other)]), 0,
                             'full coverage + live hashes must skip the walk')
            self.assertNotEqual(
                run_probe(missing, [str(target), str(other)]), 0,
                'manifest lacking the tccd entry must force the walk')
            self.assertNotEqual(
                run_probe(dead, [str(target), str(other)]), 0,
                'recorded-but-not-live hash must force the walk')
            self.assertEqual(
                run_probe(full_with_dir,
                          [str(target), str(other), str(manifest_dir)]),
                0, 'a scanned directory with a recorded member is covered')
            self.assertNotEqual(
                run_probe(full, [str(target), str(other), str(manifest_dir)]),
                0, 'a directory with no recorded member must force the walk')
            self.assertEqual(
                run_probe(full, [str(target), str(other),
                                 '--hash', live_hash]),
                0, 'an explicit --hash that is live stays on the fast path')
            self.assertNotEqual(
                run_probe(full, [str(target), str(other),
                                 '--hash', 'c' * 40]),
                0, 'an explicit --hash missing from the live set must '
                   'force the walk even when every path is covered')
            self.assertNotEqual(
                run_probe(full, [str(target), str(other),
                                 '--hash=' + 'd' * 40]),
                0, 'the --hash=<v> spelling must be checked the same way')


class TccdAdmissionContracts(unittest.TestCase):
    """F2: package + deep-repair signing contract for the Ventura tccd."""

    def test_package_postinst_signs_tccd_native_merge(self):
        self.assertIn(
            '"$ROOTFS/System/Library/PrivateFrameworks/TCC.framework/'
            'Support/tccd"', PKG_POSTINST)
        self.assertIn('$TCCD_NATIVE_ENTITLEMENTS', PKG_POSTINST)
        self.assertIn(
            "'<key>com.apple.private.notificationcenterui.tcc</key>'",
            PKG_POSTINST)
        self.assertIn("'com.apple.tccd'", PKG_POSTINST)
        self.assertIn('com.apple.TCC', PKG_POSTINST)
        # fresh-inode: stage → established merge contract → verify → mv -f;
        # pristine slice preserved as .macws-original
        self.assertIn('ensure_tccd_system_signature() {', PKG_POSTINST)
        self.assertIn('ensure_tccd_system_signature || exit 1', PKG_POSTINST)
        self.assertIn('.macws-original', PKG_POSTINST)
        self.assertIn('.macws-new.$$', PKG_POSTINST)
        self.assertIn('mv -f "$temporary" "$target"', PKG_POSTINST)
        # the merge still runs through the established helper, on the stage
        fn = _extract_function(PKG_POSTINST, 'ensure_tccd_system_signature')
        self.assertIn('ensure_chroot_native_service_signature \\', fn)
        self.assertIn('"$temporary"', fn)
        # verify-before-swap on the staged file
        self.assertIn('ldid -e "$temporary"', fn)
        self.assertIn('ldid -h "$temporary"', fn)

    def test_deep_repair_signs_tccd_native_merge(self):
        self.assertIn(
            "'/var/mnt/rootfs/System/Library/PrivateFrameworks/TCC.framework/"
            "Support/tccd'", DEEP_POSTINST)
        self.assertIn('$TCCD_NATIVE_ENT', DEEP_POSTINST)
        self.assertIn("'com.apple.tccd'", DEEP_POSTINST)
        self.assertIn('com.apple.TCC', DEEP_POSTINST)
        # fresh-inode: staged .macws-new copy through the same helper, then
        # atomic swap; pristine preserved as .macws-original
        self.assertIn('.macws-original', DEEP_POSTINST)
        self.assertIn('.macws-new.$$', DEEP_POSTINST)
        self.assertIn('mv -f "$tccd_temporary" "$tccd_target"', DEEP_POSTINST)
        stage = DEEP_POSTINST.index('sign_and_trustcache_merging_native_entitlements \\\n            "$tccd_temporary"')
        self.assertGreater(stage, 0)
        self.assertLess(
            DEEP_POSTINST.index('cp -p "$tccd_target" "$tccd_temporary"'),
            stage)
        self.assertLess(
            stage,
            DEEP_POSTINST.index('mv -f "$tccd_temporary" "$tccd_target"'))

    def test_native_profile_exists_and_matches_stock_keys(self):
        import plistlib
        ent = plistlib.loads(TCCD_NATIVE_ENT.read_bytes())
        self.assertEqual(
            set(ent.keys()), {
                'com.apple.fileprovider.acl-read',
                'com.apple.private.coreservices.canmaplsdatabase',
                'com.apple.private.kernel.global-proc-info',
                'com.apple.private.notificationcenterui.tcc',
                'com.apple.private.responsibility.set-arbitrary',
                'com.apple.private.security.storage.TCC',
                'com.apple.private.system-extensions.tcc',
                'com.apple.private.tcc.allow',
                'com.apple.private.tcc.manager',
                'com.apple.private.usernotifications.bundle-identifiers',
                'com.apple.rootless.storage.TCC',
                'com.apple.runningboard.process-state',
            })
        # the stock single-element allow list is preserved verbatim in the
        # native profile; the project merge may add kTCCServiceListenEvent
        # on top — that union is the documented installed delta.
        self.assertEqual(ent['com.apple.private.tcc.allow'],
                         ['kTCCServiceSystemPolicyAllFiles'])
        self.assertEqual(ent['com.apple.private.usernotifications.bundle-identifiers'],
                         ['com.apple.tccd'])

    def test_job_plist_declares_only_private_endpoint(self):
        import plistlib
        job = plistlib.loads(TCCD_PLIST.read_bytes())
        self.assertEqual(job['Label'], 'com.macwsguide.tccd-system')
        self.assertEqual(set(job.get('MachServices', {})),
                         {'com.apple.macosbooter.tccd.system'})
        args = ' '.join(job['ProgramArguments'])
        self.assertIn('launchdchrootexec', args)
        self.assertIn('TCC.framework/Support/tccd', args)
        self.assertTrue(job.get('EnablePressuredExit', False))

    def test_probe_is_built_and_deployed(self):
        self.assertIn('macwstccdprobe', MAKEFILE)
        self.assertIn('macwstccdprobe', PKG_POSTINST)
        self.assertTrue((ROOT / 'macwstccdprobe' / 'main.c').is_file())
        self.assertTrue((ROOT / 'macwstccdprobe' / 'Makefile').is_file())


if __name__ == '__main__':
    unittest.main()
