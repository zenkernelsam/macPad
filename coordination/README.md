# macPad 协调与交接规则

用户分工：Devin 执行实现、构建与设备实验；Codex 等候交付后集中审核、记录证据、规划下一包。用户2026-10-15明确要求参考 GPU_Rework，**等 Devin 明确 handover 后再派发**。这条覆盖此前忙碌期间追加 review/guide、强制提交 queued 消息及预审打断的做法。

## 唤醒与状态

沿用用户已经设置的定时任务，**不创建或修改自动化**。唤醒时点由现有任务负责，relay 不叠加第二个硬性倒计时；每次实际唤醒仅做一次有界状态检查。未交接不派发，不在内部循环轮询/睡眠催进度。用户要求继续时，goal 的 blocked 标签本身不是停止理由；交接边界仍保留。

- WORKING / REVISING：只做有界身份/状态读取，记录必要的当前状态；不深审移动中的候选、不追加反馈、不派新任务、不重复发送。
- READY_FOR_REVIEW：Devin 冻结源码、更新候选HEAD/源码指纹/真实测试记录，最后独立一行输出 `(handover to codex now)`（或用户指定的 `(handover to codex)`）。需在本次实际目标会话屏幕看到，不能用旧滚屏、文档引用或单纯 Ask Devin 代替。
- 审核：核对候选和证据；发现集中写一份完整报告，明确批准范围、失败/不可用/超时、复现命令和下个任务包。测试前后候选必须一致。
- 派发：只发一行报告/任务文件指针；一份交接只发送一次。发完等下一次明确交接。不要把长篇逐项指令不断灌进终端。
- 用户明确的暂停/停止指令优先；定时唤醒不构成恢复授权。

## 每次重新绑定终端

使用 `python3 DevinWatchdog_mac/macpad_devin_relay.py read --json`。它动态查找唯一 macPad移植 会话，并交叉核对：标题、session ID、tty、当前 Devin ACP PID、实际 cwd==本仓库和屏幕。重启后 PID 必须重新读取；旧PID和tty都不是单独的角色依据。其它项目会话始终排除。

只读检查可使用原 `devin_iterm_watch.py --list --no-reconnect`。该原始看门狗的无参数自动续跑、idle-continue、press-enter/selftest 不属于当前协议，不要启动。

唯一正常发送入口：

```sh
python3 DevinWatchdog_mac/macpad_devin_relay.py send coordination/next-pointer.txt \
  --expected-session-id <fresh-session-id> --expected-pid <fresh-acp-pid> --dry-run
# 只有 handover 门禁通过后，去掉 --dry-run 发一次。
```

消息正文为单行、至多600字符的任务/报告指针。工具验证当前交接、身份与cwd，正文只写一次，单独追加可见hash标记，重新核验后才发送一次CR；全部 suppress_broadcast=True。标记缺失或身份改变则不发送CR、不自动重贴。旧guide/flush/dismiss命令已移除。用户正在操作菜单时不代按键。

## 文档与证据

- 每次实际定时唤醒/恢复：更新 REVIEWER_STATE.md 的已核验状态；无新交付写“等待交接”，不制造新审计或反复通知。
- 每个实际里程碑、失败、否定假设或交接：记录时间/时区、设备/OS/rootfs、UUID/哈希、候选HEAD/指纹、完整命令和真实退出码、原始日志位置、结论及限制。
- 运行证据、静态反汇编、源码推断、端到端验收分开。原始返回值/失败不能被管道尾部状态替代。仅有口述结果不予批准。
- 报告集中存放 reviews/ 与 artifacts/；当前工作只维护一个 DEVIN_TASK.md，审核意见积累到下一次交接后一次性给出。执行中不要改当前任务来暗中追加要求。
- Git写入在交接边界指定唯一持有者；不在执行中反复发送锁/解锁提醒。只提交明确路径，保留无关WIP，不force push/reset --hard。认证未解决时明确本地已提交/远端未同步。
- 地址、密码、私人主机名不入库。证据脱敏后保存；SSH凭据只在临时环境或用户授权会话内传递。

## 方法参考及边界

参考 `GPU_Rework/AMD-next/tools/iterm_relay_send.py` 的身份、单次正文、可见标记与独立CR机制，以及 `/Users/ciscohe/Nextcloud/Agent/Devin-Audit-Relay-Methodology.md` 的工作/交接/审核状态机。参考文件是方法资料，不会自动授权 GPU 项目操作、修改其暂停状态、开启常驻自动续跑或改变现有调度。文档中的忙碌review模式和idle-continue不采用，以用户本次等待交接要求为准。

原目标仍未完成：先取得真实WS-DISPATCH+可见输入效果，再完成剩余pacing/GPU-fence、壁纸重建和会话/激活验收。诊断、构建或文档检查点不能替代这些端点。

2026-10-15 流程衔接：旧R005任务未要求新口令。收到继续推进的指令后，在已完成且空闲的同一会话仅补了一次交接确认（非新任务）；Devin随后明确输出标记。此次过渡已结束，后续任务均预先包含最后一行标记要求。通用派发门禁未放宽，不会重复过渡提醒。记录：handover-transition.json。
