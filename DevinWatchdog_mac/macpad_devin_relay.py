"""Read macPad Devin; send one short pointer only after explicit idle handover."""
import argparse
import asyncio
import hashlib
import fcntl
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import uuid

import iterm2

PROJECT = Path(__file__).resolve().parents[1]
ROLE = 'macPad移植'
HANDOVER = {'(handover to codex now)', '(handover to codex)'}


def marker_visible(screen, marker):
    # iTerm soft wraps/NUL separators can split even the marker token.
    return "".join(marker.split()) in "".join(screen.replace("\x00", " ").split())


def handover_ready(screen):
    lines = screen.replace('\x00', ' ').splitlines()
    markers = [i for i, line in enumerate(lines) if line.strip() in HANDOVER]
    prompts = [i for i, line in enumerate(lines)
               if re.match(r'^\s*❭\s+Ask Devin\b', line)]
    if not markers or not prompts or markers[-1] >= prompts[-1]:
        return False
    # Ignore an old handover remaining above a newly started CLI session.
    if any('Devin CLI' in line or '[macPad relay ' in line
           for line in lines[markers[-1] + 1:]):
        return False
    footer = '\n'.join(lines[-12:])
    return not any(text in footer for text in (
        'Guide Devin', 'esc twice to interrupt', 'Press Enter to send queued',
        '↵ accept', 'esc close', '[Pasted text #'))


def live_processes(tty):
    rows = subprocess.check_output(
        ['ps', '-axo', 'pid=,ppid=,tty=,comm='], text=True, timeout=5).splitlines()
    result = []
    for row in rows:
        fields = row.split(None, 3)
        if len(fields) != 4 or fields[2] != tty.removeprefix('/dev/'):
            continue
        if Path(fields[3]).name != 'devin':
            continue
        pid = int(fields[0])
        argv = subprocess.check_output(
            ['ps', '-p', str(pid), '-o', 'command='], text=True, timeout=5).strip()
        cwd = subprocess.run(['lsof', '-a', '-p', str(pid), '-d', 'cwd', '-Fn'],
                             text=True, capture_output=True, timeout=5)
        directory = next((line[1:] for line in cwd.stdout.splitlines()
                          if line.startswith('n')), None)
        parts = shlex.split(argv)
        result.append({'pid': pid, 'ppid': int(fields[1]), 'cwd': directory,
                       'acp': len(parts) == 2 and Path(parts[0]).name == 'devin'
                              and parts[1] == 'acp'})
    return result


async def snapshot(connection):
    app = await iterm2.async_get_app(connection, True)
    matches = []
    for window in app.windows:
        for tab in window.tabs:
            for session in tab.sessions:
                name = await session.async_get_variable('session.name') or session.name or ''
                if ROLE in name:
                    matches.append((session, name))
    if len(matches) != 1:
        raise RuntimeError('macPad role absent or ambiguous; no input sent')
    session, name = matches[0]
    tty = await session.async_get_variable('tty')
    command = await session.async_get_variable('commandLine')
    processes = live_processes(tty or '')
    agents = [p for p in processes if p['acp'] and p['cwd'] == str(PROJECT)]
    verified = len(agents) == 1 and command == 'devin acp'
    screen = await session.async_get_screen_contents()
    text = '\n'.join(screen.line(i).string for i in range(screen.number_of_lines))
    record = {'session_id': session.session_id, 'name': name, 'tty': tty,
              'commandLine': command, 'processes': processes,
              'agent_pid': agents[0]['pid'] if len(agents) == 1 else None,
              'binding_verified': verified, 'handover_ready': handover_ready(text),
              'screen': text}
    return record, session


def identity(record):
    return (record['session_id'], record['tty'], record['agent_pid'])


def require_recipient(record, args):
    if not record['binding_verified'] or record['session_id'] != args.expected_session_id \
            or record['agent_pid'] != args.expected_pid:
        raise RuntimeError('Fresh session/PID/cwd binding does not match; no input sent')


async def main(connection, args):
    record, session = await snapshot(connection)
    if args.action == 'read':
        if args.json:
            print(json.dumps(record, ensure_ascii=False, indent=2))
        else:
            print(json.dumps({k: v for k, v in record.items() if k != 'screen'},
                             ensure_ascii=False))
            print(record['screen'])
        return
    require_recipient(record, args)
    if not record['handover_ready']:
        raise RuntimeError('No explicit idle handover; task/review not sent')
    body = args.file.read_text().strip()
    if not body or '\n' in body or len(body) > 600:
        raise RuntimeError('Relay requires one short report/task pointer (maximum 600 characters)')
    digest = hashlib.sha256(body.encode()).hexdigest()
    marker = '[macPad relay ' + digest[:12] + '-' + uuid.uuid4().hex[:8] + ']'
    if args.dry_run:
        print(json.dumps({'input_sent': False, 'dry_run': True,
                          'identity': identity(record), 'handover_ready': True}))
        return
    expected = identity(record)
    await session.async_send_text(body, suppress_broadcast=True)
    await asyncio.sleep(.5)
    draft, draft_session = await snapshot(connection)
    require_recipient(draft, args)
    if identity(draft) != expected or 'Guide Devin' in draft['screen']:
        raise RuntimeError('Recipient changed or started working; no marker/CR sent')
    # Separate marker stays visible when the TUI collapses a pasted body.
    await draft_session.async_send_text(' ' + marker, suppress_broadcast=True)
    await asyncio.sleep(.5)
    after, current_session = await snapshot(connection)
    require_recipient(after, args)
    if identity(after) != expected or not marker_visible(after['screen'], marker) \
            or 'Guide Devin' in after['screen'] or 'Press Enter to send queued' in after['screen']:
        raise RuntimeError('Draft marker/identity not verified; no CR sent; never repaste automatically')
    # Exactly one CR; no working-session guide/queued flush/overlay keys.
    await current_session.async_send_text('\r', suppress_broadcast=True)
    print(json.dumps({'body_sent_once': True, 'cr_sent_once': True,
                      'identity': expected, 'body_sha256': digest}, ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('read', 'send'))
    parser.add_argument('file', type=Path, nargs='?')
    parser.add_argument('--expected-session-id')
    parser.add_argument('--expected-pid', type=int)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    if args.action == 'send' and (not args.file or not args.expected_session_id or not args.expected_pid):
        parser.error('send requires file, --expected-session-id and --expected-pid from a fresh read')
    lock = None
    try:
        if args.action == 'send' and not args.dry_run:
            (PROJECT / 'tmp').mkdir(exist_ok=True)
            lock = (PROJECT / 'tmp' / 'macpad-relay.lock').open('a')
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        iterm2.run_until_complete(lambda c: main(c, args), retry=False)
    except (RuntimeError, subprocess.SubprocessError, BlockingIOError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
