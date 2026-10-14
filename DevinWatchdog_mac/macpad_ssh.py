#!/usr/bin/env python3
"""Bounded SSH with real remote exit status; credentials only from environment."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
from datetime import datetime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--command', help='Remote shell command (no credentials)')
    source.add_argument('--script', type=Path, help='UTF-8 shell script sent over stdin')
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--record', type=Path, help='Write sanitized command/output/exit JSON')
    args = parser.parse_args()
    device = os.environ.get('MACWS_DEVICE', '')
    port = os.environ.get('MACWS_DEVICE_PORT', '2222')
    password = os.environ.get('MACWS_SSH_PASSWORD', '')
    if not device or device.startswith('-') or any(c.isspace() for c in device):
        parser.error('Set MACWS_DEVICE to user@host; do not store credentials in files')
    if not port.isdigit() or not 1 <= int(port) <= 65535:
        parser.error('MACWS_DEVICE_PORT must be a TCP port')
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    payload = args.script.read_text() if args.script else args.command
    if password and password in payload:
        parser.error('Do not include the SSH password in the remote command')
    argv = ['ssh', '-T', '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=accept-new',
            '-o', 'PreferredAuthentications=password' if password else 'BatchMode=yes',
            '-o', 'NumberOfPasswordPrompts=1', '-p', port, device]
    stdin = None
    if args.script:
        argv += ['sh -s']
        stdin = payload
    else:
        argv += [payload]
    env = os.environ.copy()
    # Never forward this secret to the remote shell or include it in argv.
    env.pop('MACWS_SSH_PASSWORD', None)
    if password:
        env['SSHPASS'] = password
        argv = ['sshpass', '-e'] + argv
    started = datetime.now().astimezone().isoformat()
    proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, env=env,
                            start_new_session=True)
    timed_out = False
    try:
        stdout, stderr = proc.communicate(input=stdin, timeout=args.timeout)
        code = proc.returncode
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(proc.pid, signal.SIGTERM)
        try:
            stdout, stderr = proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            stdout, stderr = proc.communicate()
        code = 124
    def sanitize(value):
        for secret in (password, device, device.rsplit('@', 1)[-1]):
            if secret:
                value = value.replace(secret, '<redacted>')
        return value
    stdout, stderr = sanitize(stdout), sanitize(stderr)
    if stdout:
        print(stdout, end='')
    if stderr:
        print(stderr, end='', file=sys.stderr)
    print(f'MACPAD_SSH_EXIT={code} timeout={int(timed_out)}', file=sys.stderr)
    if args.record:
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(json.dumps({
            'started': started, 'finished': datetime.now().astimezone().isoformat(),
            'command': sanitize(payload), 'command_sha256': hashlib.sha256(payload.encode()).hexdigest(),
            'exit': code, 'timeout': timed_out, 'stdout': stdout, 'stderr': stderr,
            'limit': 'A timeout kills the local SSH process group; remote work may need explicit cleanup.'
        }, ensure_ascii=False, indent=2) + '\n')
    return code if code >= 0 else 128 - code


if __name__ == '__main__':
    sys.exit(main())
