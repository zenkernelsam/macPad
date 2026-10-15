# 分享包的实际角色闭包与输入差异（2026-10-15，离线审计完成）

这一轮补充初版报告只看通用 reference-source 的不足。依据是再次从原归档提取的角色二进制、实际 plist、IDA/Hex-Rays 导出、ldid 读取和精确 cache 符号／指令。没有运行 iPad、没有安装任何库。

## 1. 重要纠正：分享包保留了 blanket TCC override

**源码确认：**原作者 `analysis/MacWSBootingGuide/libmachook/mac_hooks.m:5661` 和分享包 `reference-source/sequoia-hook-source/libmachook/mac_hooks.m:5689` 均有：

```c
// grant all permissions
MSHookFunction(MSFindSymbol((MSImageRef)header,
    "_audit_token_check_tcc_access"), hooked_return_1, NULL);
```

我们当前源码的 R003-F01 段明确移除了这条安装（commit `685624f`），理由是恒真权限覆盖不能作为修复，而且当时的 22F82/provider 组合无法解析该局部符号。

**RE-confirmed via packaged libmachook_arm64.dylib +0x7b74/+0x7b84：**这不是仅存在于参考源码的旧行。实际 arm64 产物的 `loadImageCallback +0x71b8` 在 SkyLight 路径匹配分支，调用 `MSFindSymbol`，再将结果传给 `MSHookFunction`，replacement 是 `hooked_return_1 +0x607c`：

```text
+0x607c  20008052  MOV W0,#1
+0x6080  c0035fd6  RET
```

该安装位于按 SkyLight 路径匹配的分支，没有独立 TCC 诊断开关。包内 WindowServer 为 arm64，正常 launch profile 指向主 native15 launcher；该架构选择 `/usr/local/lib/libmachook_arm64.dylib`。证据：[实际 hook 函数与汇编](WindowServer-hook/functions.json)、[文件身份](WindowServer-hook/identity.json)。

**RE-confirmed via actual CydiaSubstrate/ElleKit +0xe948/+0x19794：**它与我们曾使用的 no-op scaffold 不是同一 provider。`MSFindSymbol` 会调用 `findSymbol(image:symbol:)`，后者遍历传入 Mach-O 的 `LC_SYMTAB`／`__LINKEDIT`，并不要求候选带 `N_EXT`。详见 [provider 实现](Substrate/functions.json)。

**文件核验＋实际指令：**当前 24G90 cache 中 SkyLight UUID 为 `4E052846-80C2-38AF-85BF-1482E070A32B`，image VA `0x1864af000`；其符号表确实有非零的局部 `N_SECT (0x0e)`：

- `_audit_token_check_tcc_access` = `0x1868b97f8`，image-relative `+0x40a7f8`。
- `_CGXSenderCanSynthesizeEvents` = `0x18688d2c8`。

后者在 `0x18688d31c..0x18688d330` 把前者的真实函数指针传给权限 dispatcher。原 checker 在 `0x1868b9860` 调用缓存的 TCC 函数，随后 `CMP W0,#0; CSET W0,EQ`。因此恒返 1 会绕过该 checker 原本进行的 TCC 结果计算，不是恢复真实服务回复。

符号取自精确 cache 的真实 `LC_SYMTAB`，不是搜索字符串猜函数地址；header offsets 用项目 dyld C header 的 `offsetof` 编译核对。原指令使用 Capstone 5.0.7 对从该 cache 精确 VA 读取的函数字节解码；provider/hook 使用 IDA Pro 9.3/Hex-Rays。见 [符号可用性](skylight-symbol-availability.json)和[原 gate/checker 指令](skylight-checker-disasm.txt)。

**仍未证明：**运行中的分享设备是否已成功落钩、每次 post 是否命中、是否有其他 guard／dispatch 拒绝。包内没有对应原始运行 trace，不能将静态可解析／安装尝试升级为 runtime-confirmed。也不能把我们的“22F82 上这个 provider 解析不到”直接套给不同 provider 与 24G90 cache。

**对项目的影响：**“同样能点开桌面”不自动意味着两边运行着同样的权限策略。当前 Ventura 正在修复真正的 system-tccd transport／reply；分享包含有试图恒真绕过这层检查的代码。R003 移除它不是应该撤销的错误，不能为了快速复刻画面把它重新装回去。初版“相关 entitlement 相同”的观察仍成立，但不足以覆盖这条二进制行为差异。

## 2. Dock 是单独的启动角色，不能只审通用 libmachook

实际 `gui-launchd/com.macwsguide.dock.plist` 使用 `launchdchrootexec-dock-reopen-20261010`，并通过 `MACWS_EXTRA_INSERT_LIBRARIES` 加载五个附加库：trash-volume、trash-event-v2、launchpad-trash、launchpad-backdrop、dock-actions。`macos_gui.sh:3460–3506` 的 native15 生成逻辑也会重建这组配置。

**RE-confirmed via launcher +0x4d48：**专用 launcher 的架构选择函数对 arm64e 返回 `/usr/local/lib/libdockfix.dylib`，对 arm64 返回通用 `libmachook_arm64.dylib`。包内 Dock 是 arm64e，因此其配置链选择前者。普通 [WS launcher](WS-launcher/functions.json)的同名函数对 arm64e 选择 `libmachook.dylib`、对 arm64 选择 `libmachook_arm64.dylib`；不能把两个 launcher 视为只有文件名不同。已导出的 [Dock launcher](Dock-launcher/functions.json)保留真实 chroot、uid/gid、额外注入和 exec 准入步骤。

`libdockfix` 内有完整 AppInput、RBS、文件系统、图形和生命周期兼容代码，不能把它当作一个只修 Dock 复开的几行库。其 constructor 的语义还包括大量历史诊断路径和受控开关；对另一个 macOS 版本的整体替换必须先审核实际启用集合。

## 3. Dock actions 修的是 AppleEvent endpoint，不是 WS 全局输入

**RE-confirmed via libmacpad-dock-actions-20261012.dylib arm64e +0x830/+0x1148：**constructor 限定 `MACPAD_TARGET_MACOS_VERSION=15.6.1` 与 private-cache lifecycle，再区分 Finder 与 Dock。Dock 分支寻找实际 Dock Mach-O 的 `_AESendMessage` import stub，安装自己的 sender。

sender **先调用原始 AESendMessage**；仅当原始结果为 `-600`、目标 PSN 能解析成 PID、event class 为 `aevt` 且 event ID 为 `rapp`／`quit` 时才采用兼容路径：

- Finder reopen 尝试将原始 AppleEvent flatten 后发到该 Finder 的专用 socket，最多 64 KiB，读取实际返回状态。
- 其余 fallback 将 reopen/quit 编码成既有 `MacWSInputRecord` kinds **18/25**，发往目标 PID 的 AppInput socket。
- socket 必须为实际 socket、同 uid、无 group/other 权限，目标 PID 存活。

证据：[实际函数与汇编](dock-actions/functions.json)；84-byte record 的 kinds 18/25 与当前 `include/macws_host_protocol.h` 的 ReopenApplication/PerformQuit 对应。**已有协议能力可以复用，不需要新造一套输入系统。**

但 fallback 的部分成功值仅表示 84-byte 数据报已发出，不是应用已完成 Quit/Reopen；不能以返回 0 代替真实窗口／进程／未保存文档交互验收。也不能因 Dock action 可执行就推断 `_XPostEventRecord` 或 CGEventPost 已被正常授权。

## 4. 实际签名读取与工具校验

对指定 arm64/arm64e slice 用 `ldid -e` 解码的 key 数：Dock 139、Finder 197、WindowServer 115、tccd 127、OSXvnc 117、专用 launcher 115。相关 `tcc.allow` 仍是 ListenEvent＋SystemPolicyAllFiles，没有直接增加 PostEvent grant；原生角色 entitlement 与项目集的其他键不同，不能把全局 plist 当作所有角色的最终签名。

这次宿主 `codesign -d --entitlements :-` **exit 0 但报告 no signature**；`otool` 明确有 LC_CODE_SIGNATURE，`ldid` 可以解码。这个工具差异没有被当成“没签名／没 entitlement”的事实。两个 dylib 的 `ldid -e` 没有可解析输出，记录为未取得 entitlement 结果，不从空输出推断权限状态。

完整方法、fat/thin SHA 和解码结果见 [角色 metadata](installed-role-metadata.json)。这是文件签名内容的读取，不是设备的动态 trust 或权限生效验证。

## 5. 当前 Ventura scaffold 的能力缺口：本地行为证据

当前 `misc/cydiasubstrate_macos.m` 的源码明确声明：函数 hook 只为预登记的 UUID/offset/prologue 安装；约 70 个未审查的 callsite 保持 pass-through；`MSHookMessageEx` 是 no-op。这是 R003 的有意 containment，不应被称作通用 Substrate 修复。

用**未修改的该源码**构建了本地 macOS 小探针（普通 NSObject 子类、独立本地／导出符号），没有操作 iPad 或真实 WindowServer。完整 [运行记录](provider-contract-probe.json)和[探针源码](provider_contract_probe.m)给出：

```text
symbol hidden=0x0 reference=0x1017c7300 expected=0x1017c7300 exported=0x1017c7310
message-hook before=3 after=3 replacement-expected=7 old-callable=3
VERDICT=scaffold-contract-gaps-reproduced
```

导出符号的正对照正常；同一局部函数通过正确的 LINKEDIT 地址换算可得到其真实地址，但 scaffold 返回 NULL。方法 hook 仅交回原 callable，实际方法没有变成 replacement。

**SOURCE-CONFIRMED 计算问题，并有上述本地 runtime 反例：**当前 resolver 对每个 segment 重算 `seg_slide = mh - seg.vmaddr`，再令 `le_lo = seg.vmaddr + seg_slide`，使 LINKEDIT 下界退化为 `mh`；symtab/strings 又按 `slide + fileOffset` 访问。一般正确的文件偏移换算应沿真实 TEXT slide 和 `LINKEDIT.vmaddr - LINKEDIT.fileoff`。不能从这个 resolver 的 NULL 推导“所有 dyld4 cache 的局部符号都不可取得”。

这不是已完成的设备修复，也没有独立证明历史 22F82 NULL 就只由这个计算错误导致。需要对真实已加载镜像、已安装 provider 和目标符号继续验证，保留 export-trie、re-export／stub-resolver、PAC 和 bounds 的完整契约。探针最初的 NSObject 初始化错误及一次旧二进制误跑已排除，未用于支持结论。

**新增可用材料：**分享包的 CydiaSubstrate 实际包含 arm64/arm64e，两个 slice 声明 macOS 13.0，依赖包括 Foundation、CoreFoundation、libobjc、libSystem 和 Swift runtime。其 arm64 `messageHook +0x1bd6c` 真正调用 `class_replaceMethod` 并交回原 callable，与当前 scaffold 的空安装不同。它提供了原先“缺少 macOS-built provider”问题的具体候选，值得离线做完整兼容审计；minos=13 不等于已在 22F82 验收。见 [load commands](Substrate/load-commands.txt)、[依赖](Substrate/dependencies.txt)和 [method hook 反编译](Substrate/functions.json)。`otool -L` 的第一条 `/usr/local/lib/libellekit.dylib` 是 install name，不能误判成另一个缺失依赖。

不能直接把它覆盖进当前 rootfs：完整 provider 可能激活以前被 scaffold 拒绝的旧 hook，尤其必须先确认 blanket TCC／assert／validation override 已移除或隔离。应分别验证 `DYLD_INTERPOSE`、显式 `method_setImplementation`、`MSHookFunction`、`MSHookMessageEx`，不能用一种 hook 的成功证明其他机制正常。

## 对当前 Ventura Agent 的即时反馈

1. 保留 R003 去掉恒真 TCC override 的结果；继续用实际 WS request/reply 排查真实服务路径。
2. 比较成功案例必须把 **provider＋实际二进制＋角色注入＋权限策略**一并列出来，不能只比较 OS 版本或 entitlements.plist。
3. 输入事件投递、AppleEvent 命令投递、AppKit 生命周期、最后一帧显示分别验收；不要把某一层 rc=0 升格为完整桌面成功。
4. 若在 22F82 已经观察到真实 `AESendMessage=-600`，可先查现有 AppInput 18/25 路线是否已正确启用，而不是照搬 24G90 的库；没有同类失败时，这不是当前 TCC 阻塞的优先修复。
5. 把 provider 完整性作为独立的基础能力工单：现有 gate 诊断可用，不代表 ObjC method hooks 和所有生产函数 hooks 都已恢复。先做离线能力／符号换算／依赖审计，再选择有证据的最小修复；不以取消 permit 约束代替完整实现。

本页仍在补充角色闭包；原始全目标阅读和最终结论审计见 `../completion-audit.json`。
