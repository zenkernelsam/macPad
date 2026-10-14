# macPad 本地终端工具

复制自 ShadowRocket 协作流程使用的 DevinWatchdog_mac。当前主机的 python3 已安装 iterm2，无须旧文档中的 .venv-iterm。

在 macPad 仓库根目录只读列出 Devin 会话：

```sh
python3 DevinWatchdog_mac/devin_iterm_watch.py --list --no-reconnect
```

macpad_devin_relay.py 是本轮建立的精确会话派发工具。它绑定本次核验的 session ID 和 tty，使用 suppress_broadcast；send 仅在确认空闲时发送指定消息文件。会话重建后必须重新核验 PID、tty、标题和 session ID 再更新绑定。

```sh
python3 DevinWatchdog_mac/macpad_devin_relay.py read
# 仅在移植工作获准继续、需要派发新工作包时执行：
python3 DevinWatchdog_mac/macpad_devin_relay.py send coordination/dispatch-message.txt
```

未安装或启动常驻看门狗，未复制指向旧目录的 launchd plist。无参数运行 devin_iterm_watch.py 会开启自动续跑；任务暂停期间不要运行它。

工作中发送指导可使用 `guide MESSAGE_FILE`（只在屏幕确认 Guide Devin 时允许）。若 TUI 将消息排队，先 read 核验 `Press Enter to send queued messages now`，再用 `flush` 递送已排队消息；这不会重启 Devin。任何 send/guide 都不会把消息正文打印到工具日志。敏感授权不要通过消息文件落盘。

## SSH：保留真实退出码

`macpad_ssh.py` 使用现有 ssh/sshpass。仅从临时环境读取 `MACWS_DEVICE`、`MACWS_DEVICE_PORT`（默认2222）、`MACWS_SSH_PASSWORD`；不把密码放 argv 或文件。例：在环境已配置后运行

```sh
python3 DevinWatchdog_mac/macpad_ssh.py --command 'uname -m' --record coordination/artifacts/device-identity.json
python3 DevinWatchdog_mac/macpad_ssh.py --script coordination/device-check.sh --timeout 60 --record coordination/artifacts/device-check.json
```

`--script` 通过 stdin 交给远端 `sh -s`；脚本开头应设置 `set -eu`，预期失败用显式分支处理。工具保留远端 shell 的最终退出码，但不会改变 `false; true` 等 shell 组合的语义。构建、签名、部署各步骤分别记录退出码，或明确 fail-fast。输出 JSON 会替换当前连接地址/密码，但提交前仍需人工检查其他私人信息。

已在设备验证：远端 `exit 0` → 工具0；远端 `exit 7` → 工具7，证据见 coordination/artifacts/ssh-exit-*.json。超时返回124并清理本地进程组；这不证明远端长任务已停止，仍须核验远端PID。工具不会启动常驻代理。
