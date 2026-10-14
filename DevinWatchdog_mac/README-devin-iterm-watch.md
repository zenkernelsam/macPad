# Devin iTerm2 看门狗 —— 绑定「正在跑的 devin 会话」自动续跑（macOS 版）

> 解决 Devin CLI 整夜无人值守时，被**免费模型限流**打断、需手动补消息的问题。
> 脚本：`devin_iterm_watch.py`（本目录）
> 状态：**已在真机端到端验证成功**（见文末「验证记录」）
> 对照：Windows 版 = `devin_terminal_watch.py`（`AttachConsole` 路线）；本文件是它的 macOS 孪生版。

---

## 一、它做什么

不去改 Devin、不抓包、不装 CA。它**直接绑定你已经开着的那个 devin 会话**：

1. 每 N 秒把目标 devin 会话的**当前屏幕**抓下来（= 你要的「最后几十行」）；
2. 命中下面**任一触发**（正在跑 `esc twice to interrupt` 时一律跳过）：
   - **限流**：出现限流特征（`Reached free model rate limit` / `Send a message to retry` …）
     → 等到窗口重置（解析 `reset in N minutes/seconds`，默认约 130s + 余量）后注入；
   - **空闲即续跑**：devin 停在输入框（如 `❭ Ask Devin to build features…`）且屏幕连续 N 秒无变化
     → 立即注入（默认开，N=60s）。
3. 往那个会话**注入** `Please continue and non-stop` + 回车；
4. 冷却一段时间防重复。

> **为什么有两个触发**：devin 停下时**不一定**会留限流字样——它常常只是静静停在输入框等输入。
> 只认限流文本会漏掉这种情况，所以新增「空闲即续跑」：只要它停了，就把它推回工作。

Devin 侧完全无感：它只是「收到了一条用户消息」，对话从断点继续，整夜推进不中断。

逻辑与参数与 Windows 版**完全一致**，只是把「读屏 / 注入」两个原语换掉。

## 二、原理（原语映射）

Windows 上靠 `AttachConsole` 附着到别的进程的 console。macOS 没有这个 API，
等价物是 **iTerm2 官方 Python API**（你贴的 [iterm-mcp](https://github.com/ferrislucas/iterm-mcp)
底层用的也是同一套 API，只是它由 AI agent 驱动、不适合常驻看门狗）：

| 能力 | Windows | macOS（本脚本） |
|------|---------|-----------------|
| 读目标终端当前屏幕 | `AttachConsole` + `ReadConsoleOutputCharacterW` | `session.async_get_screen_contents()` |
| 往目标终端注入按键 | `AttachConsole` + `WriteConsoleInputW` | `session.async_send_text()` |
| 发现「哪些终端在跑 devin」 | Toolhelp32 枚举 `devin.exe` | 遍历 iTerm2 会话，按 `jobName`/`commandLine` 匹配 `devin` |

> 为什么不用 iterm-mcp：MCP server 是「一问一答」的、由 agent 调用的，
> 而看门狗是**无人值守的常驻进程**。所以这里直接调同一套 iTerm2 API，把它写成独立脚本。

## 三、一次性前置（必须）

1. **开启 iTerm2 的 Python API**：
   `iTerm2 ▸ Settings(⌘,) ▸ General ▸ Magic ▸ 勾选 “Enable Python API”`。
   弹授权框时选「允许」/「Allow all apps to connect」。
   > 实测在这台机器上：勾选后**需要重启一次 iTerm2** 才会真正建立 API socket
   > （`~/Library/Application Support/iTerm2/private/socket`）。重启会结束当前终端里的进程，
   > 请先确认没有正在跑、不能中断的任务。

2. **准备解释器**（本目录已建好 `.venv-iterm`，可跳过）：
   ```bash
   cd /Users/ciscohe/Desktop/Patch/anyrouter-squeeze/DevinWatchdog_mac
   python3 -m venv .venv-iterm
   ./.venv-iterm/bin/pip install iterm2
   ```

确认通道就绪（应能看到 `private/socket`）：
```bash
ls -la ~/Library/Application\ Support/iTerm2/private/socket
```

## 四、用法

```bash
cd /Users/ciscohe/Desktop/Patch/anyrouter-squeeze/DevinWatchdog_mac
PY=./.venv-iterm/bin/python

# 0) 先看有哪些 iTerm2 会话、各自屏幕末尾（确认能看到目标 devin）
$PY devin_iterm_watch.py --list

# 1) 干跑：只检测 + 打印，不注入
$PY devin_iterm_watch.py --once --dry-run

# 2) 正式看门狗（自动发现所有 devin 会话，每 20s 扫一次）
$PY devin_iterm_watch.py

# 3) 只盯某个 session_id / 调参 / 落盘日志
$PY devin_iterm_watch.py --session-id 0039E0D4-6223-4FAA-9577-E76E90D8A67A \
      --interval 15 --wait 120 --log-file devin_iterm_watch.log

# 4) 真机注入自测：往输入框打标记(**不回车**) → 读回确认 → 退格清除
$PY devin_iterm_watch.py --selftest

# 5) 调试：向首个目标会话发一个单独回车并回显屏幕
$PY devin_iterm_watch.py --press-enter

# 6) 盯所有会话（不做 devin 过滤）/ 自定义匹配
$PY devin_iterm_watch.py --all-sessions
$PY devin_iterm_watch.py --match 'coffee-soap'
```

## 五、参数

| 参数 | 默认 | 说明 |
|------|------|------|
| `--session-id` | 自动发现 | 只盯指定 iTerm2 session_id（可重复） |
| `--all-sessions` | 关 | 盯所有会话，不按 `--match` 过滤 |
| `--match` | `devin` | 会话匹配正则，作用于 name/jobName/commandLine |
| `--case-sensitive` | 关 | `--match` 区分大小写（默认不区分） |
| `--interval` | `20` | 轮询间隔秒 |
| `--lines` | `40` | 每次扫描末尾行数 |
| `--wait` | `130` | 限流后基础等待秒；`0`=命中即注入（不等待） |
| `--margin` / `--wait-jitter` | `10` / `10` | 解析到 reset 时间后的保险秒 / 抖动秒 |
| `--continue-text` | `Please continue and non-stop` | 命中限流后注入的消息 |
| `--enter-delay` | `0.6` | **正文与回车之间的间隔秒**（关键，见坑 #1） |
| `--enter-key` | `\r` | 回车键内容（支持 `\r` `\n`）；实测 `\r`(CR) 正确 |
| `--cooldown` | `180` | 同一会话两次注入最小间隔秒 |
| `--idle-continue` / `--no-idle-continue` | 开 | 空闲即续跑总开关 |
| `--idle-seconds` | `60` | 停在输入框且屏幕稳定多久算“停了” |
| `--idle-marker` | 内置 | 额外的“空闲输入框”提示语（可重复） |
| `--dry-run` / `--once` | | 只检测不注入 / 只扫一轮 |
| `--no-reconnect` | 关 | 持续模式下 iTerm2 断开后不自动重连 |
| `--list` / `--selftest` / `--press-enter` | | 见上文 |
| `--log-file` | 无 | 日志双写文件 |

## 六、踩坑记录（Mac 实测）

1. **正文+回车一口气灌入 → 被 TUI 的「快速输入=粘贴」启发式吞掉 → 消息发不出去。**
   修复：**分两次注入**——先发正文，`--enter-delay`(默认0.6s) 后再**单独**发一个回车。
   （与 Windows 坑 #1 同源，`async_send_text` 写的是 pty 输入流，同样会被 TUI 当作粘贴。）

2. **回车字节要选对。** 用隔离会话 + raw 模式读字节实测：`async_send_text("\r")`
   发到 pty 的是 **0x0D (CR)**——正是 raw 模式 TUI（devin）认的回车。默认即成。

3. **恢复后限流提示仍留在滚动缓冲区 → 会被误判为「又限流」而反复注入。**
   修复：只要当前屏出现「正在跑」标记 `esc twice to interrupt` 就跳过；配合 `--cooldown` 双保险。

4. **API 打开后仍连不上。** 本机实测：勾选 `Enable Python API` 后**要重启一次 iTerm2**
   才生成 `private/socket`（`defaults write` 写偏好**不会**热生效）。脚本在 socket 不存在时
   会打印明确指引并友好退出，而不是抛一堆栈。

5. **读屏的 `session.tty` 不存在。** 需用变量 `async_get_variable("tty")` 取（脚本已处理）。

6. **devin 停下时不一定会留限流字样。** 实测它常常只是静静停在输入框（`❭ Ask Devin to build features…`），
   此时若只认限流文本，看门狗不会动作。故新增「空闲即续跑」：以「空闲提示可见 + 屏幕连续 N 秒无变化」
   作为「停了」的判据，自动推回工作。注意此时底线是 `--cooldown`（默认180s）防死循环式反复注入。

## 七、安全与注意

- **只在检测到限流特征时才注入**；正常工作时绝不发消息。
- 脚本**只读屏 + 注入按键**，不删会话、不改文件、不动 `sessions.db`。
- 所有注入都带 `suppress_broadcast=True`：即便你开着 iTerm2 的**广播(broadcast)**，
  也不会误发到其它会话。
- 多个 devin 会话时按屏幕内容去重，同一块屏幕只处理一次。

## 八、让它常驻运行（关键！）

看门狗是**常驻进程**：**必须一直开着它**，才会在限流时「自动续跑」。它不会自己后台守着。

**A. 自己开一个终端跑（推荐，能实时看日志）**
```bash
cd /Users/ciscohe/Desktop/Patch/anyrouter-squeeze/DevinWatchdog_mac
./.venv-iterm/bin/python devin_iterm_watch.py --log-file devin_iterm_watch.log
```

**B. 脱离式后台常驻（跨会话存活）**
```bash
cd /Users/ciscohe/Desktop/Patch/anyrouter-squeeze/DevinWatchdog_mac
nohup ./.venv-iterm/bin/python devin_iterm_watch.py --log-file devin_iterm_watch.log \
      >/dev/null 2>&1 &
# 查看 / 停止
pgrep -lf devin_iterm_watch
pkill -f devin_iterm_watch
```

**C. launchd 开机自启（可选）** —— 仓库内已附 `com.ciscohe.devin-iterm-watch.plist`：
```bash
cp com.ciscohe.devin-iterm-watch.plist ~/Library/LaunchAgents/
launchctl load ~/Library/LaunchAgents/com.ciscohe.devin-iterm-watch.plist
launchctl list | grep devin-iterm-watch      # 查看
launchctl unload ~/Library/LaunchAgents/com.ciscohe.devin-iterm-watch.plist  # 停止
```
> 注意：launchd 里跑的是**那个 venv 的解释器**，所以 `iterm2` 库随 venv 一起可用；
> plist 里已把工作目录与日志路径写死到本目录。

---

## 验证记录（2026-09-30，真机）

环境：macOS 15.6.1 (arm64) · iTerm2 **3.7.3** · `iterm2` 库 2.25 · 目标会话 `devin -r coffee-soap`。

- **前置**：`Enable Python API` 开启后需重启 iTerm2，`private/socket` 才出现；随后 `iterm2.Connection` 成功连上。
- **`--list`**：发现 1 个匹配会话 → `session_id=0039E0D4-…`、`jobName='devin'`、
  `commandLine='devin -r coffee-soap'`、`tty=/dev/ttys000`，成功读到 25 行屏幕内容。
- **`--selftest`**：注入 `DEVINWATCH_SELFTEST` → 读回出现 `❭ DEVINWATCH_SELFTEST` → 退格清除干净。
  结论：**读屏 + 注入通道可用 ✅**
- **回车字节**：新建隔离 iTerm2 窗口跑 raw 读字节程序，注入 `hi` + `\r` → 收到 `HEX=68690d`
  （`68 69` = `hi`，`0d` = CR）。结论：**回车就是 CR，raw TUI 认它 ✅**
- **`--once --dry-run`**：正确报告「已扫描 1 个会话，未命中限流特征」（当前未限流，零误注入）。
- **持续模式**：`--interval 3 --dry-run` 连跑多轮，表头/循环稳定无异常。
- **空闲即续跑（端到端真实注入）**：devin 停在输入框时，看门狗检测到 `💤 空闲 60s（停在输入框）`
  并自动注入 `Please continue and non-stop` + 回车；随后 devin 屏幕变为
  `⢰ Thinking · 46s (esc twice to interrupt)`、输入框提示变为 `❭ Guide Devin while it works`
  —— **devin 被成功推回工作 ✅**
