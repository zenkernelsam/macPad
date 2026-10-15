# 今晚继续 Ventura：分享包提供的线索与当前验证顺序

日期：2026-10-15。目标仍是 **iPad13,11／20D47 上的 macOS 13.4.1／22F82 复现**。本文是可交给隔壁 Agent 的建议包，没有向任何 Agent 派发任务，没有启动设备。15.6.1 继续冻结，待 Ventura 完成后另行恢复。

当前工程状态以 [coordination/PAUSE_REQUEST.md](../../coordination/PAUSE_REQUEST.md)、[REVIEWER_STATE.md](../../coordination/REVIEWER_STATE.md) 和后续交接为准；它们与 `_RELAY SYNC` 类文件保持原位。不能将本文视为用户已经返回或同意重启。

## 1. 接续的真实位置

源码基线：`529c412` 为 R008 candidate；`6d1b7f6` 记录 10-15 07:38 的人工要求停机。R008 尚未形成完整设备验收，reviewer 另有未处理的边界失败。

| 已有证据 | 能证明什么 | 仍未证明什么 |
| --- | --- | --- |
| 10-13 最终合成输出、`state=ready`、连续 sequence | 桌面显示链已有运行见证 | 系统输入可用、所有 UI 交互正确 |
| R002 的真实 MSHook provider 探针与随后 gate telemetry | 诊断入口确实能打到执行路径 | 早期 `INSTALL` 日志可以追认成真实 hook |
| R005 实际 TCC callsite 返回 `w0=1` | WS 的 fresh-miss 路径调用了 checker，随后拒绝 | 是 daemon 明确拒绝授权 |
| R006 `_xpc_error_connection_invalid` 原始记录 | 当时缺少真正的 system-tccd transport，默认 1 未被改写 | 一启动 tccd 就能输入 |
| R007 真正 dictionary 回复、`get_bool("result")=0` | transport 已前进到真实回复对象 | 存在一个类型正确的 `result=false` 字段 |
| R008 的源码、fixture 和构建 | 候选恢复机制在部分本地用例通过 | 冷启动、recovery、完整回复语义和最终输入设备验收 |

详见 [R006](../evidence/r006-tcc-producer-20261015.md)、[R007（含纠正）](../evidence/r007-system-tccd-20261015.md)、[R008](../evidence/r008-tccd-transport-repro-20261015.md)。不要从旧版“事件子系统没初始化”的摘要重新开始。

## 2. 分享包真正给了哪些帮助

### A. `-virtualonly` 不是单独成立的终局解释

**文件核验：**分享包的 `host-overlay/var/jb/usr/macOS/LaunchDaemons/com.apple.WindowServer.plist` 也使用 `-virtualonly -daemon`。结合其来源方报告的桌面使用经验，这足以要求我们重新检验“这个 flag 必然使所有交互不可能”的说法。

但不是同版对照：分享包为 24G90／M2／20C65；当前是 22F82／M1／20D47。包中没有它的 `SLSPostEventRecord=0` 原始见证，不能据此断言同一全局入口已经通过。不要把去掉 `-virtualonly`、抢占原生显示作为第一步。

### B. 分开验证应用内输入和全局系统输入

**源码确认：**分享包 `reference-source/sequoia-hook-source/libmachook/AppInputBridge.m` 的实现沿用两条路径：

1. 精确 PID/window 的输入在目标 AppKit 进程主线程投递 NSEvent，保留 first responder、原始窗口身份和事件序列。
2. Dock／系统表面使用实际 CGS-connected owner 的系统指针路径；`MacWSPostDockSystemInput` → `MacWSPostLegacySystemPointerEvent` → `CGPostMouseEvent`。菜单 modal 状态、完整 down/drag/up 和全局命中测试不能被随意替换成直接调用 tile action。

这些路径在当前源码里也已经存在；**不要再造一个“全新输入框架”**。应先用实际同一触摸／键盘动作建立身份和终点对照：

| 场景 | 观察链 | 验收 |
| --- | --- | --- |
| Terminal 内容区点击、文本／箭头 | Host source/seq → broker → exact PID/window → AppKit dispatch | 实际光标／文本变化，并有新完成帧 |
| Dock 图标、菜单栏、上下文菜单 | 唯一系统指针 owner → WS/TCC → 实际目标事件 | 菜单或应用真的出现；不存在重复 mouseDown |
| 标题栏 drag、菜单 tracking | 持续同一接触序列与 capture owner | 可见移动、release/cancel 正确，无 modal 嵌套 |

这个对照既能尽早恢复部分可见交互，又不会把局部 AppKit 成功宣称为全局输入修复。若旧证据已对同一身份／同一配置完整跑过其中一项，直接沿用其失败定位，不重复轮询 API 名单。

### C. 不是“多加一个 PostEvent entitlement”就解决

**文件核验：**当前 `entitlements.plist`、作者副本、分享包 host-overlay 中，相关列表相同：

```text
com.apple.private.tcc.allow:
  kTCCServiceListenEvent
  kTCCServiceSystemPolicyAllFiles
com.apple.private.tcc.manager.check-by-audit-token:
  kTCCServiceAccessibility
  kTCCServiceListenEvent
  kTCCServicePostEvent
  kTCCServiceScreenCapture
```

`check-by-audit-token` 中出现 PostEvent，不能从名称推断被检查的 sender 自动被授予 PostEvent。参考源码的总 key 数虽不同，但上述相关列表没有差异；安装后二进制的实际 entitlement 还必须另查。

今晚最有信息量的是在 **WS 的真实 sender/audit 上下文**内捕捉完整 request/reply、字段是否存在及类型、responsible process 和实际 peer identity。独立 `macwstccdprobe preflight` 可以验证传输和打印自身身份，但它不是 WS 原始请求的等价替身。

### D. 生命周期与身份比继续加“通过检查”的 patch 更重要

分享包把 launcher、每次新 exec、fork、helper、private cache 和清单联成一套契约。Ventura 不需要因此导入它的 private-cache loader，但应复用这种验证方法：

- 实际加载域、job label、MachService、PID generation、架构、哈希和动态信任必须一致。
- 恢复路径必须重新发布已停止的依赖，而不是延长 timeout。
- readiness 要证明正确 daemon 对正确请求给出可识别回复，不能只搜索输出中的服务名字。
- 一个成功 build／测试／进程存活都不能替代本机终点验收。

### E. 适用于后续应用，不阻塞今晚输入

Cryptex 资源路径、WebKit 多实例 XPC proxies、RunningBoard 身份、Network socket 能力、NoCopy 所有权、真实 GPU 完成与像素分层、退出后按路径＋启动时间清理进程，都是有价值的后续线索。

先记入待办，不把 24G90/G14 的命令数据、136 字节配置、pager sidecars、gigacage 或 Safari 专项补丁复制到 22F82/G13。完整版分析见 [15.6.1 复盘](sequoia-24G90-share-review-20261015.md)。

### F. 新增 IDA 结果：借鉴协议转换，别照搬 15 的补丁

本轮已经确认：分享 dyld 在内核共享区之外先保留地址，再映射真实 cache；将 sidecar 的 format12 数据交给真实 syscall 550；前置失败时执行真正的 eager v5/PAC；exec helper 按 uniqueid/idversion 协调 `jbctl proc_set_debugged`。22,818,485 个指针的静态目标与 PAC 输入语义对照通过，详见 [机器码证据](../evidence/sequoia-share-audit-20261015/ida/README.md)。

对 22F82 的启发是：先确认服务／协议版本、真实生产者和正确进程代际，再做保持语义的转换。这些发现**没有改变 R008/TCC 的未完成状态**，也不构成给 Ventura 添加 sidecar、额外 debug 标记或绕过权限检查的理由。

## 3. 今晚建议的工作顺序

### P0：先处理已保存的 R008 reviewer 边界问题

读取 [R008-review-edge-repro.json](../../coordination/artifacts/R008-review-edge-repro.json) 及对应 `.py`。这是**已有本地 fixture 结果，不是设备运行失败**，也不是本轮重新执行的测试。

| 已保存用例 | 实际结果 | 应守住的不变量 |
| --- | --- | --- |
| `endpoint_token_only_in_environment` | 被判 ready | 只认 job 的真实 service table，不能把 Environment 中的相同文本当注册证据 |
| `observation_error_still_unloads_job` | 查询失败后仍 unload/remove/load | 观察失败是 unknown，不是明确不存在；不能据此破坏已有 job |
| `missing_mandatory_path` | exit 0 | 必需文件缺失必须失败 |
| `mandatory_target_empty_codes` | exit 0 | 必需目标没有可用 CodeDirectory 不能算已信任 |
| `trust_query_nonzero_with_partial_stdout` | exit 0 | 查询失败的部分 stdout 不能证明完整 membership |
| `mandatory_signature_trust_failure_swallowed` | exit 0 | 必需签名／trust 失败不能被吞掉 |

沿用实际源码函数与 fixture 修复并复核，不用把已通过的 55 个既有测试当成否定这六项的理由。包方的 socket-only readiness 也不能替代这套要求。

### P1：完成受控部署与真正回复语义

用户明确返回后，按现有项目 pipeline 验证 source/build/deployed hash、macOS platform、arm64／arm64e 两个 thin libmachook、fresh inode、trust 和 native iOS 健康。全流程前先读取热状态。

然后在一次新的 WS generation 上，用最小 `postmin` 请求复现，保存：

- sender 的 PID、pidversion/audit token、调用进程、实际 daemon PID/domain；避免把缓存 deny 当新 checker 调用。
- WS 侧真正 TCC request 和 decoded reply。分别记录 key 缺失、类型不对、显式 false、显式 true，不能只打印 accessor 返回值。
- 对同一请求关联 checker 原始返回、gate 结果、MIG 返回及目标 AppKit dispatch。
- 若仍拒绝，在实际 tccd 内追踪身份／策略的生产层；不修改 TCC.db、不编造回复、不 hook checker 恒真。

需要日志时，记住诊断标记在 chroot 的 `/var/mnt/rootfs/tmp/macws_runtime_diagnostics`；有些日志开关会在首次调用时缓存。旧 WS 启动后才补标记不一定使日志生效。记录标记及 generation，结束后清理自己创建的那一项。

### P2：把协议进展接到人能用的界面

以 Terminal、Dock、菜单三个小场景验收，记录真实输入效果、新帧序列、crash 和热状态。若精确 AppKit 路径通过而系统输入仍 1002，分别报告它们，不用“桌面可用了”掩盖系统菜单／拖拽缺口。

最后做一次正常停止和再次启动／既有 recovery 路径，确认 tccd 依赖重建、没有残留 debugger/探针、原生 tccd/backboardd 正常。具体恢复动作由当前交接 owner 串行管理。

## 4. 可直接交给隔壁 Agent 的提示

> 继续当前 22F82 Ventura 复现，不恢复或部署 15.6.1。先读 docs/README.md、当前 coordination/PAUSE_REQUEST.md 和 REVIEWER_STATE.md；本文不解除暂停，只有用户明确返回后才能做设备操作。源码 candidate 是 529c412，停机记录是 6d1b7f6。先处理 coordination/artifacts/R008-review-edge-repro.py/json 中的六项未通过边界，不以已有 55 项通过替代它们。随后按项目 pipeline 做部署身份和有限时 cold/recovery 验证，在真正 WS sender/audit 上下文捕捉 TCC 完整 request/reply；get_bool(result)=0 不等于显式 false。分享包同样使用 -virtualonly，其相关 TCC entitlement 列表与我们相同，不能直接据此诊断缺 flag 或缺权限。复用现有 exact PID/window AppInput 与唯一系统指针 owner，分别验收 Terminal 内容输入、Dock/菜单、drag/cancel；不能把局部 AppKit 成功算全局输入已修复。不要复制 24G90/M2 的 dyld、sidecar、G14 ABI 或检查绕过。每个结论附同一 generation 的原始证据，最后确认新帧、实际界面变化、原生 iOS 健康及清理。详细研究见 docs/porting/sequoia-24G90-share-review-20261015.md。
