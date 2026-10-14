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
