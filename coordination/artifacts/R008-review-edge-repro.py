"""Reviewer edge fixtures; candidate code runs only against local fake OS tools."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GUI = (ROOT / 'layout/usr/macOS/bin/macos_gui.sh').read_text()
PKG = (ROOT / 'layout/DEBIAN/postinst').read_text()

def function(source, name):
    start = source.index(name + '() {')
    return source[start:source.index('\n}\n', start) + 3]

spec = importlib.util.spec_from_file_location(
    'candidate_fixture', ROOT / 'misc/test_tccd_lifecycle_contract.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
fixture = m.TccdReadinessFixtures()
results = []

for name, jobprint, print_rc in [
    ('endpoint_token_only_in_environment',
     'user/501/com.macwsguide.tccd-system = {\n pid = 4242\n'
     ' environment = { NOTE = com.apple.macosbooter.tccd.system }\n'
     ' endpoints = { }\n}\n', 0),
    ('observation_error_still_unloads_job', '', 75),
]:
    body = '''
launchctl() {
 echo "$*" >> "$CALLS"
 case "$1" in
 list) printf '%s\\n' ''' + shlex.quote(m.LIST_RUNNING) + '''; return 0;;
 print) printf '%s\\n' ''' + shlex.quote(jobprint) + '; return ' + str(print_rc) + ''';;
 esac
 return 0
}
'''
    r, calls = fixture._run(body)
    results.append({'case': name, 'actual_exit': r.returncode,
                    'stdout': r.stdout, 'stderr': r.stderr, 'launchctl_calls': calls})

with tempfile.TemporaryDirectory(prefix='r008-review-') as td:
    td = Path(td)
    jbctl = td / 'jbctl'
    live = 'a' * 40
    jbctl.write_text('#!/bin/sh\necho ' + live + '\nexit "${FAKE_TRUST_EXIT:-0}"\n')
    jbctl.chmod(0o755)
    py = function(GUI, 'trust_closure_already_live').split("<<'PY'\n",1)[1].split('\nPY',1)[0]
    py = py.replace('/var/jb/usr/bin/jbctl', str(jbctl))
    probe = td / 'manifest-probe.py'
    probe.write_text(py)
    target = td / 'tccd'
    other = td / 'other'
    target.write_text('fixture')
    other.write_text('fixture')
    for name, target_path, entry, failed_command in [
        ('missing_mandatory_path', td/'missing-tccd', None, False),
        ('mandatory_target_empty_codes', target, {'codes': []}, False),
        ('trust_query_nonzero_with_partial_stdout', target,
         {'codes': [{'hash': live}]}, True),
    ]:
        files = {str(other): {'codes': [{'hash': live}]}}
        if entry is not None: files[str(target_path)] = entry
        manifest = td / 'hashes.json'
        manifest.write_text(json.dumps({'files': files}))
        env = dict(os.environ, FAKE_TRUST_EXIT='75' if failed_command else '0')
        r = subprocess.run(['python3', str(probe), str(manifest), str(target_path)],
                           capture_output=True, text=True, env=env, timeout=10)
        results.append({'case': name, 'expected_exit': 'nonzero',
                        'actual_exit': r.returncode, 'stdout': r.stdout, 'stderr': r.stderr})

    rootfs = td / 'rootfs'
    target = rootfs / 'System/Library/PrivateFrameworks/TCC.framework/Support/tccd'
    target.parent.mkdir(parents=True)
    target.write_text('untouched fixture executable')
    native = td / 'native.plist'
    native.write_text('<plist/>')
    ldid = td / 'ldid'
    ldid.write_text('''#!/bin/sh
case "$*" in
 *-e*) printf '%s\n' '<key>com.apple.private.graphics-restart-no-kill</key>' '<true/>' '<key>com.apple.private.notificationcenterui.tcc</key>' '<true/>';;
 *) printf '%s\n' 'Identifier=com.apple.tccd' 'CDHash=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';;
esac
''')
    ldid.chmod(0o755)
    jbctl.write_text('#!/bin/sh\necho TRUST_FAILED >&2\nexit 75\n')
    script = '\n'.join(function(PKG, name) for name in
                       ['trust_installed_macho','ensure_chroot_native_service_signature',
                        'ensure_tccd_system_signature'])
    script = script.replace('/var/jb/usr/bin/ldid', shlex.quote(str(ldid)))
    script = script.replace('/var/jb/usr/bin/jbctl', shlex.quote(str(jbctl)))
    script = ('ROOTFS='+shlex.quote(str(rootfs))+'\nTCCD_NATIVE_ENTITLEMENTS='+
              shlex.quote(str(native))+'\n'+script+'\nensure_tccd_system_signature\n')
    path = td / 'sign-fixture.sh'
    path.write_text(script)
    r = subprocess.run(['/bin/bash', str(path)],capture_output=True,text=True,timeout=10)
    results.append({'case':'mandatory_signature_trust_failure_swallowed',
                    'expected_exit':'nonzero','actual_exit':r.returncode,
                    'stdout':r.stdout,'stderr':r.stderr,
                    'notes':'Fake ldid supplies only the two marker keys; fake jbctl always fails.'})

record={'method':'Candidate functions; only absolute external-tool paths redirected to fixture executables. No device use.',
        'gui_sha256':hashlib.sha256(GUI.encode()).hexdigest(),
        'pkg_sha256':hashlib.sha256(PKG.encode()).hexdigest(), 'results':results}
(ROOT/'coordination/artifacts/R008-review-edge-repro.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
