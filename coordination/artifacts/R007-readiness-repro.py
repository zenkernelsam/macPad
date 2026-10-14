"""Read-only reviewer fixture: execute the candidate function with fake OS calls."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'layout/usr/macOS/bin/macos_gui.sh'
s = SOURCE.read_text()
start = s.index('start_tccd_system() {')
body = s[start:s.index('\n}\n', start) + 3]
prefix = r'''
TCCD_SYSTEM_LABEL=com.macwsguide.tccd-system
TCCD_SYSTEM_PLIST=/fixture/tccd.plist
P_TCCD_SYSTEM='TCC.framework/Support/tccd system'
LOGDIR=/fixture
log() { printf '%s\n' "$*"; }
rm() { :; }
sleep() { :; }
tail() { :; }
launchd_job_pid() { printf '%s\n' "$$"; }
'''
cases = {
    'warm_path_has_no_endpoint': r'''
proc_running() { return 0; }
launchctl() {
    case "$1" in
        list) return 0;;
        print) printf 'ERROR: endpoint absent\n'; return 113;;
    esac
    return 0
}
''',
    'cold_path_only_unrelated_domain_substring': r'''
seen=0
proc_running() { seen=$((seen + 1)); [ "$seen" -gt 1 ]; }
launchctl() {
    case "$1" in
        print) printf 'unrelated = com.apple.macosbooter.tccd.system.other\n';;
    esac
    return 0
}
''',
}
results=[]
for name, mocks in cases.items():
    with tempfile.TemporaryDirectory(prefix='r007-readiness-') as td:
        fixture = Path(td) / 'fixture.sh'
        fixture.write_text(prefix + mocks + body + '\nstart_tccd_system\n')
        r = subprocess.run(['/bin/bash', str(fixture)], capture_output=True,
                           text=True, timeout=10)
    results.append({'case': name, 'expected_readiness_exit': 'nonzero',
                    'actual_exit': r.returncode, 'stdout': r.stdout,
                    'stderr': r.stderr, 'false_ready_reproduced': r.returncode == 0})
record = {'source_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
          'method': 'Unmodified candidate function; OS calls mocked; no device access or mutation.',
          'cases': results}
(ROOT / 'coordination/artifacts/R007-readiness-repro.json').write_text(
    json.dumps(record, indent=2) + '\n')
print(json.dumps(record, indent=2))
