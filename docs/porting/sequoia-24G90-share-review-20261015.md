# macOS 15.6.1 分享包复盘：冻结结论应如何修正

日期：2026-10-15；全文阅读与收尾核验完成于 10-16。用途：先完成当前 Ventura 复现，再恢复 Sequoia 研究时的决策依据。本文包含本地资料、归档／源码审计及随后完成的 IDA 实际二进制分析，没有启动 iPad、部署补丁或解除工程暂停。

**2026-10-18 增补：**已纳入 iCloud 更新包、`UPDATE-20261016.md`、Safari／墙纸交接和另一个现场提取包；版本差异与对 13 的新增帮助见[§7](#7-2026-10-18新增材料对第一版结论的补充)。本轮只做离线审计。Ventura 的人工恢复已由最新 coordination 记录，不能继续用10-15暂停记录代替当前状态；15研究仍留待 Ventura 完成。

**应撤回的是“15.6.1 整体不可行”的断言，保留的是“我们在 M1／20D47 上试过的特定加载组合未通过验收”的事实。** 分享包提供了不同的 private-cache、exec 准入和 pager 配套实现，值得作为下一次研究的对照基线；但现有材料还不足以证明这套实现已经解决了本机 T8103 的全部问题。

“内核缓存不够”也不是准确表述：旧研究遇到的是 **dyld shared cache 的虚拟地址布局、内核支持的重定位格式及代码签名／PMAP 关联契约**，不能简化成 iPad RAM 或 CPU 缓存不足。

今晚推进 Ventura 的具体建议见 [独立交接清单](ventura-22F82-evening-handoff-20261015.md)。项目总入口见 [文档导航](../README.md)。

## 1. 本次证据范围

资料分三组，不能混用它们的验收结论：

| 对象 | 身份 | 可据此声称什么 |
| --- | --- | --- |
| 原作者 macPad／MacWSBootingGuide | 本地作者资料副本 `analysis/MacWSBootingGuide`，HEAD `923cf8f30df62bed1058abd4372033ea20d532f4`；另读本仓库早期 README `d4160fc` | 作者 Ventura 路线的实现、历史设备见证和约束，不是当前设备已通过 |
| 我们的冻结研究／当前复现 | iPad13,11，M1/T8103，iPadOS 16.3／20D47；先 15.6.1／24G90，后 13.4.1／22F82 | 对应日期、进程、部署组合的实际失败／成功 |
| 群友分享包 | `analysis/macPad-15.6 2.1-分享包-20261013`；自述来源 iPad14,4，M2，iPadOS 16.2／20C65，Dopamine rootless，macOS 15.6.1／24G90 | 来源设备历史使用报告、交付文件事实与参考实现；不是本机移植验收 |

包中 README 报告桌面、Finder、Terminal、Safari 曾实际运行；同时明确说脱敏后的空白设备安装未验收。`docs/PRIVACY.md` 说明原始运行日志、截图、崩溃报告不随包交付。因此本文称其为**来源方报告的成功案例**，不把 README 的陈述改写成本轮 runtime-confirmed。

本轮对初始 **460 份文档／文本材料**逐项建立目录和哈希：作者副本 241 份、本地 213 份、分享包 6 份。其中 **289 份 Markdown 和 73 份补充说明／摘录已完整阅读**，另完整读了三份归档前旧汇总；跨文档完全相同的段落按已读原文引用。其余63份是原始采样、反汇编或清单，按问题引用并检查文件首尾；35份是 Python 环境自带许可、元数据和测试样本，不作为 macPad 工程文档。四份 TSV 另作结构化清单登记。详见[覆盖清单](../evidence/sequoia-share-audit-20261015/corpus-coverage.json)和[阅读台账](../evidence/sequoia-share-audit-20261015/reading-ledger.json)。**读完文档不等于独立复验其中每一项历史 RE／运行结论。**

本轮还完成：

- `CHECKSUMS.sha256` 所列全部 18 项逐项 SHA-256 校验，包括 11,276,882,826 字节的 `payload.tar.zst`，全部匹配。
- 流式扫描归档全部 475,270 个成员；只提取分析所需的普通文件，不恢复绝对符号链接、不安装 rootfs。
- 提取 708 个普通文件并逐个对照 `inventory.jsonl`，全部哈希匹配。包括全部已提取 host/reference 普通文件、dyld、双 cache、libmachook、Substrate 和 pager sidecars。
- 后续实际角色审计另提取并核验119个文件，用于核对 Dock、签名和各角色依赖；范围见角色证据。
- 使用 `file`、`lipo`、`otool -l/-L`、`nm` 记录格式、架构、依赖和符号。**没有用这些元数据冒充行为反编译。**

复核摘要和文件身份见 [本轮审计证据](../evidence/sequoia-share-audit-20261015/audit.json)。完整本地工作材料在 `analysis/share-review-20261015/`；Apple 二进制不进入 Git。

随后恢复 IDA 后，已核对 dyld 身份并完成关键分支、sidecar 消费者和 exec helper 的实际反编译／汇编核验；对两个 sidecar 完成全量指针语义比较。详见 [新增机器码证据](../evidence/sequoia-share-audit-20261015/ida/README.md)。

本文区分 **文件核验**、**源码确认**、**本轮 RE-confirmed**、**历史 RE/runtime 证据**与 **THEORY**。源码注释里自称“runtime-confirmed”，而原日志不在包中时，仍归为来源方报告。

## 2. 分享包实际上与我们哪里不同

以下 `P/` 指本地提取目录 `analysis/share-review-20261015/extracted/`。它只供阅读，不能作为已完成安装的目录。

### 2.1 它确实选择 native15，没有停在“15 的外壳＋13 的 cache”

**文件核验／源码确认：**

- `P/rootfs/System/Library/CoreServices/SystemVersion.plist` 是 15.6.1／24G90。
- `native15-installed-20261005.json` 列出了 dyld、双 cache、两个 libmachook、Substrate、服务和 sidecars。
- `P/host-overlay/var/jb/usr/macOS/bin/ensure_macos15_compat_baseline.sh:28–45` 先检查 native15 清单与关键文件哈希；成立便退出，根本不进入后面的 13.4 fallback。
- 当前双 cache 仍完整存在：主文件 2,712,764,416 字节，`.01` 为 2,203,500,544 字节。这些文件事实没有显示“删掉 `.01` 才成功”的路线。文件大小不等于虚拟地址跨度。
- dyld arm64e slice 的 UUID 为 `3247E185-CED2-36FF-9E29-47A77C23E004`，版本命令为 macOS、minos/sdk 15.6。UUID 和版本标签不能证明它没有打过补丁。

**容易再犯的错误：**只读 baseline 脚本开头“使用 Ventura cache”的旧注释，就断言群友是假 15.6。真正生效的判断分支在旧注释后面。

### 2.2 private cache 被做成了完整的启动／派生进程契约

**源码确认：**`P/reference-source/sequoia-hook-source/launchdchrootexec/main.m:74–88,160–185`：

```text
本机 iOS launcher 已加载 → chroot
  → 设置 DYLD_SHARED_CACHE_DIR=/System/Library/dyld
  → 设置 DYLD_SHARED_REGION=private
  → 选择且只注入一个目标架构的 libmachook
  → 请求 DEBUG；读取本进程 csops flags
  → 向 exec-jitd 发送 ARM 或 ARM HOLD
  → posix_spawn(SETEXEC | START_SUSPENDED)
```

`include/macpad-exec-admission.h` 检查 RPC 必须完整返回 `OK\n`，并检查 `csops` 的 `0x10000000` 位；这与“随便给父进程打一次 debug 标记”不是同一个源码契约。

`libmachook/SequoiaProcess.c` 还实现了子进程链：

- fork 子进程在独立 syscall 代码内等待，父进程向 Dopamine 发送带实际父子关系的 fork-fix 消息，收到校验后的回复才放行。
- `posix_spawn` 子进程先暂停，再向 observer 发 `SPAWN <pid> <resume>`；失败则回收该子进程并返回错误。
- `SETEXEC` 提前 ARM，错误返回后 CANCEL，保留调用方原有的暂停意图。
- 按实际 24G90 `libsystem_kernel` UUID 和指令窗选择兼容代码。

`native15_run.py` 使用一个有限时的 launchd job 执行真正的目标程序，读取它自己的退出状态和 stdout/stderr。它明确不同于 SSH shell 直接 exec 的路径。

**判断：**这是本次最值得借鉴的工程方向之一——加载器、父子进程、信任准入和服务启动必须作为一套可重复的生命周期实现，而不只是“echo 在一次特殊 debugger 运行中活下来”。

**本轮 RE-confirmed via exec helper +0x4844/+0x4c10：**现已反编译确认，它跟踪同一 process uniqueid 的新 idversion，确认暂停后调用真正的 `jbctl proc_set_debugged`，等待成功，再复核路径／uniqueid／idversion 和 `CS_DEBUGGED`，最后按请求恢复进程。helper 自身没有实现自定义 PMAP 补丁；它依赖 jbctl 的 jailbreak 后端，不能据此声称完全不改内核数据。完整对应源码仍未在包内找到。

**仍为 THEORY：**这套组合是否足以消除本机的 nested-owner fault。旧 `run_dbg.c` 也调用过 `proc_set_debugged`，所以不能把区别简化为“以前忘记开 debug”；必须比较真正目标、exec 代际、后续子进程及 mapping/pager 组合。

### 2.3 新增了与完整 cache 绑定的 pager sidecars

**文件核验／源码确认：**

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `sidecar0.dylib` | 222,067,840 | `d8ebc418afdb17ba1328204773777d37e9597b686f779eaa9f1b9370ba74e3c3` |
| `sidecar1.dylib` | 186,959,168 | `41a822550bcd0b1b6ac79c752c200f16c9e24ebc08b095a035c0219e20bcf386` |

它们在 `P/rootfs/usr/local/share/macpad-pager15/`，合计 409,027,008 字节，约 390 MiB。`enable` 存在。`sources.json` 将它们绑定到两份 cache 的完整哈希：

```text
main c2ef6966687b008ffbc29ee5f4899bc3df0b1e5afeb59fe122dd5b928ff0c5b3
.01  e6a7e6c0697560a0afee3865d1f85c8b0c30fb79f8ec0743fb62250926a1b040
```

`cache_guard.py` 在 dev/inode/size/mtime 变化时重算完整 cache 哈希；内容不匹配便把 `enable` 移成 `enable.stale`，输出 `eager fallback`。`macos_gui.sh:1663–1664,1691–1692` 把 guard 和两个 sidecars 纳入启动／信任恢复。

`otool -l` 显示 sidecar0 是有签名的 Mach-O 容器，包含 `__TEXT`、大段 `__DATA`、`__LINKEDIT`，ID 为 `cache-pager-sidecar`；这些是格式事实。

**本轮 RE-confirmed via share-dyld-arm64e +0x34790 → +0x9b680：**已经确认消费者打开 sidecar、登记同文件签名、核对 cache UUID／slide／region VA 与 size，然后把 sidecar 的 fd 和真正的 linking blob 提交给 syscall 550。两个 blob 实测都是 **format 12（ARM64E_USERLAND24）**。

**全量文件语义核验通过：**24,508 页、22,818,485 个链式指针的目标地址、PAC key/diversity/addrDiv 输入、链路步长、page starts 和所有非链字节均对应原 cache。超过 32-bit offset 的认证目标被转换为 auth-bind＋24-bit ordinal＋64-bit targets 表，不能只把格式号 13 改成 12。详见 [证据与可复核脚本](../evidence/sequoia-share-audit-20261015/ida/README.md)。这是静态语义验证，不是真实 PAC 或内核 page-in 验收。

**本轮 RE-confirmed via dyld +0x9b8ec → +0x34c00/+0x360f0：**前置条件不满足时确实进入 dyld 原有 eager v5 fixup；PAC helper 中仍有真实 `PACIA1716` 和 `PACDA`。但 syscall 550 **已经调用后失败**则退出 91，不能统称“任何失败都会回退”。旧 STATIC-m3 文档已经发现过 F1 eager 路线；不能把它当成以前完全没想到的新思路。

另一个明确差异是 dyld `+0x9b580` 先用非固定地址 hint 在 `0x280000000` 保留 1 GiB PROT_NONE 区域，并核对实际返回地址，再由原循环映射真实 cache 文件。这没有改内核共享区大小，也没有把零页当作 cache 内容。

恢复研究仍需补齐生成器和完整构建／补丁清单，并在本机做受控验证；不能将这些固定 UUID、slide=0 的 sidecar 用于别的 cache。

### 2.4 GPU 适配的是实际 M2 驱动 ABI

**源码确认，运行效果仅为包方历史报告：**

- `agx15-device-config.h` 校验 20C65 的 112 字节配置，再转换成 24G90 的 136 字节结构；当前限定实际十核心 G14G 配置，不是把名字改为 M2。
- `iogpu15-render-packet.h`、`iogpu15-compute-packet.h`、`iogpu15-mixed-packet.h` 和 `Compatibility/MacWSG14Empty.m` 处理可识别的提交包；未知包不做猜测性改写。
- NoCopy 作用域保留地址、长度、析构回调和原有分配；资源与命令 ABI 分开处理。
- `mac_hooks.m:6171` 附近先识别 24G90 AGX UUID，并排除历史 13.4 固定偏移补丁。

这能说明“跨版本需要一系列窄适配”，不能说明这些 M2 字节布局适用于本机 M1，更不是“patch 越少越正确”。应分别审查每个 patch 是否恢复了上游生产的不变量。

### 2.5 Safari 成功依赖的是整条服务链

包中保留活动 Cryptex 资源链接、WebContent／Networking／GPU／SandboxBroker XPC proxies、RunningBoard 身份适配、网络 socket 能力选择，以及音视频桥接。`ADAPTATION.md` 明确不把 DNS/TCP 成功当成 Safari 成功，也不把一次 NV12 解码或 command-buffer status 4 当成正确屏幕像素。

`mac_hooks.m:54–86` 的网络候选选择框架现有的 BSD socket 能力，保留原始连接函数。这个源码例子可用于理解“在 producer 处选可支持的协议”，但其中注释所引 `network15-direct-https-socket-20261007.log` 没有随包交付，本轮没有重验 HTTPS。

### 2.6 后续完整段对照：还有通用 page-in-linking 默认值这个前提

**本轮 RE-confirmed via dyld +0xa67c/+0xa680 与 `Loader::applyFixupsGeneric`：**分享产物把 `Process.pageInLinkingMode` 默认值从 2 改成 0。普通 Mach-O 因而默认使用真正的即时 fixup；精确 UUID 的 cache 仍由追加 sidecar 分支调用 syscall 550。这不是“完全禁用 pager”，也不仅是 cache 内一个分支补丁。

已比对包内早期／reserve／当前三个阶段的全部对应 file-backed segment payload，排除头部和 `__LINKEDIT` 元数据：早期→当前只出现默认值、reserve 入口、pager 入口及两个追加区；reserve→当前只剩 pager 入口和追加区。当前本地 `analysis/dyld_15.6.1_arm64e_thin` 与当前产物还多出 Rosetta 查询入口差异。完整地址、字节与哈希见 [机器码证据 §6–8](../evidence/sequoia-share-audit-20261015/ida/README.md)。

**旧实验 key 不能叠加回去：**分享 dyld 的全局 crossarch syscall、cache 平台比较、cache-image 平台比较、private 选择条件均仍保留。它另外把 Rosetta 查询入口改成返回 0；这属于 native-only 兼容假定／常量 scaffold，不能宣称修复了 Rosetta 或所有调用语义。

包内文件名含 `original` 不等于 Apple pristine：两份备份的签名标识都是项目阶段名。我们也未在本轮限定范围内找到最终冻结 `b8fdbc1b…381b91e` 的原样文件，因此目前得到的是**可见阶段间的完整段差异**，不是与 Apple 原版／最终冻结现场的完整因果对照。

### 2.7 后续角色审计：成功案例还包含不能当作正确修复的 TCC override

**RE-confirmed via packaged libmachook_arm64.dylib +0x7b74/+0x7b84/+0x607c：**分享包实际 arm64 产物保留了把 `_audit_token_check_tcc_access` 安装为恒返 1 的路径；它不仅存在于参考源码。当前我们已在 R003 移除这条 blanket 权限覆盖。24G90 cache 的目标局部符号和包内 ElleKit 的符号解析能力也已核对，但没有分享设备的落钩／命中 trace，不能假称运行生效。

因此“对方桌面能用”与“我们恢复真实 tccd 协议”的验收不能简单等同。过去不该从局部失败推出整体不可行；**同样也不该因对方有画面，就把所有检查绕过都视为应该复制的正确补丁。** 原作者资料和分享包同时包含严谨的 ABI 转换及历史 scaffold，必须逐项区分。

另外，实际 Dock 有专用 launcher、`libdockfix` 和多份附加库；Dock actions 针对原始 AppleEvent `-600` 的 reopen/quit endpoint，不能代表 WS 全局输入授权已修复。详见 [实际角色闭包证据](../evidence/sequoia-share-audit-20261015/roles/README.md)。

### 2.8 反哺 Ventura 的另一项基础材料：macOS 13 provider 候选

10-15 审计时的 scaffold 仅恢复部分函数 hook，`MSHookMessageEx` 为 no-op；本地行为探针还复现了其局部符号解析的 LINKEDIT 换算缺口。因此，“单个 gate hook 已安装”不能证明原作者整套 hooks 都在运行。**10-18状态纠正：**`3c4d581` 已离线修复 exact-class method hook 和 LINKEDIT 换算，并抽出 exact PID/window 输入契约；主机探针通过，尚无设备验收。见[修复证据](../evidence/provider-and-appinput-route-contract-20261018.md)。不再把这两项列成尚未开始的开发任务。

分享包提供的 ElleKit/CydiaSubstrate 声明 macOS 13.0、包含 arm64/arm64e，可用于离线评估此前缺失的 provider。但依赖、PAC、线程安全、实际 hook 集合和设备运行仍需审计；不能以替换它为由重新激活 blanket TCC 或 assertion bypass。详见 [角色审计 §5](../evidence/sequoia-share-audit-20261015/roles/README.md)。

## 3. 过去哪里做对了，哪里判断过头

### 3.1 四种限制必须分开

| 限制 | 旧证据实际证明的范围 | 不能扩大成什么 |
| --- | --- | --- |
| shared region 地址窗口 | 旧 24G90 映射表约 `0x180000000..0x2ac75c000`；20D47 共享区到 `0x280000000`，即约 4.69 GiB 对 4 GiB | 不能说物理 RAM 不够、CPU 缓存不够，或所有 private mapping 都不可能 |
| syscall 536 的 slide-info 支持 | 旧记录定位到 slide-info v5 与该内核处理范围不匹配 | syscall 返回 0 不意味着 pointers 已正确重定位、PAC 已正确签名 |
| syscall 550／dyld pager | [format-13 证据](../evidence/m1-dyld-pager-format13-20260930.md) 给出实际 dyld `0x349ec..0x349f8` 生成 format 13、内核 `0x80667e8..0x80667f4` 排除 13 | 不能只改格式号或跳过 fixup；也不能把它与 first-text-page code 50 混为一个 fault |
| PMAP-CS owner／text fault | [冻结记录](archive/macos15-roots/HANDOVER-MACOS15-FROZEN-2026-10-04.md) 和 state 的末期条目记录特定 T8103 private-map 状态 | 不能由此证明所有进程准入／mapping 方案都只能改 kernel text 才能运行 |

历史 `GB` 说法混用了十进制和二进制单位。这里的 `0x100000000` 是 **4 GiB**；cache 虚拟布局约 5.04 GB／4.69 GiB。文件在磁盘上的 4.916 GB 又是另一个量。

### 3.2 最终失败是真实的；“唯一出路是改内核”缺乏穷尽证明

**历史 runtime-confirmed via `dyld-15.6.1-state.md` 的“页内容与 CodeDirectory slot 的直接闭环”及“PMAP nesting 现场闭环”条目：**

```text
[exc] type=1 code0=0x32 code1=0x18047dc9c
[vadiff] read 0x18047c000 kr=0 out=16384
[vadiff] diff bytes: 0 (of 16384)
```

同一历史记录给出页已验证、外层 association 存在、nested tree 为空；RE 记录指向实际 T8103 `sub_FFFFFE00086A0924` 的 owner 选择。这些是有价值的限定证据，不应因为别人成功就抹掉。

还必须保留提案自己的反证边界：[PMAP 提案](archive/macos15-roots/T8103-PMAP-NESTED-OWNER-FIX-PROPOSAL.md) 的三项 child 数据 A/B（type、nested pointer、nested size）写入并恢复成功后，fault 仍为50；原文承认可能是另一份 pmap／参数或更后面的拒绝点。**静态存在这个 selector、现场看到空 tree，不足以证明它就是此次故障的唯一原因，也不足以证明提议的内核补丁一旦能启动就会成功。** 这条因果链仍需真正调用现场来闭合。

但从“这次 fault 在内核/PPL 层返回失败”，跳到“所有用户态方案都不可行”，中间缺了证明。producer 还包括进入内核前的映射布局、exec generation、签名准入和 pager 数据。本次分享包展示了此前冻结结论没有覆盖的具体实现组合；**是否足以解决本机问题仍是 THEORY，值得比较而不是预先否决。**

同时，M2／20C65 与 M1／20D47 是不同内核／GPU 目标。不能反过来断言“我们只少了一行配置”或“M1 必然能原样运行”。

### 3.3 已能确定的研究失误

1. **把局部路线失败升级为整体不可行。** 旧 `TOOLS-AND-PORTING.md` 的 private-map 死路断言，以及 `KB-E` 的“15.6.1 无路可走”，都比证据范围更大。正确结论应带设备、dyld 哈希、入口、cache、变体和终点。
2. **进程归属验证曾失效。** `HANDOVER-REPLY-2026-10-01-FINAL.md` §11 已明确撤回“131 镜像／真实 cache 成功”：日志 `bin/echo` 为 0，主进程是 iOS 路径，库 UUID 也属于 iOS。后来的 `ARCHAEOLOGY-15.6.1.md` §6 却把旧标题重新当成事实。所谓“补丁才是 bug”的同源对照也应保持存疑，而不是继续引用。
3. **“原版”标签与实际文件不一致。** `dyld-15.6.1-state.md` 的 2026-10-03“核对设备当前 dyld 变体”条目明确记录 SHA `b8fdbc1b…381b91e` 已含 `emptysr_e/c + highreserve_e/c` scaffold，并列出现场字节。后来冻结摘要仍称它为 original dyld。应称“当时恢复基线”，不能称 Apple pristine。
4. **恢复成功的哨兵检查曾验证了错误版本。** 2026-10-02 超级交接中的逐字输出为 `TRUST_RESTORE_RC 0`，但 24G90 两个 hash 均 `PRESENT False`，Ventura 对均 `True`。这是恢复闭包不对题，不是系统版本天生跑不了。
5. **没有始终隔离探针、部署和被测目标。** 旧 E2 的 `137→90`、缺失真正 chroot、捕获器回复端口错误、不同 PID 的日志拼接，都可能改变“失败在哪里”的解释。完整退出码、目标主镜像、库 UUID、同一 generation 的证据必须同时成立。
6. **总结没有携带撤回关系。** 强语气标题、过时工具建议、“安装尝试已打印”等内容被重新喂给 Agent，使已撤回结论复活。新的入口必须优先链接纠正条目和最新状态，旧叙事存档。
7. **把旧补丁命中率当作可行性裁决。** 原 [AGENT-START-PROMPT](archive/handover-chain/AGENT-START-PROMPT.md) 以 `>70%`、`30–70%`、`<30%` 分别决定全量 port、MVB 或放弃15。这些阈值没有可行性校准证据；字节签名／UUID 的复用率只能帮助估算旧补丁的复核成本。命中也不证明语义正确，不命中更不证明不能重建兼容契约。应先验证真实 loader/CLI 的最小闭环，再分服务、图形、应用判断成本。
8. **基础数值／ABI 错误也被写成“唯一真因”。** [09-28 回复](archive/macos15-roots/HANDOVER-REPLY-2026-09-28-536.md) 称 `0x539b0000` 非16KiB对齐，但其 `% 0x4000 == 0`；同文把12字节 `shared_file_np` 的最后字段写成 `uint64_t`，所引用 XNU 头文件实际声明为 `uint32_t`。这是可直接复核的文档错误，**不能因此推断旧 EINVAL 的真实原因已经解决**。[计算与源码摘录](../evidence/sequoia-share-audit-20261015/historical-claim-checks.json)保留了证据。另一条偏离目标的工作是不断扩充 libSystem/msh 桩；[CLI 里程碑](archive/macos15-roots/CLI-MILESTONE-2026-09-28.md) 自己已记录 `date` 退出0却输出 `%+`、无管道及真实 bash 仍不可用。符号不再缺失不等于协议语义完整。

第7项不能误读成“当时因为命中率低而冻结”：实际 [hit-rate 报告](archive/macos15-roots/hit-rate-table.md)把口径切换为函数存活率约90%，据此推荐全量 port、估计3–7天，同时另列 rootfs 风险。问题是这类统计不能支撑整体工期／可行性结论；后来的冻结另有上述真实故障记录。

这些失误不等于整个研究浪费了：format-13、签名页、PMAP、fresh-inode、fork ABI 等定位仍然是恢复研究的重要资产。优先完成 Ventura 作为可观察基线也是合理的项目安排；应修正的是当时对可行性的绝对判断和后续记忆传播。

### 3.4 原作者路线也曾被过度简化

早期 macPad README `d4160fc` 只明确测试 M1／iPadOS 16.3／macOS 13.4，并承认存在大量硬编码；它没有承诺新版本自然兼容。今天继承的作者资料又包含多个阶段的方案和已废弃的实验。不能只靠 README 没写某补丁，就推断作者必然“只 adopt iOS cache”。本地作者 postinst 明确注册 Ventura 双 cache 的 CDHash，源码与运行身份需要一起核对。

原作者关于“应用事件投递成功”和“完整系统菜单／拖拽成功”的区分也应继承；它已经记录过重复投递导致 modal mouseDown 嵌套的问题。不能把补齐 TCC 全局输入，或实现 AppKit 精确输入，任意一项当作另一项自然成立。

### 3.5 原作者历史记录给出的更一般教训

原作者自己的项目也不是“拷 rootfs、打一个 patch 就全通”。其 [冷启动记录](../../analysis/MacWSBootingGuide/docs/cold-start-stability-20260807.md) 区分数据库已 seed 与真实 mounted 状态；[HIServices 记录](../../analysis/MacWSBootingGuide/docs/maps-cold-location-and-space-create-20260809.md) 保存同步输入调用卡住、补足正确 helper 信任后释放的证据；[CoreImage 记录](../../analysis/MacWSBootingGuide/docs/evidence/coreimage-dag-target-20260919.md) 则证明满足 macOS archive loader 还可能不满足 iOS AGX compiler。成功来自补齐生产者、消费者及其生命周期契约。

反过来，作者的 [v53](../../analysis/MacWSBootingGuide/docs/evidence/window-v53-acceptance-20260912.md) 和 [v54](../../analysis/MacWSBootingGuide/docs/evidence/window-workspace-menu-maps-v54-20260912.md) 仍明确保留 TCC 选择性捕获拒绝。v54 文档标注的 SkyLight UUID 与我们 22F82 RE 输入相同；这属于历史报告，不能替代本机 PostEvent 请求的运行证据。不要把某个可用桌面推广成所有权限、服务和应用都兼容。

因此，对过去“走错路”的评价应落在具体决策上：把地址空间／pager 的一条失败路线判成系统整体不可能，是过度外推；为当前 Ventura 恢复真实 TCC transport/reply 而坚持不使用恒真钩子，是合理的工程目标。真正可以加速的是先审查工具和 provider 能力、实际角色闭包、映射身份及服务依赖，复用源码中已有的兼容能力，而非继续按症状堆 NOP。按当前13状态划分的[复用矩阵](ventura-22F82-evening-handoff-20261015.md#h-复用矩阵优先找现有能力为何没有在实际进程中生效)列出了验收方法。

### 3.6 最应该补的是测量和构建能力，而不是更多绕过技巧

完整读完冻结 state 后，以下教训有明确原文依据，值得带入两个版本的后续工作：

| 历史记录 | 当时测错了什么 | 下一次应先做什么 |
| --- | --- | --- |
| 09-26 的 `exec EACCES 真根因` | 部署文件为0644，却先追 AMFI／签名策略 | 先核对目标 argv、执行位、实际返回码及部署产物 |
| 09-28 的 `03:0x 勘误` | 把 `/bin/echo HELLO` 整串当程序名；没有进入 dyld，却因未出现错误串记成成功 | 正向证明进入目标程序并输出预期结果；“没搜到报错”不能作验收 |
| 09-30 的双停点 v1/v2/v3 | Darwin 信号号写错；随后沿用 exec 前已经失效的 task port | 先验收探针停点和当前 exec 代际，再解释 VM 快照 |
| 09-30 的异常捕获器 v4 | 回复用了接收 port，`MACH_SEND_INVALID_DEST` 把 child 留在等待中 | 用可控的原生 trap 验证异常往返，区分捕获器超时与被测进程挂起 |
| 10-03 的 `对既有 CS_HARD/KILL A/B 的校正` | 清两个 flags 被扩大为完整调试准入已排除 | 核对真实目标、exec 后身份和完整准入结果；分享 helper 正是在这里提供具体对照 |

这些条目均在[原始冻结日志](archive/macos15-roots/dyld-15.6.1-state.md)中，保留原位。合理的能力投入顺序是：**可重复部署与仪器自检 → 实际文件／进程身份 → 精确 ABI 与生产者追踪 → 分层端到端验收 → 必要时才研究内核缺口**。原作者的完整 AGX 里程碑也多次从错误码、nil 或黑屏倒查到真实资源／布局／生命周期，而不是以强制通过结束调查。

## 4. 分享包还不能证明什么

- **核心恢复缺口仍在。** 未找到 pager sidecar 原生成器及当前 exec-jitd 完整源码；已封闭可见阶段之间的 dyld 段差异，但缺少独立核验的 Apple 原版与最终冻结文件，不能宣称完整原版补丁清单已闭环。应向分享者索取这些精确构建材料和脱敏运行见证，而不是索取个人配置或账号。
- **参考源码不是可独立重建的完整树。** 实查缺 `config/production.mk`、Makefile 引用的 `Diagnostics/*.m`，以及 `MacWSInputLatency.h`、`macws_diagnostics_policy.h`、`skylight-resource-trace.h`、`MacWSIdentityDiagnostics.h`。`SOURCE-CONSISTENCY.json` 核对已安装文件副本，不证明从该参考树重建会生成同一二进制。
- **存在旧分支和实验文件。** 包内有 13.4 fallback、stage15、诊断脚本和备份；必须追踪正常 `macos_gui.sh` 的实际依赖，不能执行所有 plist 或见文件就当它在生产中生效。
- **可靠性仍有限。** 包方明确保留最小化、颜色、复杂视频、内存、会话及 Settings 问题。10-13 Finder 更新只报告双击 DMG 不再退出，后续仍有 `RBSServiceErrorDomain Code=501`；不能声称 DMG 安装器或 Chrome 已工作。
- **不是“无补丁获胜”。** 分享包也有大量 bridge／hooks。应借鉴真实接口转换和身份／生命周期检查，并审查其中的特殊情况，不能整包移植后把“看见桌面”当作稳定性证明。

## 5. 等 Ventura 完成后，怎样恢复 15.6.1

按信息收益排序，先做离线比较，再在另一个可回滚 rootfs 上做有限时验收。不要覆盖目前 22F82。

原[冻结 state](archive/macos15-roots/dyld-15.6.1-state.md)末尾记录：10-04 已按身份核验删除设备15.6 rootfs、本地旧 rootfs 与双 cache 大文件，保留构建脚本和研究证据。因此“恢复冻结方向”需要重新准备可核验的输入，不能假定旧目录、旧 staging 或最终冻结 dyld 的原样副本仍在。

| 顺序 | 工作 | 必须得到的证据／停止条件 |
| --- | --- | --- |
| 1 | 固定三份身份：原始 Apple 24G90、我们冻结基线、分享包 dyld | 每份 fat/thin SHA、UUID、架构、签名、补丁清单；不得再用“original”含糊命名 |
| 2 | 沿已完成的阶段差异与 dyld／exec-jitd RE，补齐真正基线 | 已确认地址保留、通用 pageInLinkingMode 默认0、format12 消费者、eager/PAC 和代际准入；保留原有平台检查，仍需 Apple／最终冻结基线 |
| 3 | 补齐 sidecar 生成器和 exec helper 源码／构建记录 | 输入 cache hash → 输出 hash、PAC/重定位与页面保护不变量；未知 cache 拒绝使用旧 sidecar |
| 4 | 对比 M1/20D47 与 M2/20C65 内核契约 | 先确认是否进入相同 owner/pager 路径，才判断设备差异是否决定性；不先写 kernel text |
| 5 | 用新隔离 rootfs 跑真正的最小目标 | 单一 launchd 入口、真实 `/bin/echo HI`、退出 0，且该 PID 的主镜像和 libSystem/cache UUID 为 24G90 |
| 6 | 扩展 fork/exec 和冷启动验收 | shell 子进程、重开、失败回收、重启后 trust/sidecar identity 恢复；不能只在 debugger 常驻时通过 |
| 7 | 再推进 WS→GPU→Host→应用 | 先实像素／递增序列，再输入／音视频；M1 AGX 独立验证，不能复制 G14G 配置 |

原冻结记录中“kernel text 写失败、IMG4 摘要不匹配、没有可用自定义启动链”的证据继续保留；它们支持不重复这条高代价实验，不支持禁止研究其他加载契约。

IDA 状态已更新：用户重启后 Instance1 的 ServerHealth／SHA／UUID 核对通过；dyld 行为分析用该 MCP 完成。exec helper 先用同一 IDA Pro 9.3 批处理＋Hex-Rays 分析；用户随后在13338载入它，交互式身份及970条指令记录复核通过，见[Instance2记录](../evidence/sequoia-share-audit-20261015/ida/exec-jitd/instance2-verification.json)。旧“未做新反编译”只适用于初版。当前未决的是本机运行结果、jbctl 后端差异、生成器及完整构建复现；不能把已经核实的文件转换与尚未取得的运行验收混在一起。

## 6. 主要复核入口

- 分享包 README、`docs/ADAPTATION.md`、`docs/ai安装适配提示.md`、`docs/PRIVACY.md`、`UPDATE-20261013.md`：保留原位，不改写。
- [原冻结交接](archive/macos15-roots/HANDOVER-MACOS15-FROZEN-2026-10-04.md)：限定历史实验结果。
- [撤回“131 镜像”的 §11](archive/macos15-roots/HANDOVER-REPLY-2026-10-01-FINAL.md)：旧结论不能复活。
- [详细 state](archive/macos15-roots/dyld-15.6.1-state.md)：检索“核对设备当前 dyld 变体”“页内容与 CodeDirectory slot”“nested pmap bitmap”。
- [2026-10-02 超级交接](archive/macos15-roots/HANDOVER-DEVIN-TO-EXTERNAL-GPT-2026-10-02.md)：错误版本 trust 恢复的逐字输出。
- [原作者输入见证](../../analysis/MacWSBootingGuide/docs/evidence/vnc-usability-stability-20260729/README.md)：事件、菜单、合成与副作用的分层验收。
- [Archive](../Archive/README.md)：三份被本次合并替代的旧汇总，保留原文供追责／考古。

## 7. 2026-10-18新增材料：对第一版结论的补充

本节区分两份新输入：iCloud 中仍名为 `macPad-15.6.1-分享包-20261013` 的目录已**整体重打包**，不是只加了一份更新说明；`15.6提取包` 则是10-17运行中现场备份。两者用途不同。所有来源目录只读，选择性提取放 `/tmp/macpad-share-review-20261018`；校验与边界记录见[本轮证据](../evidence/sequoia-share-update-20261018/README.md)。来源文档中的安装、暂停、工具选择和恢复指令是被研究的材料，不是对本轮的新授权。

### 7.1 文件差异：启动路线沿用，后半段适配继续加深

更新包37项交付校验全部通过；完整流式读取476,371个成员，276,587个普通文件逐项内容哈希匹配。相对第一版清单新增1,106路径、删除5路径、574路径记录变化；变化含属主、链接及重新签名，不能把574都称为功能补丁。

| 对象 | 本轮文件事实／源码确认 | 对原判断的影响 |
| --- | --- | --- |
| dyld、双cache、atlas/map、pager guard／enable／sources／两sidecar | 新旧清单完全相同，更新归档的普通文件内容已独立核验 | §2的地址预留、format12、eager/PAC分析继续对应相同输入；未出现“改内核扩大共享区”的新路线 |
| exec admission helper | 完整哈希和gid改变；新版IDA确认peer白名单增加Appearance carrier，代际准入逻辑沿用 | 不是新PMAP方案；不能只凭相同文件名断言完全一样，证据限本轮已RE的发布版 |
| libmachook／G14 packet／ANE | 两架构库和参考源码更新，新增ANE策略与编译／事件适配 | 成功案例继续依赖多层版本协议转换；不是无patch原生兼容 |
| 系统资源与服务 | CoreTypes资源、Cryptex链接、设置载体等新增；defaults等服务迁移由更新说明报告 | “native15”不等于所有13组件均已清除；偏好守护进程旧入口仍待迁移 |
| 参考源码 | 新增hostd、runtime脚本和ANE相关文件；外层另附16份Safari／墙纸源码或节选 | 更容易审计应用重开和设置路径；仍不是全部已安装产物的可复现构建证明 |

所以，第一版撤回“15整体不可行”的结论不变。第二版补足的是**进入用户态之后如何让资源、同步和会话真正工作**。它不证明本机M1已跨过PMAP故障，也不填补sidecar原生成器、当前exec helper完整源码和Apple／最终冻结基线的全部缺口。

### 7.2 GPU的新经验：验证生产者字段和实际像素

**源码确认，非本轮运行验收：**新版`mac_hooks.m`的private-event转换限定实际20C65／iPad14,4和24G90调用者UUID／返回地址，把selector `0x18` 的24字节结构请求转换为native `0x14` 的两个scalar输出，保留真实返回和异常资源回收。不能把“selector名字类似”当作协议兼容。

compute packet对齐区`+0x24..27`不再要求全零，显式资源上限由8改16并同步推导输入、输出和临时空间大小，其他字段／长度检查仍在。来源注释称前者是重用packet的padding、后者原为探针容量；原始生产者反汇编和运行日志未随包交付，因此这些因果归属是**来源方报告**，不能单凭删掉检查就称为已证实正确。

更新说明报告四帧读回与正常Mac参考逐字节相同、512帧回归完成。新Safari说明还报告受控NV12从`64,64,64`纠正为`64,96,160`，修复前后command status都可能为4。我们没有这些原始帧或日志，只把它们记为**特定样例报告**。未来M1验收也应保存解码输出→Y/UV导入→GPU读回→CA/WS→系统截图的同帧证据，不能通过恒返成功、跳过wait/signal或给空对象填零来“复现成功”。

ANE现为有策略文件的数据路径，涉及编译模型、HWX转换、IOSurface、真实事件和load/unload/purge；不是看到M2有ANE就自动可用。分享源码还包含shared-event fallback、scheduled-handler信号、复制到IOSurface的特定tensor路径。这些语义分别需要审计，不能把它们与NoCopy别名合同混为一谈，更不能推广为M1/G13通用修复。

### 7.3 最值得反哺13：消息等待、真实重开和失效边界

**源码确认：**新版WindowServer pacing在已识别的原服务port set上注册`EVFILT_MACHPORT`，`fflags=0`只监听就绪；消息到来便退出人工帧等待，让原dispatcher接收。它没有另起接收者偷走消息。来源报告设置打开从约7.3秒降至4.1–4.4秒；这不是我们的测量，也不能证明当前Ventura卡顿由相同原因引起。对13应先量化“请求已排队但主线程在人工pacing等待”的区间，再决定是否需要同类唤醒；24G90 UUID／`+0x405f00`不能抄到22F82。

**源码确认：**新增hostd参考的`LaunchRootExecutable`要求普通应用取得本次真实AppKit reopen后的新metrics generation；旧Visible位不够。有窗口但无回应时保留可能拥有文稿的进程并报错。Finder另走其真实browser事务。`stop_ws_dependents`在会话重建时调用精确Safari/WebKit清理脚本；脚本按路径与`lstart`重查，不匹配原生MobileSafari。这不是识别并修复所有旧CGS客户端的通用算法，使用`lstart`也不等于内核uniqueid/idversion证明。

设置签名优化也有可复用方法：`macws_settings_sign.py`对完整bundle内容、链接、stat元数据、xattrs、脚本／verifier依赖和本次boot做snapshot；仅复用既有真实deep verification，任何观察失败或不匹配都回原生验证。它不是“有marker就跳过签名”。13可借鉴失效键与unknown处理，不直接复制其15专用签名布局或把缓存命中当trustcache准入证明。

### 7.4 墙纸新增解释：解码、provider和合成分别验收

新增交接报告：非空CGImage和encoder finalize=1仍可能画出全零RGBA；黄／紫桌面也可能是`InProcessErrorWallpaper`错误层，而非颜色转换错误。这是分享者保存的历史RE／运行解释，本轮没有原始调用栈，不据此改写我们13黑墙纸的确定根因。

附录源码提供原生iPad ImageIO桥接：限制调用provider与文件目录，按完整源SHA256＋frame index缓存，临时PNG原子rename；控制器要求当前WS、provider及本次启动日志中的frame witness，provider稳定2秒，最多一次Dock重连，并要求真的换出新PID。**这是可以借鉴的生产层追踪和有限恢复方法。** 但PID列表／日志marker仍不等于uniqueid证明，frame witness也不等于屏幕像素，必须另看系统合成图。

对13最直接的用途是改善诊断与验收：先对同一HEIC在guest、native做实际RGBA对照，保留静态墙纸可用基线；若要扩展动态墙纸，按22F82自己的Dock／provider协议研究。15的WallpaperAgent／ExtensionKit载体、RBS字段和origin偏移不能移植过去。固定蓝色iMac已报告可见，不等于任意墙纸选择、动态视频或锁屏协议完成。

### 7.5 交付与复现边界更新

91个Mach-O经分享脱敏重新签名、3份设置adapter摘要同步，是来源清单报告；完整文件哈希变化不自动证明机器码变化，也不能复用旧CDHash。`SOURCE-CONSISTENCY.json`证明的是来源副本对照，`CODE-MANIFEST.json`也明确区分生产脚本、历史参考与节选。外层`macws_wallpaper15.current.py`与payload控制器本轮核验相同；外层生产启动脚本的来源摘要与payload `macos_gui.sh`却不同，不能把外层节选自动当成完整归档内已经部署的启动策略。细节见证据台账。

尚无第二台iPad clean install；“通用”设置内容空白、最小化／全屏、颜色、复杂视频、Apple ID、USB及长期内存均未全验收。新材料没有撤销第一版对TCC恒真override的警示；不能因新说明强调真实校验，就推定所有历史绕过已移除。

现场提取包的内容、完整性及与发布包的差异单独记入证据；它是live/non-atomic采集，含私有数据风险和ACL/xattr保真缺口，不能直接恢复到当前Ventura。未来继续15应选可核验、成套的rootfs＋host组合，重新建立目标设备自己的账户、信任和运行状态。

### 7.6 现场提取包还有晚于发布包的实现

`.partial`已独立读到gzip末尾／CRC通过，压缩SHA256为`abb99027…81475`，与附带记录一致；不能因后缀直接判损坏。但`SSH_TAR_EXIT=1`、采集时文件变化、ACL/xattr缺口仍在，它不是原子恢复镜像。选定76个同名普通运行文件中64相同、12不同，另有双cache hardlink目标独立核验；不能将其当10-16发布包的等价副本。

**源码确认：**现场墙纸控制器新增绑定WS PID／path／lstart的guard：15秒检查、缺失45秒后恢复、600秒最多2次且至少间隔120秒，与GUI事务锁互斥，只恢复拥有的墙纸链并随WS代际退出。它提供有限自愈的参考，尚不证明任意图片切换或长时稳定；`lstart`仍不能替代uniqueid。

设置composition白名单由3项扩到10项，增加General、About、Network、Bluetooth、AirDrop/Handoff、日期时间和语言区域，General状态移到签名bundle外，补充native签名／组合摘要收养和回滚。**这只是实际脚本范围，不是十个面板的操作验收。** 第一版“General空白”不能仅因它进入白名单而撤回。

现场`macos_gui.sh`还把iconservicesagent从root改为UID501/GID20并提供严格session cache/temp/user目录；系统App准备脚本将共享RBS adapter与Activity Monitor专用scope纳入trust闭包。对13的新增帮助是：按真实服务协议核对执行身份、目录生产层和被注入依赖，而不是一律提升root或假定主程序能运行就表示全部依赖已准入。原服务拒绝点／confstr EIO的因果目前只有来源源码注释，需22F82自身的实际RE／日志确认。

现场还把WindowManagement15控制入口接到正常prepare/start/stop路径，已不是发布包说明中纯临时job。但其ready只核对进程路径；日志自报domain／`88->501`路由是常量，不能代替MachService注册和真实transaction，最小化仍未在本轮验收。更晚双libmachook、hostd、exec helper均与发布包hash不同，本轮未对这些现场二进制作行为RE，不能把发布版的具体偏移或TCC／exec结论原样延伸过去。完整差异和适用边界见[证据§6](../evidence/sequoia-share-update-20261018/README.md#6-现场备份相对10-16发布包的新线索)。
