# macPad 指挥、审核与复现入口

用户分工：Codex 负责规划、派发、独立审核、交接；现有 iTerm2「macPad移植」Devin 负责实现、构建、SSH 实验和设备部署。优先在此 macPad 工作目录保存工具与产物。授权范围内的操作不反复确认；凭据仅在会话中传递，不写 Git。

## 新 Agent 从这里恢复

1. 完整阅读根 AGENTS.md、CLAUDE.md、README.md。
2. 阅读 docs/porting/PROJECT-ARCHAEOLOGY.md、ventura-22F82-session-reproduction-20261013.md，以及最新 dated evidence。旧总结若冲突，以当前源码及精确运行证据为准。
3. 阅读本目录 REVIEWER_STATE.md、最新 reviews/、DEVIN_TASK.md 和 DEVIN_STATUS.md。查看 git status/log，不假设上次暂停后无人改动。
4. 用 ps 的 PID/PPID/tty 与 `python3 DevinWatchdog_mac/devin_iterm_watch.py --list --no-reconnect` 的 session ID、tty、标题交叉确认。先读屏再派发。WORKING 文件、PID 存在和旧报告均不足以证明仍在执行。
5. 只向核验的单一会话发送消息，suppress_broadcast=True，正文与 CR 分开；进程忙时不重复启动同一任务。用户暂停时停止新实验并传递暂停指令；恢复后重新核验现场。

## 文档更新节奏（用户明确要求）

- 每次恢复：更新 REVIEWER_STATE.md 中的已核验 HEAD、现场状态、当前包及下一步。
- 每次派发：先把目标、证据、边界、验收写入 DEVIN_TASK.md；归档上一工作包/审核结论。
- 每个里程碑、否定假设、部署、失败或清理：执行者立即保存原始日志/命令/退出码/身份；审核者记录它证明什么、不证明什么。
- 持续工作每 5 分钟做一次有界检查并落盘一次 checkpoint；若正在运行同步工具，在其返回后的首个安全边界补记。没有新证据则明确写“无新验收”，不要复制旧结果充数。无需后台自动续跑来模拟进度。
- 每个有界包审核后、暂停前或结束本轮前：写 reviews/ 的 PASS / CHANGES_REQUESTED / INCONCLUSIVE，列明未关闭问题、复现命令、清理及下一个具体实验。
- 文档与相关候选代码独立提交；Git 写入由一方持有，先通知 Devin 停止 Git 写入，检查暂存区，按明确路径提交，正常推送；不得覆盖或夹带无关工作。

## 证据格式

每项至少包括：时间/时区、候选 HEAD、文件 SHA256、设备/OS/rootfs build、目标 UUID/hash、执行命令和真实退出码、原始输出位置、结论与限制、清理结果、下一步。外部执行者报告标“Devin-reported runtime”，审核者亲自复核后才升级。source-confirmed 不能冒充 runtime-confirmed。RE 给出 IDA 输入路径/UUID/imagebase/偏移和反汇编。

地址、密码、个人主机名不进文档；用 MACWS_DEVICE、MACWS_DEVICE_PORT 等占位。运行日志提交前审查敏感内容。历史失败保留且标注已被何条证据推翻，不删掉来制造全绿记录。

## 总目标与验收

先恢复 22F82 的真实 WS input dispatch（正确 owner/window 的 WS-DISPATCH + 可见状态变化），保留完成帧/像素证据；然后处理已列明的 GPU-fence/pacing、wallpaper rootfs 重建、session/activation 回归。诊断包完成不等于整个移植完成，持续时长和 rc=0 也不是端点验收。
