#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
devin_iterm_watch.py  (macOS 版)
================================
Devin CLI「免费模型限流」看门狗 —— 绑定你**已经在 iTerm2 里跑的那个 devin 会话**，
限流后自动注入续跑消息。逻辑与 Windows 版 devin_terminal_watch.py 完全一致，
只是把「读屏 / 注入」两个原语换成 iTerm2 官方 Python API（与 iterm-mcp 底层同一套）。

原语映射（Windows → macOS）
--------------------------
  AttachConsole + ReadConsoleOutputCharacterW  →  session.async_get_screen_contents()
  AttachConsole + WriteConsoleInputW           →  session.async_send_text()
  Toolhelp32 枚举 devin.exe                    →  遍历 iTerm2 会话，按 jobName/commandLine 匹配 devin

流程
----
  每 N 秒抓一次目标 devin 会话的**当前屏幕**，命中下面任一触发 → 先注入正文，
  `--enter-delay` 后**单独**注入回车 → 冷却防重复：
    触发 A（限流）  ：出现限流特征 → 等窗口重置（解析 "reset in N minutes"，默认约 130s + 余量）后注入。
    触发 B（空闲即续跑）：devin 停在输入框（空闲提示可见）且屏幕连续 N 秒无变化 → 直接注入。
  触发 B 是本机新增：devin 停下时不一定留限流字样，只要“停了”就把它推回工作（默认开）。

前提 / 依赖
-----------
  1) iTerm2 → Settings ▸ General ▸ Magic ▸ 勾选 “Enable Python API”。（必须）
  2) 本脚本需用装了 `iterm2` 库的解释器运行：
       python3 -m venv .venv-iterm && ./.venv-iterm/bin/pip install iterm2
     （本目录已建好 .venv-iterm）

用法
----
  # 0) 先看现在有哪些 iTerm2 会话、各自屏幕末尾（确认能看到目标 devin）
  ./.venv-iterm/bin/python devin_iterm_watch.py --list

  # 1) 干跑：只检测 + 打印，不注入
  ./.venv-iterm/bin/python devin_iterm_watch.py --once --dry-run

  # 2) 正式看门狗（自动发现所有 devin 会话，每 20s 扫一次）
  ./.venv-iterm/bin/python devin_iterm_watch.py

  # 3) 只盯某个会话 / 调参 / 落盘日志
  ./.venv-iterm/bin/python devin_iterm_watch.py --session-id <ID> --interval 15 \\
        --wait 120 --log-file devin_iterm_watch.log

  # 4) 调「空闲即续跑」：停在输入框 45s 就续跑；或整体关闭
  ./.venv-iterm/bin/python devin_iterm_watch.py --idle-seconds 45
  ./.venv-iterm/bin/python devin_iterm_watch.py --no-idle-continue

  # 5) 真机注入自测：往输入框打标记(**不回车**) → 读回确认 → 退格清除
  ./.venv-iterm/bin/python devin_iterm_watch.py --selftest

  # 6) 调试：向首个目标会话发一个单独回车并回显屏幕
  ./.venv-iterm/bin/python devin_iterm_watch.py --press-enter

要点 / 安全
----------
- **只在限流、或“确实停在输入框且连续 N 秒无变化”时才注入**；正在跑（含 `esc twice to interrupt`）时绝不发消息。
- 在 iTerm2 里 —— 只要你手动点过任意会话，广播(broadcast)是关的，注入仅落在目标会话；
  脚本对所有注入都传 suppress_broadcast=True，进一步保证不会误发到其它会话。
- `--selftest` 只打标记、不回车、并退格清除，用来安全验证注入通道。
- 多个 devin 会话时按屏幕内容去重，同一块屏幕只处理一次。
"""

import argparse
import asyncio
import os
import random
import re
import sys
import time

try:
    import iterm2
except ImportError:  # pragma: no cover
    sys.stderr.write(
        "缺少 iterm2 库。请用装了该库的解释器运行，例如：\n"
        "  python3 -m venv .venv-iterm && ./.venv-iterm/bin/pip install iterm2\n"
        "  ./.venv-iterm/bin/python devin_iterm_watch.py --list\n")
    sys.exit(2)

VERSION = "1.0.0"
DEFAULT_INTERVAL = 20.0
DEFAULT_LINES = 40
DEFAULT_WAIT = 130.0
DEFAULT_MARGIN = 10.0
DEFAULT_COOLDOWN = 180.0
DEFAULT_CONTINUE_TEXT = "Please continue and non-stop"
DEFAULT_ENTER_KEY = "\r"          # 回车（CR）。若目标 TUI 不认，可试 --enter-key "\\n"
DEFAULT_MATCH = "devin"           # 会话匹配（jobName/commandLine，大小写不敏感）
DEFAULT_IDLE_SECONDS = 60.0       # 「空闲即续跑」阈值：屏幕不变这么久就认为“停了”
# devin 空闲（输入框为空、等输入）时的占位提示；命中它就认为会话已停下
IDLE_PROMPT_MARKERS = ("ask devin to build features",)

SOCKET_PATH = os.path.expanduser("~/Library/Application Support/iTerm2/private/socket")

PYAPI_HINT = """\
连不上 iTerm2。请确认：
  * iTerm2 已打开，且已开启 Python API：
      iTerm2 ▸ Settings ▸ General ▸ Magic ▸ 勾选 “Enable Python API”
    （勾选后**立即生效**，无需重启；若曾重启过 iTerm2 也请再确认一次）
  * 脚本与 iTerm2 在同一台机器上运行
  * 理论上文件应存在： %s
""" % SOCKET_PATH

# ----------------------------------------------------------------------------
# 限流特征（大小写不敏感），与 Windows 版一致
# ----------------------------------------------------------------------------
RATE_LIMIT_PATTERNS = [re.compile(p, re.I) for p in (
    r"reached free model rate limit",
    r"limit will reset in",
    r"send a message to retry",
    r"upgrade to max for higher limits",
    r"rate[ \-_]*limit",
    r"too many requests",
    r"\b429\b",
)]

# 当前“正在跑”的标记。限流提示会留在滚动缓冲区里，只要看到这个标记就说明已恢复。
WORKING_MARKERS = ("esc twice to interrupt",)


def ts():
    return time.strftime("%H:%M:%S")


def setup_encoding():
    for name in ("stdout", "stderr"):
        try:
            getattr(sys, name).reconfigure(encoding="utf-8", errors="replace",
                                           line_buffering=True)
        except Exception:
            pass


class Tee:
    def __init__(self, stream, path):
        self.stream = stream
        self.file = open(path, "a", encoding="utf-8", buffering=1)

    def write(self, data):
        try:
            self.stream.write(data)
        except Exception:
            pass
        try:
            self.file.write(data)
        except Exception:
            pass
        return len(data)

    def flush(self):
        try:
            self.stream.flush()
        except Exception:
            pass
        try:
            self.file.flush()
        except Exception:
            pass

    def fileno(self):
        return self.stream.fileno()


def setup_logging(log_file):
    if log_file:
        os.makedirs(os.path.dirname(os.path.abspath(log_file)), exist_ok=True)
        sys.stdout = Tee(sys.stdout, log_file)
        sys.stderr = Tee(sys.stderr, log_file)
        print(f"[{ts()}] 日志双写至: {os.path.abspath(log_file)}", flush=True)


# ----------------------------------------------------------------------------
# 判定 / 解析
# ----------------------------------------------------------------------------
def tail_has_rate_limit(text):
    return any(p.search(text) for p in RATE_LIMIT_PATTERNS)


def is_working(text):
    t = text.lower()
    return any(m in t for m in WORKING_MARKERS)


def at_idle_prompt(text, extra_markers=()):
    """当前屏是否是 devin 的空闲输入框（在等输入）状态。"""
    t = text.lower()
    return (any(m in t for m in IDLE_PROMPT_MARKERS) or
            any(m.lower() in t for m in extra_markers))


def parse_wait(text, base_wait, margin, jitter):
    if base_wait <= 0:          # --wait 0：不等待，命中即注入
        return 0.0
    wait = None
    m = re.search(r"reset in\s+(\d+)\s*(minute|minutes|min|second|seconds|sec|s\b)",
                  text, re.I)
    if m:
        n = int(m.group(1))
        unit = m.group(2).lower()
        wait = (n * 60.0 if unit.startswith("m") else float(n)) + margin
    if wait is None:
        wait = base_wait if base_wait > 0 else 0.0
    return wait + (random.uniform(0, jitter) if wait > 0 else 0)


def decode_key(s):
    """把命令行里写的 \\r / \\n / \\t 还原成真实控制字符。"""
    return (s.replace("\\r", "\r").replace("\\n", "\n").replace("\\t", "\t"))


# ----------------------------------------------------------------------------
# iTerm2 会话枚举 / 读屏 / 注入
# ----------------------------------------------------------------------------
def iter_sessions(app):
    for window in app.terminal_windows:
        for tab in window.tabs:
            for session in tab.all_sessions:
                yield session


async def session_str(session, name):
    try:
        v = await session.async_get_variable(name)
        return v if isinstance(v, str) else ("" if v is None else str(v))
    except Exception:
        return ""


async def session_identity(session):
    """返回 (显示名, jobName, commandLine)。"""
    return (await session_str(session, "session.name"),
            await session_str(session, "jobName"),
            await session_str(session, "commandLine"))


def looks_like_devin(name, job, cmd, pattern, case_sensitive):
    hay = " ".join(x for x in (name, job, cmd) if x)
    if case_sensitive:
        return re.search(pattern, hay) is not None
    return re.search(pattern, hay, re.I) is not None


async def discover_sessions(app, explicit_ids, watch_all, pattern, case_sensitive):
    found = []
    explicit = set(explicit_ids or [])
    for session in iter_sessions(app):
        sid = session.session_id
        if explicit:
            if sid in explicit:
                found.append(session)
            continue
        if watch_all:
            found.append(session)
            continue
        name, job, cmd = await session_identity(session)
        if looks_like_devin(name, job, cmd, pattern, case_sensitive):
            found.append(session)
    # 显式 id 里可能存在当前 app 已看不到的（如别的 window），提示一下
    if explicit:
        seen = {s.session_id for s in found}
        for missing in explicit - seen:
            print(f"  ⚠️ 未找到会话 id={missing}（可能已关闭或不属于当前 iTerm2）")
    return found


async def read_screen(session, max_lines):
    """读会话当前屏幕（可变区）内容，返回 list[str]；失败返回 None。"""
    try:
        contents = await session.async_get_screen_contents()
    except Exception as e:
        print(f"  读屏失败: {e!r}")
        return None
    n = contents.number_of_lines
    lines = [contents.line(i).string.rstrip() for i in range(n)]
    if max_lines and max_lines > 0:
        lines = lines[-max_lines:]
    return lines


async def send_message(session, body, enter_delay, enter_key):
    """先发正文，停顿后再**单独**发一个回车。
    整段一口气灌进去，可能被 TUI 的“快速输入=粘贴”启发式吞掉，末尾回车只算换行；
    分开且让回车单独到达，才能被当作真正的“回车提交”。（与 Windows 版坑 #1 同理）"""
    if body:
        await session.async_send_text(body, suppress_broadcast=True)
    await asyncio.sleep(max(enter_delay, 0.0))
    await session.async_send_text(enter_key, suppress_broadcast=True)


# ----------------------------------------------------------------------------
# 动作：列出
# ----------------------------------------------------------------------------
async def do_list(app, args):
    sessions = await discover_sessions(app, args.session_id, args.all_sessions,
                                       args.match, args.case_sensitive)
    if not sessions:
        print(f"没找到任何匹配 {args.match!r} 的 iTerm2 会话"
              f"（可用 --all-sessions 列出全部，或 --session-id 指定）。")
        return 1
    print(f"发现 {len(sessions)} 个匹配会话：\n")
    for session in sessions:
        print("=" * 70)
        name, job, cmd = await session_identity(session)
        tty = await session_str(session, "tty")
        print(f"session_id = {session.session_id}")
        print(f"  name={name!r}  jobName={job!r}  tty={tty}")
        if cmd:
            print(f"  commandLine={cmd!r}")
        screen = await read_screen(session, args.lines)
        if screen is None:
            print("  （读屏失败）")
            continue
        nonempty = [l for l in screen if l.strip()]
        print(f"  可见行数={len(screen)} 非空={len(nonempty)}")
        for l in nonempty[-8:]:
            print("  | " + l[:120])
    return 0


# ----------------------------------------------------------------------------
# 动作：自测注入（打标记→读回→退格清除，不回车）
# ----------------------------------------------------------------------------
async def do_selftest(app, args, marker="DEVINWATCH_SELFTEST"):
    sessions = await discover_sessions(app, args.session_id, args.all_sessions,
                                       args.match, args.case_sensitive)
    if not sessions:
        print("没找到目标会话。")
        return 1
    session = sessions[0]
    name, job, _ = await session_identity(session)
    print(f"目标会话: {session.session_id}  name={name!r} jobName={job!r}")
    print(f"1) 注入标记 {marker!r}（**不回车**，只放进输入框）...")
    try:
        await session.async_send_text(marker, suppress_broadcast=True)
    except Exception as e:
        print("   -> 注入失败:", repr(e))
        return 1
    print("   -> 已注入")
    await asyncio.sleep(1.5)
    print("2) 读回屏幕确认标记出现...")
    screen = await read_screen(session, 60) or []
    joined = "\n".join(screen)
    present = marker in joined
    print("   标记是否出现在屏幕上:", present)
    if present:
        for l in screen:
            if marker in l:
                print("   | " + l.strip()[:120])
    print("3) 退格清除标记（恢复输入框原状）...")
    try:
        await session.async_send_text("\x7f" * len(marker), suppress_broadcast=True)
        print("   -> 已发送退格")
    except Exception as e:
        print("   -> 退格失败:", repr(e))
    await asyncio.sleep(1.0)
    screen2 = await read_screen(session, 60) or []
    still = marker in "\n".join(screen2)
    print("   清除后标记是否还在:", still)
    print("\n自测结论：注入通道", "可用 ✅" if present else "可疑 ⚠️")
    return 0


async def do_press_enter(app, args):
    sessions = await discover_sessions(app, args.session_id, args.all_sessions,
                                       args.match, args.case_sensitive)
    if not sessions:
        print("没找到目标会话。")
        return 1
    session = sessions[0]
    print(f"向会话 {session.session_id} 发送单独回车...")
    try:
        await session.async_send_text(decode_key(args.enter_key), suppress_broadcast=True)
        print("  -> 已发送")
    except Exception as e:
        print("  -> 失败:", repr(e))
        return 1
    await asyncio.sleep(1.2)
    print("  屏幕末尾：")
    for l in (await read_screen(session, 30) or [])[-8:]:
        print("  | " + l[:120])
    return 0


# ----------------------------------------------------------------------------
# 主看门狗循环
# ----------------------------------------------------------------------------
async def sleep_countdown(seconds):
    """等待限流窗口。只在开始打印一行，之后静默 sleep。"""
    seconds = max(seconds, 0.0)
    if seconds <= 0:
        return
    print(f"[{ts()}] ⏳ 等待 {seconds:.0f}s（限流窗口）...", flush=True)
    await asyncio.sleep(seconds)


async def _inject(session, args, enter_key, state):
    """注入续跑消息并更新该会话的状态。"""
    if not getattr(args, "auto_continue", False):
        return
    sid = session.session_id
    try:
        await send_message(session, args.continue_text, args.enter_delay, enter_key)
        now = time.time()
        state["last_inject"] = now
        state["since"] = now
        state["hash"] = None   # 让下轮把当前屏当“刚变化”，避免立刻重复判定空闲
        print(f"[{ts()}] {sid}: ✅ 已注入续跑消息 {args.continue_text!r} + 回车")
    except Exception as e:
        print(f"[{ts()}] {sid}: ⛔ 注入失败：{e!r}")


async def run_watch(connection, app, args):
    enter_key = decode_key(args.enter_key)
    idle_markers = tuple(args.idle_marker or ())
    first = True
    seen_screens = {}  # screen-hash -> (sid, time)  限流同一屏去重
    st = {}            # sid -> {"hash", "since", "last_inject"}

    while True:
        if not first:
            await asyncio.sleep(max(args.interval, 1.0))
        first = False

        # 每轮重取 app，确保新开/关闭的会话能被看到
        try:
            app = await iterm2.async_get_app(connection)
        except Exception as e:
            print(f"[{ts()}] 获取 iTerm2 app 失败：{e!r}")
            continue

        sessions = await discover_sessions(app, args.session_id, args.all_sessions,
                                           args.match, args.case_sensitive)
        if not sessions:
            if args.once:
                print("没找到目标 devin 会话。")
                return 1
            continue

        now = time.time()
        hit = False
        for session in sessions:
            sid = session.session_id
            screen = await read_screen(session, args.lines)
            if screen is None:
                continue
            tail = "\n".join(screen)
            h = hash(tail)
            working = is_working(tail)
            rate_limited = tail_has_rate_limit(tail)

            state = st.setdefault(sid, {"hash": None, "since": now, "last_inject": 0.0})
            if state["hash"] != h:
                state["hash"] = h
                state["since"] = now
            stable_for = now - state["since"]

            if rate_limited:
                hit = True
            if working:
                # 正在跑（屏幕含 "esc twice to interrupt"）→ 什么都不做
                continue

            # -------- 触发 A：限流 --------
            if rate_limited:
                prev = seen_screens.get(h)
                if prev and (now - prev[1]) < 60:
                    continue
                seen_screens[h] = (sid, now)
                if now - state["last_inject"] < args.cooldown:
                    print(f"[{ts()}] {sid}: 命中限流，但仍在冷却期，跳过")
                    continue
                wait = parse_wait(tail, args.wait, args.margin, args.wait_jitter)
                print(f"[{ts()}] {sid}: ⚠️ 命中限流特征 → "
                      f"{'（dry-run 不注入）' if args.dry_run else f'等待 {wait:.0f}s 后注入续跑'}")
                for l in screen:
                    if tail_has_rate_limit(l):
                        print("     命中行 | " + l.strip()[:110])
                if args.dry_run:
                    continue
                if wait > 0:
                    await sleep_countdown(wait)
                # 等待后再读一次：若限流提示已消失，说明已恢复/已被处理，跳过注入
                recheck = await read_screen(session, args.lines)
                if recheck is not None and not tail_has_rate_limit("\n".join(recheck)):
                    print(f"[{ts()}] {sid}: 等待后限流提示已消失，跳过注入。")
                    continue
                await _inject(session, args, enter_key, state)
                continue

            # -------- 触发 B：空闲停在输入框（等输入）--------
            if args.idle_continue and at_idle_prompt(tail, idle_markers):
                if stable_for < args.idle_seconds:
                    continue
                if now - state["last_inject"] < args.cooldown:
                    continue
                print(f"[{ts()}] {sid}: 💤 空闲 {stable_for:.0f}s（停在输入框）→ "
                      f"{'（dry-run 不注入）' if args.dry_run else '注入续跑'}")
                if args.dry_run:
                    continue
                await _inject(session, args, enter_key, state)
                continue

        if args.once:
            if not hit:
                print(f"[{ts()}] 已扫描 {len(sessions)} 个会话：未命中限流；"
                      f"空闲续跑{'已启用' if args.idle_continue else '已关闭'}。")
            return 0


def print_watch_header(args, n_sessions):
    print("=" * 70)
    print(f"Devin iTerm2 看门狗（绑定现有会话）  v{VERSION}")
    print("=" * 70)
    print(f"目标会话 : {n_sessions} 个（匹配 {args.match!r}"
          f"{'，全部会话' if args.all_sessions else ''}）")
    print(f"轮询间隔 : {args.interval:.0f}s   扫描行数: {args.lines}")
    print(f"限流等待 : 基准 {args.wait:.0f}s（解析到 reset 时间则用之）")
    print(f"续跑文本 : {args.continue_text!r} + 回车（{decode_key(args.enter_key)!r}）")
    print(f"冷却     : {args.cooldown:.0f}s（防重复注入）")
    idle = (f"开，阈值 {args.idle_seconds:.0f}s（停在输入框即续跑）"
            if args.idle_continue else "关")
    print(f"空闲续跑 : {idle}")
    print(f"模式     : {'干跑(dry-run，不注入)' if args.dry_run else '实弹(命中限流/空闲都会自动注入)'}")
    print("Ctrl+C 退出\n", flush=True)


# ----------------------------------------------------------------------------
# 分发 / 入口
# ----------------------------------------------------------------------------
async def dispatch(connection, args):
    app = await iterm2.async_get_app(connection)
    if args.list:
        return await do_list(app, args)
    if args.selftest:
        return await do_selftest(app, args)
    if args.press_enter:
        return await do_press_enter(app, args)

    sessions = await discover_sessions(app, args.session_id, args.all_sessions,
                                       args.match, args.case_sensitive)
    if not sessions:
        print("没找到任何 devin 会话。请先在 iTerm2 里跑起 devin，"
              "或用 --all-sessions / --session-id 指定。\n"
              "可先执行 --list 看看有哪些会话。")
        return 1
    if not args.once:
        print_watch_header(args, len(sessions))
    return await run_watch(connection, app, args)


def run(args):
    if not os.path.exists(SOCKET_PATH):
        sys.stderr.write(PYAPI_HINT)
        return 1
    reconnect = not args.no_reconnect and not args.once
    while True:
        try:
            conn = iterm2.Connection()
            code = conn.run_until_complete(lambda c: dispatch(c, args), retry=False)
            return code if isinstance(code, int) else 0
        except ConnectionRefusedError:
            if not reconnect:
                sys.stderr.write(PYAPI_HINT)
                return 1
            print(f"[{ts()}] iTerm2 连接不可用（可能刚重启），5s 后重试…（Ctrl+C 退出）",
                  flush=True)
            try:
                time.sleep(5)
            except KeyboardInterrupt:
                return 130
        except KeyboardInterrupt:
            print(f"\n[{ts()}] 已停止。")
            return 130


def main():
    p = argparse.ArgumentParser(
        description="Devin iTerm2 看门狗：绑定已存在的 devin 会话，限流后自动注入续跑消息",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--session-id", action="append", default=None,
                   help="只盯指定 iTerm2 session_id（可重复）；默认自动发现 devin 会话")
    p.add_argument("--all-sessions", action="store_true",
                   help="盯所有 iTerm2 会话（不按 --match 过滤）")
    p.add_argument("--match", default=DEFAULT_MATCH,
                   help=f"会话匹配正则，作用于 name/jobName/commandLine（默认 {DEFAULT_MATCH!r}）")
    p.add_argument("--case-sensitive", action="store_true",
                   help="--match 区分大小写（默认不区分）")
    p.add_argument("--interval", type=float, default=DEFAULT_INTERVAL,
                   help=f"轮询间隔秒（默认 {DEFAULT_INTERVAL:.0f}）")
    p.add_argument("--lines", type=int, default=DEFAULT_LINES,
                   help=f"每次扫描末尾行数（默认 {DEFAULT_LINES}）")
    p.add_argument("--wait", type=float, default=DEFAULT_WAIT,
                   help=f"限流后基础等待秒（默认 {DEFAULT_WAIT:.0f}；0=立即注入）")
    p.add_argument("--wait-jitter", type=float, default=10.0,
                   help="等待抖动上限秒（默认 10）")
    p.add_argument("--margin", type=float, default=DEFAULT_MARGIN,
                   help=f"解析到 reset 时间后额外加的保险秒（默认 {DEFAULT_MARGIN:.0f}）")
    p.add_argument("--continue-text", default=DEFAULT_CONTINUE_TEXT,
                   help=f"命中限流后注入的消息（默认 {DEFAULT_CONTINUE_TEXT!r}）")
    p.add_argument("--enter-delay", type=float, default=0.6,
                   help="正文与回车之间的间隔秒（默认 0.6，分开注入避免被当成粘贴）")
    p.add_argument("--enter-key", default="\\r",
                   help=r"回车键内容，支持 \r \n（默认 '\r'，即 CR）")
    p.add_argument("--cooldown", type=float, default=DEFAULT_COOLDOWN,
                   help=f"同一会话两次注入的最小间隔秒（默认 {DEFAULT_COOLDOWN:.0f}）")
    p.add_argument("--auto-continue", action="store_true",
                   help="显式启用自动续发（默认关闭；否则仅检测，不注入消息）")
    p.add_argument("--idle-continue", action=argparse.BooleanOptionalAction, default=False,
                   help="空闲即续跑：devin 停在输入框且屏幕 N 秒无变化 → 自动注入续跑"
                        "（默认关；还需 --auto-continue 才会注入）")
    p.add_argument("--idle-seconds", type=float, default=DEFAULT_IDLE_SECONDS,
                   help=f"空闲多久算“停了”（默认 {DEFAULT_IDLE_SECONDS:.0f}s）")
    p.add_argument("--idle-marker", action="append", default=None,
                   help="额外的“空闲输入框”提示语（可重复；默认内置 devin 的占位提示）")
    p.add_argument("--dry-run", action="store_true",
                   help="只检测 + 打印，不注入")
    p.add_argument("--once", action="store_true",
                   help="只扫描一轮就退出")
    p.add_argument("--no-reconnect", action="store_true",
                   help="持续模式下 iTerm2 连接断开后不自动重连")
    p.add_argument("--list", action="store_true",
                   help="列出发现的会话及各自屏幕末尾几行，然后退出")
    p.add_argument("--selftest", action="store_true",
                   help="真机注入自测：打标记(不回车)→读回确认→退格清除")
    p.add_argument("--press-enter", action="store_true",
                   help="调试：向首个目标会话发送一个单独回车并回显屏幕")
    p.add_argument("--log-file", default=None, help="日志双写到该文件")
    p.add_argument("--version", action="version", version=f"devin_iterm_watch {VERSION}")
    args = p.parse_args()

    # A completed handover must stay idle until the coordinator dispatches.
    # Old launch commands now monitor only; rate-limit retry is also opt-in.
    if not args.auto_continue:
        args.dry_run = True

    setup_encoding()
    setup_logging(args.log_file)
    sys.exit(run(args))


if __name__ == "__main__":
    main()
