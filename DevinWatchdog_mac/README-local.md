# macPad 本地终端与 SSH 工具

当前协议：用户现有定时任务唤醒，**仅在明确 handover 后集中审核并发送一次任务指针**。执行中不guide、不flush、不代关菜单。详见 coordination/README.md。

## 只读发现与重新绑定

```sh
python3 DevinWatchdog_mac/macpad_devin_relay.py read --json
python3 DevinWatchdog_mac/devin_iterm_watch.py --list --no-reconnect
```

PID重启会变化；relay动态验证唯一macPad移植标题、session、tty、ACP进程及其cwd。本机python3已安装iterm2。未启动常驻watchdog，不要无参数运行旧devin_iterm_watch.py（它会自动续跑，违反当前规则）。

## 交接后发送一次

```sh
python3 DevinWatchdog_mac/macpad_devin_relay.py send coordination/next-pointer.txt \
  --expected-session-id <fresh-id> --expected-pid <fresh-acp-pid> --dry-run
```

真实目标必须明确输出`(handover to codex now)`并回到空闲。新会话、普通空闲、排队输入、工作中、菜单、引用的标记均不能放行。只允许单行短指针；dry-run通过后去掉该选项。正文一次、单独可见hash标记、重核身份后独立CR；不自动重贴或催任务。并发发送由本地锁阻止。

## 保留真实退出码的 SSH / SCP

`macpad_ssh.py`从临时环境读取MACWS_DEVICE、MACWS_DEVICE_PORT（默认2222）、MACWS_SSH_PASSWORD，不把密码放argv或文件。

```sh
python3 DevinWatchdog_mac/macpad_ssh.py --command 'uname -m' --record coordination/artifacts/device-identity.json
python3 DevinWatchdog_mac/macpad_ssh.py --script coordination/device-check.sh --timeout 60 --record coordination/artifacts/device-check.json
python3 DevinWatchdog_mac/macpad_ssh.py --upload misc/macos_ws_event_probe --remote-path /tmp/macos_ws_event_probe.new --timeout 30 --record coordination/artifacts/probe-upload.json
```

远端脚本通过stdin交给sh -s，应使用set -eu并显式处理预期失败；管道末端状态不是原进程退出码。SCP固定password authentication，输出源哈希及真实退出码。上传成功不等于部署成功；仍需新文件签名/trust/原子安装及远端哈希核对。

JSON会替换当前地址/密码，提交前仍需检查其它私人信息。超时返回124并清理本地SSH进程组，不证明远端长任务已退出；须核验其真实PID/handle。工具不会自动重试或建立常驻代理。
