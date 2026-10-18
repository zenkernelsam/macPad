# 今晚继续 Ventura：分享包提供的线索与当前验证顺序

日期：2026-10-15；资料核对完成于10-16。目标仍是 **iPad13,11／20D47 上的 macOS 13.4.1／22F82 复现**。本文是可交给隔壁 Agent 的建议包，没有向任何 Agent 派发任务，没有启动设备。15.6.1 继续冻结，待 Ventura 完成后另行恢复。

**2026-10-18更新：**人工已恢复Ventura工作，见最新[relay SYNC](../../coordination/_relay/SYNC.md)与[REVIEWER_STATE](../../coordination/REVIEWER_STATE.md)；10-15暂停记录是历史状态。本文是离线研究交付，不控制当前worker或设备。新增分享包经验见[§5](#5-10-18补充更新包怎样帮助当前13)，接续前先读本段；15仍待13完成后研究。所有coordination／relay文件保持原位，本轮未改写或向Agent发消息。

## 1. 接续的真实位置

10-15历史基线：`529c412` 为 R008 candidate；`6d1b7f6` 记录当日人工要求停机。下表保留该时点的证据边界，不能替代10-18的工作进度。`3c4d581` 已离线推进provider和AppInput契约；R008最新状态以其证据与当前owner为准。

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

### 先看新增的关键差异：分享包仍有 TCC 恒真 override

**实际二进制 RE 已确认：**分享包的 WindowServer 用 arm64 `libmachook` 会尝试把 `_audit_token_check_tcc_access` hook 到 `MOV W0,#1; RET`。24G90 SkyLight 的局部符号表里有该函数，包内 ElleKit provider 也支持这种符号查找。我们在 R003 中明确移除了这条 blanket override。详见 [角色闭包与 TCC 证据](../evidence/sequoia-share-audit-20261015/roles/README.md)。

尚无分享设备的落钩／调用原始 trace，所以不能说已 runtime-confirmed 生效；但“相关 entitlement 一样”已经不足以证明两边的权限策略一样。**不要恢复此 override 来使 1002 消失。** 当前修复真正的 tccd transport/reply 仍然有必要，不能把更严格的验收工作误认为走错方向。

分享包还有独立 Dock launcher、`libdockfix` 及多份附加 action 库。其中 Dock actions 是在原始 AppleEvent 返回 `-600` 后使用真实目标进程的现有 Reopen/Quit 协议，与 WS 全局事件投递不同。只看通用参考源码会漏掉这些实际角色差异。

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

### C2. Provider缺口已离线修复，仍需实际加载验收

10-15审计及本地探针确认过method hook no-op和LINKEDIT换算缺口。**`3c4d581`已修复这两项**：exact-class replacement保留original IMP，符号按LINKEDIT vmaddr/fileoff换算；新增exact PID/window契约。五项focused主机测试通过并完成两架构离线编译，但provider仅编译对象，未完成设备打包／加载验收；完整708项套件有14 failures、5 errors、14 skips，不能称全绿。见[10-18证据](../evidence/provider-and-appinput-route-contract-20261018.md)。`MSHookFunction`许可表、PAC、实际加载及原调用仍需按目标核对，不能由这次离线成功推定设备输入已经通过。

分享包提供了一份声明 macOS 13.0 的 arm64/arm64e ElleKit/CydiaSubstrate，可以作为**离线兼容审计候选**；不是可以直接覆盖安装的成品。替换 provider 可能激活旧的未审查 hook，必须先排除 TCC 恒真等 override。R008 的真实回复排查继续推进；在再添加生产 hooks 之前，另立 provider 能力、符号解析和类方法 hook 的有限时验证工单。见 [本地行为证据](../evidence/sequoia-share-audit-20261015/roles/README.md)。

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

后续完整段对照另确认，15.6.1 分享 dyld 的通用 `pageInLinkingMode` 默认0，但专用 sidecar pager 仍运行；旧 `DYLD_PAGEIN_LINKING` 环境变量有实际门控，不能只看 env 宣称路径已切换。其全局 crossarch syscall 和平台检查分支也仍保留，不能把旧实验 NOP 清单批量叠加。

对 22F82 的启发是：先确认服务／协议版本、真实生产者和正确进程代际，再做保持语义的转换。这些发现**没有改变 R008/TCC 的未完成状态**，也不构成给 Ventura 添加 sidecar、额外 debug 标记或绕过权限检查的理由。

### G. 原作者文档补充：桌面可用不等于所有 TCC 服务通过

原作者 [v53 验收记录](../../analysis/MacWSBootingGuide/docs/evidence/window-v53-acceptance-20260912.md) 保存了选择性捕获的 `granted:false` / `request_returned:false`，且给隔离 helper 添加 ScreenCapture entitlement 后仍拒绝。[v54 后续](../../analysis/MacWSBootingGuide/docs/evidence/window-workspace-menu-maps-v54-20260912.md) 标注 SkyLight UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`，与我们当前 RE 输入一致，并记录 `TCCAccessPreflight` 的实际策略返回仍阻止捕获。这是**原作者保存的历史证据**，本轮没有重新运行其设备。

它的用途是纠正验收尺度：不能从作者可操作桌面推导出完整 TCC 已恢复，也不能把 ScreenCapture 的历史失败直接归因到我们今天的 PostEvent。两者应分别记录 service、sender、真实请求和返回路径。分享包的 WS checker 恒真 override 与作者这些捕获拒绝也不矛盾：它们并非同一条检查路径。

### H. 复用矩阵：优先找现有能力为何没有在实际进程中生效

下表的“已有”仅指当前源码存在，不替代 22F82 上的加载和行为验收。作者记录是其历史平台的证据；不同 build 的私有偏移需要重新匹配。

| 经验／来源 | 当前 13 的处理 | 有限时验收与禁止误判 |
| --- | --- | --- |
| 实际 provider 能力，见 [角色审计](../evidence/sequoia-share-audit-20261015/roles/README.md) | **已有离线修复`3c4d581`**：局部符号、实例／类方法和original契约已有主机对照；待设备闭环 | 先核对source/build/deployed身份，再验证replacement执行、original可调用；不以`INSTALL`日志过关，不重复实现或直接换入整份ElleKit |
| [冷启动 HIServices 缺信任](../../analysis/MacWSBootingGuide/docs/maps-cold-location-and-space-create-20260809.md) | **复用已有工具**：[启动脚本](../../layout/usr/macOS/bin/macos_gui.sh)、[trust helper](../../layout/usr/macOS/bin/macws_boot_trust.py)；与 R008 service readiness 同步检查 | shell/WS 能启动不足以证明同步调用的 XPC helper 可执行；检查真实依赖 CDHash、job domain、请求与回复，避免把阻塞归为触摸坐标错误 |
| [LaunchServices 记录失去 mounted 状态](../../analysis/MacWSBootingGuide/docs/cold-start-stability-20260807.md) | **保留后续恢复检查**：数据库存在、seed 完成与可解析 App 路径分别验证 | 真正应用 URL／ASN／窗口证据；不要反复 `kill/seed` 或只延长等待。作者早期和后续 Space/Dock 顺序有演进，当前 build 的运行记录优先 |
| [Carbon 菜单嵌套循环](../../analysis/MacWSBootingGuide/docs/evidence/input-unification-20260731/host-native/multilayer-carbon-runtime.md) | **已有输入实现**：[AppInputBridge](../../libmachook/AppInputBridge.m)；先定位它实际使用的 route | 正常主线程异步队列不保证在菜单 tracking 中被处理；验收 submenu、Escape、完整 down/up/cancel，不能以 socket send 成功代替 |
| [原生进程内 OpenPanel](../../analysis/MacWSBootingGuide/docs/ipados-host-panels-finder-fullscreen-20260802.md) | **已有能力**：[LocalFilePanel](../../libmachook/LocalFilePanel.m) 选择 AppKit 的 `NSUseRemoteSavePanel` 路径 | 真实原生 panel、delegate、URL、取消；无需重造 UIKit 文件浏览器或伪造 modal 结果 |
| [NoCopy 所有权](../../analysis/MacWSBootingGuide/docs/evidence/native-nocopy-contract-20260919.md) | **已有转换**：`mac_hooks.m` / `Metal_hooks.x`；15/G14 参数不能照搬 | 原 CPU 地址别名、GPU 回写、真实 deallocator 恰好一次、两架构；复制到新 buffer 和提前回调不是等价实现 |
| [Office／CoreImage 编译目标](../../analysis/MacWSBootingGuide/docs/evidence/coreimage-dag-target-20260919.md) | **已有模块，后续按实际 compiler UUID 检查** | archive 能加载与原生 AGX 能建 pipeline 是两个门槛；已缓存错误产物需可恢复、停写后的版本迁移，不强行跳过校验 |
| [V8 地址预留](../../analysis/MacWSBootingGuide/docs/evidence/vscode-json-language-server-20260920.md) | **已有能力**：[PageAllocator](../../libmachook/Compatibility/MacWSElectronPageAllocator.c) | `mmap != MAP_FAILED` 不证明地址满足 cage；检查真实返回、对齐和完整 LSP 响应。与 15 dyld reserve 的方法相通，具体地址不能混用 |
| [缺失 sendfile](../../analysis/MacWSBootingGuide/docs/evidence/vscode-quit-sendfile-20260919.md) | **已有实现**：[MacWSSendfile](../../libmachook/Compatibility/MacWSSendfile.c) | errno、已传字节、原调用方 fallback、正常 Quit；一律 ENOSYS 或返回0不等价 |
| [只读 tab metrics 实际触发创建](../../analysis/MacWSBootingGuide/docs/evidence/7dtd-m2-fullscreen-shift-audio-20260929.md) | **已有修正**：AppInputBridge 使用 `_windowStackController` | 它是避免观测引入同步 IconServices 的例子；应用卡顿先查真实阻塞栈，不先禁用 Shift 或清空 tab 对象 |
| [packed metrics 与 ObjC 编码](../../analysis/MacWSBootingGuide/docs/evidence/window-native-popup-and-export-cancel-20260912.md) | **复用 ABI 检查方法**，与分享包 role/profile 审计一致 | `sizeof`、wire stride、所有权、canary 均要匹配；不能 NOP `objc_release` 掩盖 56→64 字节越界 |
| 15 的 reserve／format12 sidecar／exec 准入、G14 和 Safari 配套 | **留给 15 恢复，今晚不移植** | exact cache UUID、全部 segment/sidecar、PAC 语义、正确 exec generation 和应用终点缺一不可；M2 报告不构成 M1 接受 |

## 3. 今晚建议的工作顺序

以下保留10-15排查阶梯；10-18已由worker继续推进，先核对最新owner记录与部署身份，已完成项直接使用其证据，不按此旧清单重复修复／部署。

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

按最新coordination的授权与owner串行管理，用现有项目pipeline验证source/build/deployed hash、macOS platform、arm64／arm64e两个thin libmachook、fresh inode、trust和native iOS健康。全流程前先读取热状态；本轮文档审计不操作设备。

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

以下为10-18更新后的接续提示；按最新coordination认领与当前证据执行，不重复派工。

> 继续22F82 Ventura，先读docs/README.md、coordination/_relay/SYNC.md、REVIEWER_STATE.md及当前owner交接。人工已于10-18恢复，旧暂停文档仅作历史；15仍不部署。核对R008六项reviewer边界是否已由当前worker处理，按最新源码和证据接续，不从旧candidate覆盖回来。在实际WS sender/audit上下文记录完整TCC request/reply及字段类型；accessor零不等于显式false。分享包新arm64 libmachook仍有TCC恒真hook静态安装路径，不能复制。复用exact PID/window AppInput和唯一系统指针owner，分别验收Terminal内容、Dock/菜单及drag/cancel，用同generation的新帧和可见结果闭环。

> provider的method hook／LINKEDIT和exact输入契约已在3c4d581离线修复，先读docs/evidence/provider-and-appinput-route-contract-20261018.md；未做设备验收，不要重复开发或假定已部署。更新分享包新增经验在本文§5：查人工帧等待是否阻塞真实服务消息；应用复用要求本次reopen回应和新generation，窗口旧metrics不足；会话恢复只清理精确归属客户端，保留无回应但有文稿的进程；签名缓存变更或观察失败返回真实验证。GPU按生产者ABI和实际读回像素验证，HEIC按真实RGBA／provider／系统截图分层。24G90/G14/ANE/RBS偏移均不可抄到22F82，当前TCC/输入闭环优先，其他优化按实际瓶颈排期。

## 5. 10-18补充：更新包怎样帮助当前13

新增材料及文件核验见[15复盘§7](sequoia-24G90-share-review-20261015.md#7-2026-10-18新增材料对第一版结论的补充)和[本轮证据](../evidence/sequoia-share-update-20261018/README.md)。以下是工作方法和候选方向，**不是已证明本机故障原因，也不是要求今晚全部实现**。

| 优先级／触发条件 | 新经验 | 22F82可采取的下一步与验收 |
| --- | --- | --- |
| P0，当前provider／输入闭环 | 源码有hook不等于实际进程用了正确provider | 对`3c4d581`做实际装载身份、replacement／original探针与exact输入终点验证；TCC仍按真实回复处理，不能恒返通过 |
| P0，WS恢复后应用假成功／无响应 | hostd新增普通AppKit真实reopen＋fresh metrics；旧Visible可跨WS存活 | 给本次请求、应用PID／generation、endpoint和新metrics建立关联；有窗口无回应就报错保留进程，不能按名字批杀或“复用旧窗口=成功” |
| P1，服务慢且主线程在人工pacing等待 | 新源码只读监听原server port set，提前让出帧等待 | 先保存服务请求→排队→主线程wait→原dispatch→回复时序，证明有该瓶颈再适配22F82；验收延迟、idle CPU、frame sequence，不另起receiver消费消息 |
| P1，cold/recovery依赖或签名异常 | 完整内容／metadata／xattr／boot绑定的真实验证缓存，unknown回原验证 | 沿现有trust helper修正失效键和错误语义；对cache hit、同boot替换、重启、查询错误分别检查，不能拿socket存在或marker替代mandatory准入 |
| P1，IconServices或应用helper拒绝／缓存URL异常 | 现场脚本为iconservicesagent恢复UID501/GID20及专用cache/temp/user目录；注入RBS adapter单独恢复trust | 先在22F82记录真实daemon uid、HOME／confstr结果、目录owner和被注入库CDHash；不把一律UID0／扩大权限当修复，也不盲改当前正确身份 |
| P2，AGX／视频色错或后续帧失败 | 私有event request/reply、padding与资源容量逐项适配；NV12 status4仍可错 | 在实际G13二进制定位生产者，记录有语义字段和长度；测试同帧Y/UV、GPU读回及系统输出，不复制G14 selector／packet／ANE策略 |
| P2，墙纸／设置黑屏或颜色异常 | HEIC惰性对象可能非空但无像素，黄／紫也可能是provider错误层 | guest/native同文件真实RGBA A/B→原13 provider／Dock→WS最终合成→系统截图；保留当前静态图基线，不照搬15 WallpaperAgent链 |
| P2，已定位为拥有的可选组件偶发失联 | 现场墙纸guard绑定WS代际、与GUI事务互斥、有宽限／冷却／次数预算 | 借鉴有界恢复合同；更先修根因，停止时撤销guard，不能常驻轮询／重启掩盖输入或GPU故障；源码presence不算恢复成功 |
| P2，网络／扩展服务问题 | 原函数解析可能递归到replacement；guest/native origin与实例激活语义重要 | 先查真实owner image、trampoline、PID domain／audit及原始错误；TLS、RBS和bundle containment仍由真实实现决定，不改成singleton或放宽全部权限 |

新资料最能加速的是：**避免在已经坏掉的观测／生命周期基础上继续补末端症状。** 它没有证明我们的真实tccd修复是走错路，也没有给出当前1002的替代根因。更详细的Safari／墙纸源码保留在分享目录，只作离线参考；其中节选不是可独立构建单元，来源方原始日志／截图未交付。
