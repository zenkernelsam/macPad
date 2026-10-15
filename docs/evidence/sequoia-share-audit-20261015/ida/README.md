# 2026-10-15：分享包 dyld、pager 与 exec helper 的实际二进制核验

这次补充解决初版复盘中“只有文件／源码线索、尚无新行为 RE”的限制。
全部为本地静态分析和文件语义核验，未运行 iPad 上的 macOS、未修改原始二进制。

## 输入与工具身份

- Instance1 `server_health`: `status=ok`、`auto_analysis_ready=true`、`hexrays_ready=true`。
  输入 `analysis/share-review-20261015/share-dyld-arm64e`，imagebase `0`。
  SHA-256 `def1d5e2ccde2ee7930a30f38d540981aab2ccda9a7f0d6212bd2f9339dd3a00`，
  UUID `3247E185-CED2-36FF-9E29-47A77C23E004`，与首次提取吻合。
- Instance2／13338 当时未监听。为继续独立工作，使用同一安装的 **IDA Pro 9.3 idat＋Hex-Rays 批处理**分析 exec helper，未更换分析引擎，也未执行 helper。
  输入是已核验的 `main15-exec-jitd-dock-reopen-20261010`；SHA-256
  `17d5f21a109b3df408640857dc06d8bbd63d85b41b3601f92f8297a52855d09e`，imagebase `0x100000000`。
- 在 IDB 中补定义追加代码区的指令和返回 stub，属于分析元数据，不修改输入文件。

证据：[ServerHealth](server-health.json)、[survey](survey.json)、[UUID](identity.json)、[helper 身份](exec-jitd/identity.json)。所有 dyld 地址都是此文件 imagebase=0 的地址；不能当作运行地址直接使用。

同日随后用户开启 Instance2／13338：ServerHealth 与 Hex-Rays 正常，输入文件、加载时／磁盘 SHA 和 UUID 都与上述 helper 匹配。对原批处理导出的五个函数共 **970 条指令记录**逐段比较 IDB 字节，零差异。见 [Instance2 核验](exec-jitd/instance2-verification.json)；无需因切换到交互 MCP 重做已完成的分析。

## 1. 4-GiB 限制：先建立真实地址保留，再做 private 映射

**RE-confirmed via share-dyld-arm64e +0x344ec → +0x9b580。**

`+0x9b58c` 清 `SharedCacheOptions+0xd` 的 page-in-linking 选择位；随后调用原有 `deallocateExistingSharedCache`。
`+0x9b594..+0x9b5b0` 调用：

```c
mmap((void *)0x280000000, 0x40000000, PROT_NONE,
     MAP_PRIVATE | MAP_ANON, -1, 0);
```

这里参数 `0x1002` **没有 MAP_FIXED**，是地址 hint。`+0x9b5bc..+0x9b5cc` 比较实际返回是否恰为 `0x280000000`，不相等进入真实 `dyld4::halt`。因此建立的是 `[0x280000000,0x2c0000000)` 的 1-GiB 用户地址保留，不是扩写内核 `SHARED_REGION_SIZE`，也不是把零页作为 cache 内容返回。

随后原 private 文件映射循环 `+0x3454c..+0x345c0` 将真实 cache 文件映入所需地址，dynamic region 仍调用真正的 `DynamicRegion::make`。

`deallocateExistingSharedCache +0x34224..+0x34230` 先 `check_np(&base)`，返回 0 才调用 `check_np(NULL)`。**该 helper 的实际内容与我们旧恢复基线的 empty-SR 诊断组合不能假定相同。**

这证明了包中的地址生产顺序。是否在本机同一启动上下文避免全部 guard／PMAP fault，还需实机对照，不能由静态顺序直接宣称。

证据：[追加 stub 反编译](reserve-stub-decompile.json)、[完整关键指令](dyld-caves-and-sites.txt)、[原 private 路径](dyld-eager-and-preflight.txt)。

## 2. sidecar 不是死文件：dyld 真正把它交给内核 pager

**RE-confirmed via share-dyld-arm64e +0x34790 → +0x9b680。**

原 page-in-linking 分流点现在跳到追加代码，而非仅改变 format 常量。消费者依次执行：

1. `+0x9b6a8..+0x9b6e4`：要求 slide=0、subcache index 为 0/1，并逐 128-bit 对照 cache UUID。
   常量位于 `+0x9b918` 和 `+0x9b928`，分别为 `4c1223e5cace3982a0036110a7a8a25c`、`2b390646b4b5302b841aefaa5283640d`。
2. `+0x9b6e8..+0x9b728`：真实 `open` enable 标记和对应 sidecar 文件。
3. `+0x9b72c..+0x9b7bc`：临时映射 sidecar 前 4 MiB，查找 `LC_CODE_SIGNATURE`，用 `fcntl` 命令 **97／F_ADDFILESIGS_RETURN**登记该文件内的签名。命令名与 XNU `bsd/sys/fcntl.h:398` 交叉核对。
4. `+0x9b7c0..+0x9b848`：检查 `MPAGER01` magic、blob 长度 `0x28 < size <= 0x3f0000`、region 数 `1..5`，每个提交 region 的 VA/size 必须匹配原 cache mapping；将每个 `mwl_region.fd` 改为本次实际打开的 sidecar fd。
5. `+0x9b84c..+0x9b860`：向 syscall **550／`map_with_linking_np`**提交 `metadata+0x10` 的 regions 和 `metadata+0xb0` 的真正 linking blob。
6. syscall 成功后，释放临时映射、关闭 fd，再打印 `MACPAD-PAGER15 linked OK` 并回到下一 subcache。打印点在真实 syscall 之后；静态看到这个字符串不是设备成功见证。

**重要的失败区别：**

- UUID/slide/enable/open/元数据前置检查失败：清理后 `+0x9b8ec → +0x34c00`，进入实际 eager fallback。
- syscall 550 已调用但报告失败：`+0x9b8f0..+0x9b910` 打印 failed，并以 **91** 退出；不是继续伪装成功，也不是无条件 fallback。

证据：[dyld-caves-and-sites.txt](dyld-caves-and-sites.txt)。这里确认的是调用及分支，不是异步 page-in 已完成或像素已正确。

## 3. format 12 的真实转换：全量 payload 语义对照通过

**文件核验，由上面的实际消费者定位 wire layout。** 两个 sidecar 的 linking blob 均为 version 7、16-KiB pages、**pointer_format=12**，slide=0、image_address=`0x180000000`。
format 12 是 `DYLD_CHAINED_PTR_ARM64E_USERLAND24`，是旧 XNU 已定义的格式，不是把内核跳转表加入 case 13。
已有 [T8103 实际内核 RE 记录](../../m1-dyld-pager-format13-20260930.md)还明确把 formats 1/9/12 归到 shared-auth helper；本轮没有重新载入内核，不能把该历史分支证据扩大成新设备的 syscall／page-in 验收。

| sidecar | region 数 | blob 字节 | binds 表条目 |
| --- | ---: | ---: | ---: |
| 0 | 5 | 28,448 | 188 |
| 1 | 4 | 2,282,864 | 282,557 |

针对两份实际 cache 和两个 sidecar 做了全量逐页对照：

- **24,508 页，401,539,072 字节的数据 region**。
- **22,818,485 个原 v5 链式指针**：14,836,648 个 plain rebase、7,656,359 个 auth rebase、325,478 个 auth bind。
- 所有链起点、next 步长、解码后的目标地址一致。
- 所有认证指针的 diversity、addrDiv、IA/DA key 输入一致；新格式 `key=0/2` 对应旧格式 `keyIsData=0/1`。
- 除已验证的链式指针编码转换外，所有页的其他字节一致。
- sidecar0 的 `0x1ee188000..0x1ee1ac000` 九页没有链条项，仍作为真实 mapping 提交；不要因为这些页数据看似平凡，就断言历史 pager-format 错误不可能发生。

这是**真正的编码／表示转换**：v5/format13 的 34-bit authenticated runtimeOffset 不能全部装进 format12 auth-rebase 的 32-bit offset。该包把较大的目标改为 **auth-bind + 24-bit ordinal + 64-bit targets 表**。实查两张 binds 表的全部目标均高于 `0x280000000`，相对 `0x180000000` 的偏移都超过 32-bit 范围。最小目标分别为 `0x2818a5030`、`0x281890820`。

因此不能复制其 `pointer_format=12` 一行而不转换 payload、chains 和 binds 表；那会破坏地址与认证语义。

证据：[wire headers](sidecar-wire-headers.json)、[完整计数](sidecar-semantics.json)、[bind 目标范围](bind-target-range.json)、[可复核脚本](verify_sidecar_semantics.py)。

脚本布局由 IDA 消费者和 XNU `dyld_pager.h`、`fixup-chains.h` 导出。首次在 IDA MCP 内批量运行被工具 60 秒 trace-profiler 超时中止，没有输出成功结果；随后将同一文件语义核验在普通 Python 执行，41.33 秒完整通过。原超时记录也保留，不能把它误解成 sidecar 数据失败。

**边界：**这不生成或验证真实 PAC 签名，不执行内核 pager，不证明 task admission、PMAP、COW 和冷启动正确；只证明当前两份 cache 的 sidecar payload 保存了相应静态语义。

## 4. eager fallback 确实执行 v5 fixup 和 PAC

**RE-confirmed via share-dyld-arm64e +0x34c00、+0x360f0、+0x362e0。**

fallback 进入原有 `MemoryManager` 可写事务，调用 lambda `+0x360f0`。它检查 slide-info version，v5 分支从 `+0x362e0` 开始逐页、逐链处理：

- 计算 `value_add + runtimeOffset + slide`；
- plain 指针恢复 high8；
- authenticated 指针调用 `Cache64e::signPointer`；
- 最后把计算结果写回实际 pointer slot，前进 next 链；未知 slide-info 返回错误。

`Cache64e::signPointer +0x5aabc` 是真实 `PACIA1716`，`+0x5aac8` 是真实 `PACDA X17,X16`。
Hex-Rays 简化后的伪代码可能看起来只是 `return this`；必须用对应汇编交叉核对，不能误判成 PAC no-op。

这类选择“已有正确回退实现”的补丁，与跳过 setup／fixup 后假装成功不同。它仍有运行时权限和内存成本需要实测。

证据：[eager lambda](eager-lambda.json)、[PAC 指令](cache64e-sign-asm.json)。我们旧的 `STATIC-m3-dyld-pager-format13.md` **已经定位过这个 F1 fallback**；新发现不能写成“以前没人想到进程内 fixup”。真正缺口在于没有以可信归属、真实 cache 输出和完整启动生命周期把这一组合验证到底。

## 5. exec helper：现成 jailbreak 准入的按代际协调，不是自定义 PMAP 修复器

**RE-confirmed via main15-exec-jitd-dock-reopen-20261010 +0x4000/+0x4844/+0x4c10（imagebase `0x100000000`）。**

`_main` 处理 ARM／ARM HOLD／CANCEL／SPAWN 请求；用 socket peer PID、路径检查、`proc_pidinfo(17)` 的 process uniqueid／parent uniqueid／idversion 关联归属。
`_stopped +0x4b10` 同时检查 BSD `SSTOP` 和 `MACH_TASK_BASIC_INFO` suspend_count。

`_watch +0x4844` 记录原 process uniqueid 和 idversion，最多 2,000 轮、每轮 `usleep(1000)` 等待同一进程的新 idversion 且已暂停，再调用 `_grant`；这是名义约两秒的轮询上限，不是严格两秒墙钟超时。

`_grant +0x4c10` 的实际操作：

```text
核对 chroot 路径、uniqueid、idversion
→ posix_spawn("/var/jb/usr/bin/jbctl", ["jbctl","proc_set_debugged",pid])
→ waitpid，要求该 helper 正常成功
→ 再核对路径、uniqueid、idversion
→ csops，要求 CS_DEBUGGED (0x10000000)
→ 按原 ARM/HOLD/SPAWN 请求决定是否 SIGCONT
```

实际 callsite：`+0x4d7c` spawn、`+0x4df8` waitpid、`+0x4eb0` csops、`+0x4ebc` 检查 debug 位、`+0x4eec` SIGCONT。失效／失败后的回收仍重新检查归属和暂停状态。

helper 自身的已分析函数不执行自定义 PMAP/PTE/kernel-text 写入；它**依赖现有 jbctl 的实际 jailbreak 行为**。没有分析来源设备上 jbctl 的后端，不能声称整个准入过程完全不改内核数据。
该 helper 也不是每个应用二进制的 hash/UUID allowlist：chroot 内路径和进程代际，与外部启动器的路径／属主检查是不同边界。

证据：[五个函数反编译](exec-jitd/functions.json)、[watch 汇编](exec-jitd/watch-asm.txt)、[grant 汇编](exec-jitd/grant-asm.txt)。字段名称由 XNU `proc_info.h`、`task_info.h` 交叉核对。

导出脚本是 [export_exec_jitd.py](export_exec_jitd.py)，只用于 IDA **batch**：将它作为 `idat -A -S"<script> --batch-export" -o<独立输出.i64> <已核验 helper>` 的脚本。它校验目标文件名后导出并退出；不要在用户交互 IDA 会话中执行。

**不能过度归因：**我们的 `misc/run_dbg.c` 历史上也有 START_SUSPENDED＋`proc_set_debugged`。因此“以前完全没开 debug”不成立；要比较的是被标记的是否是真正目标及正确 exec 代际、是否覆盖后续子进程，以及对应 mapping/pager 组合。静态 helper 分析仍没有证明 M1 旧 `code0=0x32` 必然消失。

## 6. 可见 dyld 阶段之间的完整段数据差异

继续对比时，先排除了“文件名就是来源证明”的误区。包内两份 `original` 目录下的备份，其 arm64e CodeDirectory 标识分别是 `dyld-15.6-fat-native-20261003` 与 `dyld-15.6-private-reserve-diagnostic`；当前产物是 `dyld-pager-candidate`。它们是有阶段标签的材料，不能称为经过独立核验的 Apple pristine。

| 本地对照输入 | arm64e thin SHA-256 | 当前可确认的身份 |
| --- | --- | --- |
| `dyld-packaged-earlier-arm64e` | `bb5122e19ffa7ec50380a77a7b77bced997028e53c4f40da2f5440d0029a4be8` | 从包内 `.../usr/lib/dyld-15.6` 提取的早期材料 |
| `dyld-packaged-reserve-arm64e` | `eca71e832d3f863eba2106612b0ce6e9e1a26ca5ee76bf8303653d26267f483a` | 从包内 `.../native15-cache-24G90/usr-lib-dyld-24G90` 提取的 reserve 阶段 |
| `analysis/dyld_15.6.1_arm64e_thin` | `12dc97d541939a8e05d58f265f62eaef93fbce63740d7eefaee434cea7acbac5` | 当前旧研究工具命名为 PRISTINE 的本地输入；不是最终冻结的 b8fdbc1b 文件 |
| `share-dyld-arm64e` | `def1d5e2ccde2ee7930a30f38d540981aab2ccda9a7f0d6212bd2f9339dd3a00` | 当前已核验分享产物 |

四份输入的架构／平台头一致：arm64e subtype `0x80000002`、macOS platform 1、minos/sdk 15.6、Mach-O flags `0x85`。比较全部对应的 file-backed segment payload，排除实际头部/load commands 和 `__LINKEDIT`（签名与符号元数据）；**包括首个 4-KiB 页中 load commands 之后的 padding**，避免漏掉历史上 `0x970` 一类 header-page cave：

| 与当前分享产物比较 | 全部差异范围（4-byte 对齐） |
| --- | --- |
| 包内早期版本 → 当前 | `+0xa67c`、`+0x344ec`、`+0x34790` 三个指令窗；`+0x9b580..+0x9b620` reserve stub/文本；`+0x9b680..+0x9b9f4` pager stub/常量/文本 |
| 包内 reserve 版本 → 当前 | 只有 `+0x34790` 指令窗和 `+0x9b680..+0x9b9f4` pager 部分 |
| 本地 12dc97d5 → 当前 | 与早期版本比较的上述五处，再加 `+0x841b0..+0x841b8` Rosetta 查询入口 |

这已封闭**这些可见阶段之间的段 payload 差异清单**，不能改写成“相对 Apple 原版的完整补丁清单”。机器可读结果包括每个窗的 old/current bytes 和函数归属，见 [差异清单](dyld-input-differences.json)、[比较脚本](compare_dyld_inputs.py)、[签名身份](dyld-signature-identities.json)。

对 `analysis/` 下符合旧记录精确文件尺寸的 14 个普通文件做完整 SHA 比较，没有找到最终冻结 `b8fdbc1b…381b91e` 或更早 `99569152…51a1` 的原样副本；[搜索记录](frozen-baseline-search.json)明确限定范围，没有搜索其他磁盘／压缩备份，也不证明这些文件永久丢失。

## 7. 新确认的前提：默认关闭通用 page-in linking，单独开启受约束 sidecar 路径

**RE-confirmed via share-dyld-arm64e +0xa67c/+0xa680、+0xbc9c、+0x21e04。**

包内早期版本的 `+0xa67c` 是 `MOV W8,#2`，当前是 `MOV W8,#0`；下一条把值写到 `Process+0xc4`（pageInLinkingMode）。`+0xa688` 之后仍保留 `internalInstall()` 门控下的 `DYLD_PAGEIN_LINKING` 解析，因此这改变了**默认值**，并非删除所有配置逻辑，也不能说任何环境下永远为 0。

实际 `DyldCache` constructor 读取该字段，达到 2 才考虑通用 cache page-in linking。`Loader::applyFixupsGeneric +0x21e04` 也读取它：为 0 时直接走真正的即时 chain fixup，不把普通 Mach-O 的页交给通用 `setUpPageInLinking`。
另一方面，share dyld 的 `+0x34790 → +0x9b680` 仍为指定 cache UUID／slide=0 的 sidecar 单独调用 syscall 550。因此正确描述是 **“通用路径默认 eager，已转换的精确 cache 使用专用 pager 路径”**，不是“完全禁用 pager”或“所有文件都能用 format12”。

证据：[Process 指令](process-config-pagein-asm.txt)、[Process 反编译](process-config-decompile.txt)、[cache 配置](cache-config-decompile.txt)、[普通镜像 fixup](generic-image-fixups.json)与[对应模式门控汇编](generic-mode-gate-asm.txt)。该前提在上一轮只分析 cache 追加 stub 时未完整展开，现加入以后复现组合。

**还核实了旧实验补丁没有被照搬：**

| 旧 builder 中的实验 key | 当前分享 dyld 的实际指令 |
| --- | --- |
| `crossarch`：把全局 trap 改为 mov-zero | `+0x76270` 仍是 `SVC 0x80` |
| `plataccept`：NOP cache 平台比较分支 | `+0x35c24` 仍是 `B.NE` |
| `imgplat`：NOP cache-image 平台检查 | `+0x2b580` 仍是 `TBZ W0,#0` |
| `hardpriv`：强制 private 分支 | `+0x34268` 仍是 `B.NE`，private 选择依赖真实配置 |

见 [当前指令证据](inherited-patch-sites.json)。表格对照的是旧 builder 的实验 key 与分享产物，**不声称这些 key 在每次历史实验或最终 b8fdbc1b 文件上都全部启用**。

分享产物另在 `_rosetta_dyld_is_translated +0x841b0` 使用 `MOV W0,#0; RET`。本地 12dc97d5 对照的同一函数会调用 `__crossarch_trap`，见 [实际参考二进制反编译](reference-dyld/rosetta-query.json)。这是把处理限定到该查询入口，而非修改整个 crossarch syscall stub；它仍是一个**返回常量的 native-only 兼容假定／scaffold**，没有本轮运行证据证明其所有调用语义，不能叫做 Rosetta 支持或通用正确性修复。

## 8. 生成器缺失时，已经恢复的重建契约与仍缺的证据

本轮没有创建或部署新的 sidecar。根据实际消费者、完整文件语义对照，已能列出将来生成器必须实现的契约：

1. 固定 cache 完整 SHA／UUID／layout／slide=0；保留原 cache 文件。
2. 将有 slide-info 的原始 data mappings 存入独立、可签名的 Mach-O；`mwl_region` 的目标 VA、长度和保护属性不变，文件 offset/fd 改指 sidecar。
3. 按 v5 链遍历指针并重编码为 format12：plain 指针保存 target/high8，auth 指针保存 target/diversity/addrDiv/key；超过 32-bit offset 的 auth 目标通过 64-bit bind targets 表和 24-bit ordinal 表示，不能截断。
4. 保留逐页 chain starts 与 next，构造真实的 `mwl_info_hdr` 和 segment chains；映射中没有链的页同样保留全部原字节。
5. 生成同文件 CodeDirectory，纳入实际设备的签名／trust 恢复；不能复用旧 CDHash。
6. 先通过现有全量语义核验，再做真实目标 PID 内的 syscall/page-in/PAC/fork/exec/冷启动验收。

这些是**从交付物恢复的格式和行为契约**，不是原作者生成器源码或 bit-identical 构建证明。bind 表排序/去重策略、元数据对齐、签名实现和完整工具版本仍需原构建材料，或者另做独立实现及验收；没有必要为了补齐文档先生成一个未验证的新 rootfs。

文件体积不等于常驻 RAM：1 GiB 是 PROT_NONE 地址保留，401,539,072 字节是 sidecar 的 data payload。**THEORY：**按需 page-in 相比 eager 可能减少启动时触页和 COW 开销；本轮没有 RSS／缺页／内存压力测量，不能把这两个数字直接当成每进程 RAM 需求或节省值。

以后恢复 15.6.1 的有限时对照应记录同一目标的输入 SHA、入口 launcher、process uniqueid/idversion、真实 cache UUID、pageInLinkingMode、sidecar 分支及实际 fault：

- 若仍在首次 text page 得到 code 50，先比较 exec／PMAP 生产层；不要将尚未执行的 format12 路径算作已失败。
- 若 syscall 550 成功但随后 data page fault，读取真实 pager/format/保护与 error，不能以 `linked OK` 代替异步 page-in 验收。
- 如需比较 eager 与 sidecar，只使用实现自身的 fallback 契约，并保存原标记/配置；不能返回成功而跳过 fixup。
- 本轮未取得本机 M1 运行结果，且最终冻结文件原样副本尚未定位，因此不做“同一 M1 已经证明能跑”的结论。

## 对复盘的结论更新

可以从 THEORY 升级为已核验事实的是：高地址保留顺序、实际 sidecar 消费者、format12 payload 转换、eager v5/PAC 分支、exec 代际协调的具体实现。
仍待运行证据的是：来源包在本机 M1／20D47 上通过首次 text fault、真正 CLI、冷启动、fork/exec 和 GUI 验收。

对当前 Ventura 最可迁移的是“保留真实语义的表示转换、正确进程身份／代际和可验证 fallback”。不能把这份 24G90/固定 cache UUID 的 sidecar 或额外 debug 调用直接加入 22F82，也不能用它解释或跳过当前 TCC 回复问题。
