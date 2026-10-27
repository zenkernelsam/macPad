"""Identity-checked single-message relay for qoderclicn iTerm2 sessions."""
import argparse
import asyncio
import fcntl
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

import iterm2

PROJECT = Path(__file__).resolve().parents[1]
HANDOVER = {'(handover to codex now)', '(handover to codex)'}
HANDOVER_REMINDER = '后台通知已结束。请确认没有运行中或排队任务，完成清理后回复独立一行 (handover to codex now)。后续后台通知完成后也请重新输出交接标记。'


def live_process(tty):
    rows = subprocess.check_output(
        ['ps', '-axo', 'pid=,ppid=,tty=,comm='], text=True).splitlines()
    out = []
    for row in rows:
        fields = row.split(None, 3)
        if len(fields) != 4 or fields[2] != tty.removeprefix('/dev/'):
            continue
        if Path(fields[3]).name != 'qoderclicn':
            continue
        pid = int(fields[0])
        command = subprocess.check_output(
            ['ps', '-p', str(pid), '-o', 'command='], text=True).strip()
        cwd = subprocess.run(['lsof', '-a', '-p', str(pid), '-d', 'cwd', '-Fn'],
                             capture_output=True, text=True).stdout
        directory = next((x[1:] for x in cwd.splitlines() if x.startswith('n')), None)
        out.append({'pid': pid, 'ppid': int(fields[1]), 'cwd': directory,
                    'command': command})
    return out


def screen_text(screen):
    return '\n'.join(screen.line(i).string for i in range(screen.number_of_lines))


def marker_visible(text, marker):
    return ''.join(marker.split()) in ''.join(text.replace('\x00', ' ').split())


def working(text, name=''):
    text = text.replace('\x00', ' ')
    return ('| Working' in name or
            bool(re.search(r'(?m)^\s*.*(?:Generating\.\.\.|Thinking\.\.\.|esc to cancel|└\s*Running)', text)) or
            'Queued (press' in text or 'Ctrl+Enter to send now' in text)


def idle(screen, name=''):
    text = screen_text(screen).replace('\x00', ' ')
    if working(text, name):
        return False
    lines = text.splitlines()
    if any(line.strip() in HANDOVER for line in lines):
        # A fresh handover is accepted only when the input prompt follows it.
        last = max(i for i, line in enumerate(lines) if line.strip() in HANDOVER)
        return any(i > last and 'Type your message' in line for i, line in enumerate(lines))
    return 'Type your message or @path/to/file' in text and 'Thinking' not in text


def reminder_ready(text, name):
    # Only a fixed administrative reminder may use this gate, never a task.
    return ('| Ready (qoderclicn)' in name and
            'Type your message or @path/to/file' in text.replace('\x00', ' ') and
            not working(text, name))


async def snapshot(connection, role, session_id):
    app = await iterm2.async_get_app(connection, True)
    matches = []
    for window in app.windows:
        for tab in window.tabs:
            for session in tab.sessions:
                name = await session.async_get_variable('session.name') or session.name or ''
                tty = await session.async_get_variable('tty') or ''
                if role in name:
                    matches.append((session, name, tty))
    if len(matches) != 1:
        raise RuntimeError(f'expected one qoder role {role!r}, found {len(matches)}')
    session, name, tty = matches[0]
    if session_id and session.session_id != session_id:
        raise RuntimeError('session id does not match the current role binding')
    processes = live_process(tty)
    agents = [p for p in processes if p['cwd'] == str(PROJECT)]
    screen = await session.async_get_screen_contents()
    record = {'session_id': session.session_id, 'name': name, 'tty': tty,
              'processes': processes, 'agent_pid': agents[0]['pid'] if len(agents) == 1 else None,
              'binding_verified': len(agents) == 1,
              'handover_ready': idle(screen, name), 'screen': screen_text(screen)}
    return record, session


async def main(connection, args):
    record, session = await snapshot(connection, args.role, args.session_id)
    if args.action == 'read':
        print(json.dumps(record, ensure_ascii=False, indent=2))
        return
    if not record['binding_verified'] or record['agent_pid'] != args.expected_pid:
        raise RuntimeError('qoder PID/cwd binding failed; no input sent')
    reminder = args.action == 'remind-handover'
    if not (reminder_ready(record['screen'], record['name']) if reminder else record['handover_ready']):
        raise RuntimeError('no idle prompt/handover; no input sent')
    body = HANDOVER_REMINDER if reminder else args.file.read_text(encoding='utf-8').strip()
    if not body or '\n' in body or len(body) > 600:
        raise RuntimeError('pointer must be one nonempty line of at most 600 characters')
    digest = hashlib.sha256(body.encode()).hexdigest()
    marker = f'[macPad qoder relay {digest[:12]}-{uuid.uuid4().hex[:8]}]'
    expected = (record['session_id'], record['tty'], record['agent_pid'])
    if args.dry_run:
        print(json.dumps({'input_sent': False, 'dry_run': True, 'identity': expected}))
        return
    await session.async_send_text(body, suppress_broadcast=True)
    await asyncio.sleep(0.6)
    after, current = await snapshot(connection, args.role, record['session_id'])
    if not after['binding_verified'] or (after['session_id'], after['tty'], after['agent_pid']) != expected or working(after['screen'], after['name']):
        raise RuntimeError('qoder identity changed before marker; CR not sent')
    await current.async_send_text(' ' + marker, suppress_broadcast=True)
    await asyncio.sleep(0.6)
    checked, current = await snapshot(connection, args.role, record['session_id'])
    if (not marker_visible(checked['screen'], marker) or not checked['binding_verified'] or
            (checked['session_id'], checked['tty'], checked['agent_pid']) != expected or
            working(checked['screen'], checked['name'])):
        raise RuntimeError('qoder marker not visible; CR not sent')
    await current.async_send_text('\r', suppress_broadcast=True)
    print(json.dumps({'body_sent_once': True, 'cr_sent_once': True,
                      'identity': expected, 'body_sha256': digest}, ensure_ascii=False))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=['read', 'send', 'remind-handover'])
    p.add_argument('--role', required=True)
    p.add_argument('--session-id')
    p.add_argument('--expected-pid', type=int)
    p.add_argument('--file', type=Path)
    p.add_argument('--dry-run', action='store_true')
    args = p.parse_args()
    if args.action == 'remind-handover' and (args.file or not args.expected_pid):
        p.error('remind-handover requires expected PID and forbids a message file')
    if args.action == 'send' and (not args.file or not args.expected_pid):
        p.error('send requires --file and --expected-pid')
    try:
        iterm2.run_until_complete(lambda c: main(c, args), retry=False)
    except (RuntimeError, subprocess.SubprocessError) as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(2)
