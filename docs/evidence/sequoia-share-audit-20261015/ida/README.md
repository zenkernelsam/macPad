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

## 对复盘的结论更新

可以从 THEORY 升级为已核验事实的是：高地址保留顺序、实际 sidecar 消费者、format12 payload 转换、eager v5/PAC 分支、exec 代际协调的具体实现。
仍待运行证据的是：来源包在本机 M1／20D47 上通过首次 text fault、真正 CLI、冷启动、fork/exec 和 GUI 验收。

对当前 Ventura 最可迁移的是“保留真实语义的表示转换、正确进程身份／代际和可验证 fallback”。不能把这份 24G90/固定 cache UUID 的 sidecar 或额外 debug 调用直接加入 22F82，也不能用它解释或跳过当前 TCC 回复问题。
