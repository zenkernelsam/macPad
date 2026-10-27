# R004 只读审计：XPC interpose/handler 与空字典回复的可能来源

日期：2026-10-18。范围：独立审计当前源码中 Ventura `TCC.framework` 与 chroot
`tccd` 之间的 XPC 处理，判断是否存在源码机制会构造/发送空字典、替换回调、
丢弃 dispatch 或抑制真实 handler。**只读**：未改源码/测试/构建输入，未构建、
未部署、未操作设备、未执行 Git 写入。设备观察引用后端 R008，不在本轮复测。

候选 HEAD：`7e5d771`（工作树含后端并行 WIP，本轮未读取为审计输入）。

## 1. 检查身份

| 文件 | SHA-256 |
| --- | --- |
| `libmachook/mac_hooks.m` | `e8ae9dadfa5bb40701a681661cb7677e729f94b35807355f5ea5ae0af71eb08d` |
| `libmachook/Metal_hooks.x` | `a49c52c14319b7991977c2ff1a5e39c353ef2f3a6824e82bec9f8b7335291e4a` |
| `macwstccdprobe/main.c` | `54fe253c2810704eb6286124e9878f2bf5cf63d51844e35aad9ce218ba8d03cd` |
| `analysis/TCC_22F82`（客户端 TCC.framework，本地只读） | `09b841378285a120a04b1c8718dd5c7e3b98b4e59849eea0d8767f845d02f2de` |
| `docs/evidence/r008-tccd-transport-repro-20261015.md` | `2a1754d0c7fb914c24cdec28a2eecfcd37b012a7dec3903cfcf549deda678147` |
| `docs/evidence/r007-system-tccd-20261015.md` | `bcbbe210919156854c51a9d8a26cc7b3f834517d6c56e212453e4a748a7ccfee` |

`analysis/TCC_22F82` 经 `file`/`nm` 确认是 arm64e **动态库客户端**
（导出 `_TCCAccessPreflightWithAuditToken`、`_TCCAccessRequest`），不是 tccd
服务端。工作树中未找到本地 `tccd` 服务端二进制。

## 2. 观察事实（引用，未复测）

**R008 设备证据（probe 上下文）：** 连接名
`com.apple.macosbooter.tccd.system`；请求 7 键（`function=TCCAccessRequest`,
`service`, `preflight=true`, `target_token` 32B, `TCCD_MSG_ID`, 
`require_purpose=null`, `background_session=false`）；回复是真实的
`OS_xpc_dictionary`，`count=0`，无 `result`/`preflight_unknown`/`restricted`/
`do_not_cache`/error 键；SPI 返回 1。见
`docs/evidence/r008-tccd-transport-repro-20261015.md:310-320` 与
`coordination/artifacts/R008-device-acceptance-20261018.json`
`tcc_reply_capture`。

**FACT：请求由真实客户端框架构造。** `macwstccdprobe` 的 `preflight` 调用
`TCCAccessPreflightWithAuditToken`（`macwstccdprobe/main.c:134-158`），不是
probe 手写 XPC 字典。因此 7 键 schema 来自 Ventura TCC.framework；probe 只
提供调用者 audit token 与 service 名。

**FACT：调用者身份是 probe，不是 WindowServer。** R008 明确记录
`caller=pid=43148 uid=0`，并注明 WS-sender 的
`MACWS_LSXPC_TRACE` 捕获被 `macos_gui.sh` 的 production preflight
诊断环境拒绝。因此该回复只证明 probe-context 行为。

**FACT（历史运行证据）：** R007 在客户端 `tccd_send_message` 内观察到真实
dictionary 回复；客户端解析路径读取 `preflight_unknown`、`restricted`、
`result`、`do_not_cache`，但当时未证明键存在。见
`docs/evidence/r007-system-tccd-20261015.md:126-146`。

## 3. XPC/hook 面清单

| 机制 | 位置 | 行为 | 进程/环境门 | 能否产生空回复 |
| --- | --- | --- | --- | --- |
| `bootstrap_look_up*` 名称映射 | `mac_hooks.m:16363-16431` | 仅把 `com.apple.tccd.system` 映射为私有名，其余透传 | 静态 interpose，所有注入进程 | 否；只改端点名 |
| `bootstrap_check_in*` 名称映射 | `mac_hooks.m:16432-16460` | 对称映射，供守护进程 check-in | 同上 | 否；只改注册名 |
| 早期 `xpc_connection_create_mach_service` | `mac_hooks.m:16478-16560` | 特定服务名改写；仅当名字被映射时清 `0x2` privileged 位 | 所有注入进程 | 否；原调用返回真实连接 |
| 早期 `xpc_connection_create` / listener | `mac_hooks.m:16572-16715` | 特定 bundle 名转私有 Mach 服务；其余透传 | 同上 | 否 |
| `xpc_main` 适配 | `mac_hooks.m:16847-16891` | 仅 ViewBridgeAuxiliary/extensionkitservice/hiservices/csnameddatad/authd/QuickLookSatellite；否则调用原 `xpc_main` | 程序名 + `XPC_SERVICE_NAME` + root | **tccd 不在列表**；handler 原样 |
| `xpc_connection_send_message_with_reply_sync` trace | `mac_hooks.m:16919-16977` | 先调用原函数（16948-16949），只在 `MACWS_LSXPC_TRACE` 下 dump 请求/回复，返回原 reply（16973） | env only | 否；不替换 reply |
| libxpc 运行时 `xpc_connection_create_mach_service` hook | `Metal_hooks.x:24722-24761` | 对**所有**调用清 privileged 位（24723）；名字经 `macws_private_chroot_service_name`（24743）；原调用（24760） | `InitMetalHooks` 全量路径 | 否；仅连接创建 |
| libxpc 运行时 `xpc_connection_create` hook | `Metal_hooks.x:24764-24821` | 特定 bundle 注册/转私有 Mach；其余透传 | 同上 | 否 |
| 上述 hook 安装 | `Metal_hooks.x:24824-24841`，调用点 24972/24990/25134 | 只 hook 两个 create 函数；注释明确“不扩展到 libxpc 协议/对象函数” | 全量 init 进程（tccd 无 `MACWS_UTILITY_PROCESS`） | 否 |
| `mach_msg` | `mac_hooks.m:13069-13345` | 诊断/CoreServices MIG 10054/Crashpad 特例；其余调用原 `mach_msg`（13208） | 特例按消息字节匹配 | 否；不构造 XPC 字典 |
| `mach_msg_overwrite` | `mac_hooks.m:13347-13409` | trace 后调用原函数 | env | 否 |
| `mach_msg2_internal` | `mac_hooks.m:13411-13480` | trace 后调用原函数（13434） | env | 否 |
| `dispatch_mig_server` | `mac_hooks.m:13514-13525` | 仅 locationd + `MACWS_LOCATIOND_MIG_TRACE` 时观察；始终调用原 dispatch（13520） | locationd + env | 否 |
| `audit_token_to_asid` | `mac_hooks.m:13565-13571` | 非零真实 ASID 保留，否则合成 `0x004d5753` | 无条件 interpose（15293） | 不构造回复；可影响身份语义 |
| `audit_token_to_auid` | `mac_hooks.m:13573-13575` | **写入** `val[0]=501` 并返回 501 | 无条件 interpose（15294） | 不构造回复；可影响身份语义 |
| `auditon`/`getaudit_addr` | `mac_hooks.m:13611-13660` | 合成 audit info | 无条件 interpose（15295-15296） | 否 |
| `SecRequirementCreateWithString` | `mac_hooks.m:14416-14514` | 全局 hook，但行为改写仅在 `CoreLocationAgent`（14437） | 程序名 | 否；tccd 不受影响 |
| PKD 签名 entitlement 适配 | `mac_hooks.m:15987-15990` | 仅 `pkd` 安装；`SecCodeCopySigningInformation` 在适配器内（15829） | `pkd` | 否；非 tccd |

**FACT：libmachook 中不存在 `xpc_dictionary_create_reply` 调用**（grep 计数 0），
也不存在对 `xpc_connection_set_event_handler` 的 hook/interpose（10 处均为
libmachook 自有连接的普通安装调用）。因此没有源码层“通用 server handler”
可替 tccd 生成或抑制回复。

**FACT：12 处 `xpc_dictionary_create(NULL,NULL,0)` 均为项目私有协议请求**
（Steam 信号量、Catalyst Keychain、hostd 控制、DNS、Metal 编译/blob、
AppInput、AGX conn borrow、`__xpc_bootstrap_services` 注册），不对应 TCC
连接或 TCC 回复。

## 4. tccd 角色与门控

- Job：`layout/usr/macOS/LaunchDaemons/com.macwsguide.tccd-system.plist:25-52`
  只发布 `com.apple.macosbooter.tccd.system`，以 `launchdchrootexec` 运行
  Ventura `TCC.framework/Support/tccd system`，**无 EnvironmentVariables**。
- 因此 tccd 不满足 `InitMetalHooks` 的 shell/utility/CFPreferences 早退条件
  （`Metal_hooks.x:24972/24990/24994-25029`），会安装两个 libxpc 创建 hook
  （25134）。这两个 hook 只改名字/privileged 位并调用原函数。
- tccd 不在 `macws_xpc_main` 的六个适配角色中（`mac_hooks.m:16851-16874`），
  故其 `xpc_main` 与事件 handler 是原版；没有任何源码替换其 server 回调。
- 名称映射是**必要**的传输修复：R006 未映射时是
  `_xpc_error_connection_invalid`；R008 映射后是真实 dictionary。映射本身
  解释“能否到达服务”，不解释“回复为什么为空”。

## 5. 结论：未发现源码层空回复机制

**FACT（源码范围）：** 本仓库注入到 tccd 的代码没有：
1. 构造/发送 TCC 回复字典；
2. 替换 `xpc_connection_set_event_handler` 或 NSXPC listener handler；
3. 拦截 `xpc_dictionary_create_reply`/dictionary get/set；
4. 丢弃 XPC dispatch（`mach_msg2`/`dispatch_mig_server` 均调用原函数）。

空字典因此必须由 **tccd 服务端自身**（Apple Ventura 二进制）或其客户端
授权/作用域分支产生；当前源码只能影响端点名、privileged 查找位和通用
audit-token 视图。以下均为 **THEORY**，未在运行 tccd 内验证。

## 6. 候选机制与判别实验（THEORY）

**C1（最高先验）tccd server 分支提前返回空 reply。**
Reply 对象是真实 dictionary（R008），最可能由 tccd 的 handler 创建后未写入
键就发送。判别：在运行中的 tccd 内对 `xpc_dictionary_create_reply` /
`xpc_connection_send_message` 设精确断点，按 probe 的 `TCCD_MSG_ID`/消息 id
关联本次请求，记录创建时与发送前的键集合，以及 handler 的提前返回点。
若 reply 创建时已有键而客户端仍见空，则转向传输/序列化假设。

**C2 客户端授权/作用域差异。** probe 是独立 root 进程，不具备 WindowServer
的 TCC 客户端 entitlement/身份；system-scope tccd 可能对非预期客户端返回
空字典。判别：在允许的 bounded 诊断下捕获一次 **WS-context** preflight
回复；或用一次性签名的 probe 副本携带与 WS 相同的项目 profile 做对照。
若两者都空，排除 C2。

**C3 audit-token 适配改变 tccd 看到的身份。** `audit_token_to_auid` 强制
501、`audit_token_to_asid` 合成共享 ASID（无条件 interpose），tccd 若在
入站授权路径调用它们，会看到非原生身份。判别：给 tccd job 临时设置
`MACWS_AUDIT_TRACE=1`（设备侧、由 backend 执行），在一次 probe preflight
期间采集 `MACWS-AUDIT audit_token_to_*` 调用与输入；再做一次不含该
interpose 的可丢弃 A/B 构建。若最小修复成立，应**按角色收窄**这两个
interpose（tccd 入站路径保留真实 token 字段），而不是伪造回复或绕过 checker。

**C4 请求选项/purpose 差异。** probe 的 SPI 调用传 `NULL` options；真实 WS
调用可能带 purpose/target identity。判别：捕获 WS 请求键（需 bounded 诊断），
或用 `TCCAccessRequestIndirectWithOptions` 复现相同 options。修复应是复现
真实客户端选项，不是填充回复。

**C5 名称/privileged 映射。** 已由 R006/R008 反证为直接原因：未映射是
connection-invalid，映射后是真实 dictionary；该机制是传输前提，不决定
reply schema。

## 7. 与分享包角色适配的比较

- 参考源码
  `analysis/share-review-20261015/extracted/reference-source/sequoia-hook-source/libmachook/mac_hooks.m:5689`
  仍安装 `_audit_token_check_tcc_access → hooked_return_1`；此前二进制审计
  确认打包产物保留该路径。
- 该参考源码中未发现 `com.apple.tccd` 名称映射，也未发现
  `xpc_dictionary_create_reply` 或 TCC XPC server 适配（grep 无匹配）。
- 因此分享包没有可复用的“空回复修复”；它通过恒真 checker 绕过问题。
  本项目明确禁止复制该 override，不能把它当作 upstream 修正。

## 8. 限制与下一步

- 没有设备运行；R008 的请求/回复事实为引用，不是本轮复测。
- 没有本地 tccd 服务端二进制；只能审计客户端与注入层。
- WS-context 捕获仍不可用；所有 schema 证据是 probe-context。
- 静态审计不能证明某 hook 在运行时一定未执行；本报告对“源码中不存在该
  机制”作事实陈述，对运行时因果作 THEORY 标注。
- **下一步单一判别实验（需 backend 设备所有权）：** 在运行 tccd 内按
  `TCCD_MSG_ID` 关联一次 probe preflight，断点记录 reply 创建/发送键集合
  与 handler 提前返回点；同时采集 `MACWS_AUDIT_TRACE` 的入站 token 调用。
  该实验能同时区分 C1 与 C3，并给 C2 提供上下文。

## 9. 命令与退出码

| 命令 | 退出 | 结果 |
| --- | --- | ---: |
| `sha256sum libmachook/mac_hooks.m libmachook/Metal_hooks.x macwstccdprobe/main.c analysis/TCC_22F82 …` | 0 | 哈希见 §1 |
| `file analysis/TCC_22F82`; `nm -a … | grep …`; `strings -a … | grep …` | 0 | arm64e 客户端库；导出/键名见 §2 |
| `grep -rn "xpc_dictionary_create_reply" libmachook/... | wc -l` | 0 | 0 |
| `grep -rn "xpc_connection_set_event_handler" libmachook/... | wc -l` | 0 | 10（均为直接调用，无 hook） |
| `grep -rn "xpc_dictionary_create(NULL" libmachook/... | wc -l` | 0 | 12（私有协议请求） |
| `git status --short | wc -l` | 0 | 审计时为 68 条既有后端并行 WIP；本轮仅新增本报告与 result 两个未跟踪文件 |

未运行构建、测试、packaging 或任何设备命令；未执行 Git commit/reset/stash/push。

(handover to codex now)
