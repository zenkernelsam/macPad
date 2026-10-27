# R007 只读审计：合并 provider 后的新激活 hook 与部署验证契约

日期：2026-10-18。范围：审计当前合并源（`3c4d581` provider + `4bcf20d` F1 +
R008）中，`MSHookMessageEx` 与 LINKEDIT 局部符号修复会新激活哪些 hook/调用、
门禁与副作用，并给出双架构装载/像素/输入验证计划。**未改源码/测试/构建输入、
未构建、未操作设备、未执行 Git 写入。** 只新增本文件与
`coordination/_relay/round-007-frontend-result.md`。

候选 HEAD：`7e5d771`。后端 R004 的设备/native 对照不在本轮。

## 1. 检查身份

| 文件/对象 | SHA-256 |
| --- | --- |
| `libmachook/mac_hooks.m` | `e8ae9dadfa5bb40701a681661cb7677e729f94b35807355f5ea5ae0af71eb08d` |
| `libmachook/Metal_hooks.x` | `a49c52c14319b7991977c2ff1a5e39c353ef2f3a6824e82bec9f8b7335291e4a` |
| `libmachook/Compatibility/MacWSSteamProcess.m` | `4eae959fcc2d19574cef05e272e185ec85852edb927ad44d44ebdf60d55a9d30` |
| `misc/cydiasubstrate_macos.m` | `6ac796c78f11fce18806840a20276308c782dbc77603c7781fc55d35eeaa7c2b` |
| `include/macws_app_input_route.h`（F1） | `29d7e17defb694cd3346c5b1868c0784f6c9dda98f8a4cf856d841e5c164440d` |
| `misc/macos_mshook_provider_probe.c` | `0e8d29a86e87f449655c3c8989cbba6e686c2825075e97f4e24ec08f505d9fe0` |
| `layout/usr/macOS/bin/macws_diagnostic_flags.sh` | `31e93fcd8059a1f1e12dbc612df7a32f9e7cedaacaf134f3237bed0045e34346` |
| `analysis/SkyLight_22F82`（UUID `96676A53-…5380`） | `13d7a0ca010a9808234c71c34fd650b26329de99d1aceaeb5ddbb58f8b906c61` |
| `analysis/device-leftovers…/libxpc.dylib`（macOS 15.6） | `4de7869b35874718f5025395b0116b925c2ef4e053863bf463beb34133e1c9f8` |
| 预修复运行记录 `r004b-canonical-post2.json` | `8a280fddb7992a419b05182841e18f95d937619ac19c90da53f9b05b715b216e` |
| 预修复 provider probe `provider-probe-r003-final.json` | `f7c617c26a3193111de9f35116b33afeb0580689d6b8b615153336adbb02eec7` |

## 2. 预修复运行见证（本地 artifact，FACT）

`r004b-canonical-post2.json` / `provider-probe-r003-final.json` 记录的部署
provider 路径为 `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`。
同一日志显示：

```text
#### XPC_BOOTSTRAP: fn=0x0 dict=... (registering Metal compiler service)
#### XPC_BOOTSTRAP: SYMBOL NOT FOUND
#### SkyLight PrepareForUse RE-verified fallback at ...d3ac
MACWSHook: REFUSED no permit for target
#### SkyLight PrepareForUse tolerate-nil hook installed at ...d3ac
#### SkyLight StartCompositeForDisplayStream: symbol not found, skipped
#### SkyLight StartComposite(WSCD) RE-verified fallback at ...1358
MACWSHook: REFUSED no permit for target
#### SkyLight EndCurrentComposite(bool) RE-verified fallback at ...053c
MACWSHook: REFUSED no permit for target
#### SkyLight EndUpdate RE-verified fallback at ...00b0
MACWSHook: REFUSED no permit for target
#### SkyLight _state_stack pop_back: symbol not found — falls back to manual deque pop
#### MACWS_HID_BYPASS installed SetMatchingMultiple → no-op (1) at ...
```

这证明合并前：局部符号返回 NULL、走字节签名 fallback；`MSHookFunction`
因 **no permit** 拒绝；日志仍打印 “installed”。合并后 LINKEDIT 修复会让
这些符号解析成功，但 permit 门禁不变；必须验证“解析成功但拒绝安装”的新
路径不会改变行为，且日志不再被误读。

## 3. `MSHookMessageEx` 新激活（7 个 callsite）

Provider 的 `MSHookMessageEx` 从 no-op 变为 `class_replaceMethod`
（`misc/cydiasubstrate_macos.m:564-582`）；先发布原 IMP，缺失方法失败关闭。
以下 7 个调用点现在真正安装：

| # | 角色/门禁 | 类/selector | replacement 行为 | 原调用/所有权 | 可能的激活副作用 | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `macws_is_stray_process()` 且（`macws_stray_agx_compat_enabled()` 或 sentinel `/tmp/macws_pipeline_diag`） | `_MFXTemporalScalingEffectV3 -encodeToCommandBuffer:`（`Metal_hooks.x:17642-17665`） | 记录纹理/格式，末尾调用原 IMP（`17560-17640`） | 原 IMP 先保存，末尾调用 | 仅 Stray 诊断；读对象 selector，无 bypass | 诊断 |
| 2 | sentinel `/tmp/macws_iogpu_error_diag` | AGX `newBufferWithLength:options:`（`23284-23310`） | 调原 IMP，记录 buffer 元数据（`23200-23211`） | 原 IMP 调用 | 仅 AGX error 诊断；可增加日志/对象读取 | 诊断 |
| 3 | 同上 | `AGXBuffer -didModifyRange:`（`23302`） | 记录范围，末尾调原 IMP（`23213-23245`） | 原 IMP 调用 | 仅诊断；异步范围观察 | 诊断 |
| 4 | `MACWS_BUFFER_SUBALLOC_DIAG` | `IOGPUMetalBuffer -initWithPrimaryBuffer:...argsSize:`（`23398-23420`） | 记录 parent/args，返回原 IMP（`23328-23396`） | 原 IMP 调用 | 仅诊断；大量对象读取 | 诊断 |
| 5 | 程序名 `pkd`（`mac_hooks.m:15987-15990`） | `LSPlugInKitProxy -entitlements`（`15976`） | 先调原 IMP；为空时读取真实签名 DER 并缓存（`15917-15945`） | 原调用；空值才替换 | PKD/PluginKit 看到真实 entitlement；可能改变插件接受路径 | 兼容（非 bypass） |
| 6 | `MACWS_LSD_ROLE=session` + `MACWS_LSD_SESSION_USER_DIR`（`16148-16175`） | `_LSDefaults -databaseStoreFileURL`（`16161`） | 调原 IMP，把 URL 移到 session 目录（`16125-16138`） | 原调用 | lsd 数据库路径隔离 | 兼容 |
| 7 | 同上 | `_LSDefaults -databaseStoreFileURLWithUID:`（`16168`） | 同上 | 原调用 | 同上 | 兼容 |

**FACT：** 1-4 的诊断仅在对应 sentinel/env 下安装；生产 preflight 的
diagnostic pattern（`macws_diagnostic_flags.sh:105`）覆盖
`MACWS_BUFFER_SUBALLOC_DIAG`、`MACWS_AGC_*`、`MACWS_KEEP_*`、
`MACWS_HID_BYPASS` 等，`macos_gui.sh:3768-3904` 会拒绝生产 plist 中的这些
变量。**5/6/7 会在正常 pkd/lsd session 角色中真正安装**，属于本轮最重要的
生产行为变化。

**构造时序：** provider 是 libmachook 的直接 dylib 依赖，`MSHookMessageEx`
在 libmachook 构造器前可用。PKD 通过 `_dyld_register_func_for_add_image`
安装，可重试；LSD 使用 `dispatch_once`，若 `_LSDefaults`/方法在
`LaunchServices.framework` 加载回调时不可用则**不会重试**（`16148-16158`）,
是验证时需专门检查的构造时序风险。MetalFX/AGX 安装器在类/方法缺失时
返回，后续路径可重试（原子 installed 标志在方法找到后才置位）。

## 4. LINKEDIT 局部符号新解析（22F82 SkyLight 为精确本地证据）

`analysis/SkyLight_22F82`（base `0x1850e5000`）的 `LC_SYMTAB` 中以下均为
`t` 局部符号；旧 `MSFindSymbol` 返回 NULL，LINKEDIT 修复后解析成功：

| 符号（callsite） | 22F82 地址 / image offset | 旧行为 | 新路径/后果 | permit 状态 |
| --- | --- | --- | --- | --- |
| `MetalIOSurfaceBacking::PrepareForUse`（`mac_hooks.m:2993`） | `0x185404fc4` / `0x31ffc4` | 回退到 `MetalTiledBacking` `0x3243ac` | **选中错误的类**：22F82 失败路径是 `MetalTiledBacking`；fallback 被跳过 | 无 permit → 拒绝 |
| `MetalContext::StartCompositeForDisplayStream`（`3020`） | `0x18522dd74` / `0x148d74` | “symbol not found, skipped” | 解析后调用 MSHookFunction | 无 permit → 拒绝 |
| `MetalContext::StartComposite(WSCompositeDestination)`（`3033`） | `0x18522d358` / `0x148358` | fallback 同址 | 解析；fallback 跳过 | 无 permit → 拒绝 |
| `MetalContext::StartComposite(MTLTexture)`（`3073`） | `0x18522dfb8` / `0x148fb8` | fallback 同址 | 解析；fallback 跳过 | 无 permit → 拒绝 |
| `MetalContext::EndCurrentComposite(bool)`（`3111`） | `0x18522c53c` / `0x14753c` | fallback 同址 | 解析；fallback 跳过 | 无 permit → 拒绝 |
| `MetalContext::EndUpdateEb`（`3165`；`Ebb` 不存在） | `0x18522c0b0` / `0x1470b0` | fallback 同址 | 解析；fallback 跳过 | 无 permit → 拒绝 |
| `std::deque<RenderState>::pop_back`（`3216`） | `0x18522afa0` / `0x147fa0` | symbol not found，手动 pop | 指针非 NULL，但仅被已拒绝的 replacement 使用 | 无 MSHookFunction |
| `WSCompositeDestinationCreateWithMetalTexture`（`3228`） | `0x18523053c` / `0x14b53c` | fallback 同址 | 解析；MSHookFunction 拒绝；**另见 6165** | 无 permit |
| `MetalContext::StopCapture`（`3262`） | `0x18522c1bc` / `0x1471bc` | symbol not found，跳过 | 解析后 MSHookFunction | 无 permit → 拒绝 |

**关键发现（FACT）：** `MSHookFunction` 的 permit 表只有 **1 个**注册
（`mac_hooks.m:6040-6088` 的 CGXSender），其余 SkyLight 目标全部被
`REFUSED no permit`（provider `433-459`）。因此这些局部符号解析**不会**
安装函数 hook；新变化是 fallback 选择、日志和 `*result` 指针。若未来为
合法 compositor hook 添加 permit，`PrepareForUse` 的符号优先逻辑会指向
`MetalIOSurfaceBacking`（0x31ffc4），而不是 22F82 实际失败的
`MetalTiledBacking`（0x3243ac），必须先用精确 UUID/prologue 修正选择。

**直接使用（不经 MSHookFunction）的新激活：**

| 符号/callsite | 本地证据 | 门禁 | 新行为/副作用 |
| --- | --- | --- | --- |
| `__xpc_bootstrap_services`（`mac_hooks.m:6360`） | 15.6 `libxpc.dylib` 中 `t` 局部符号 `0x18017bb84`；22F82 精确 libxpc 未本地 | **无**（libxpc load 分支，所有注入进程） | 预修复日志 `SYMBOL NOT FOUND`；修复后可能真正调用并注册 Metal 到 libxpc bootstrap 服务表；需验证 result 与 XPC/launch 回归 |
| `_libAccessibilityBaseImplementationsLibraryCore`（`MacWSSteamProcess.m:1449`） | 私有符号，注释明确 dlsym 不可达；精确 HIServices 未本地 | 顶层 Steam 浏览器 | 预加载 HIServices Accessibility runtime；改变初始化时序 |
| `_CreateSimpleProcess`（`MacWSSteamProcess.m:1823`） | 私有 libtier0_s 符号；精确 Steam 二进制未本地 | Steam 进程 + libtier0 路径 | 使 Steam helper 进程创建适配器可用；需验证 spawn/原路径 |
| `_getMetalPluginClassForService`（`Metal_hooks.x:25099` `%init`） | 精确 Metal 未本地；可能 exported 或 local | 无（所有加载 Metal 的进程） | 若原本为 local，AGX plugin redirect `%hookf` 将新激活；影响 device class/AGX init/pixels；必须先核对精确 Metal UUID 的 symbol table |
| `WSCompositeDestinationCreateWithMetalTexture`（`mac_hooks.m:6165`） | 22F82 local（上表） | `MACWS_KEEP_RENDER_UPDATE_CBZ`（生产 preflight 拒绝） | 旧 NULL → 跳过 render_update CBZ 直接 patch；新符号 → 可锚定并 patch；仅诊断，非生产 |

**未新激活（public/exported 或被 env+permit 双重限制）：** `___assert_rtn`/
`__assert_rtn`（8168）、`_abort_with_payload`（8191）、AGC
`verifyLoweredIR`（8294）、`AGCLLVMCtx::compile`（8346）、IOKIT
`IOHIDEventSystem*`（8530/8661/8734/8742）、XPC
`_xpc_connection_create*`（24832/24835）。其中：

- `macws_install_iohid_unserialize_bypass()` 在 `11986` **无条件调用**，
  目标是 public `IOHIDEventSystemClientSetMatchingMultiple → no-op(1)`。
  当前无 permit 被拒绝，但日志仍打印 “installed”；若有人扩大 permit，
  它会静默抑制 HID matching。**必须保持未 permit**。
- `MACWS_HID_BYPASS` bulk no-op（`12032`）、`MACWS_AGC_VERIFY_BYPASS`
  （`12010`）、`MACWS_AGC_FASTMATH_HOOK`（`12002`）、
  `MACWS_KEEP_ASSERT_BYPASS`（`8155`）、`MACWS_KEEP_RENDER_UPDATE_CBZ`
  （`6150`）均为诊断/绕过；生产 preflight 的 diagnostic pattern 覆盖它们
  （`macws_diagnostic_flags.sh:105`），且当前无 permit。禁止为它们扩 permit。

## 5. Provider 打包/装载路径与双架构

- **FACT（预修复运行）：** 实际加载的 provider 是
  `/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`
  （`provider-probe-r003-final.json` 的 `provider-image=`）。
- **FACT（本地 host build）：** 当前 `libmachook/.theos/obj/libmachook.dylib`
  的 `LC_LOAD_DYLIB` 为 `/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate`。
  部署产物可能被 postinst/Theos 改写为 `/System/Library/...`；**下一次部署
  必须用 `otool -l` 对实际部署的 libmachook/arm64 和 arm64e slice 核对
  load command，并确认该路径在 chroot 中解析到 provider**。postinst 当前
  签名/信任的路径是 `/System/Library/Frameworks/...` 和
  `/var/jb/Library/Frameworks/...`（`layout/usr/macOS/bin/postinst.sh:1292-1300,1632-1633`）。
- `misc/cydiasubstrate_macos.m` **不在** Theos `SUBPROJECTS`/Makefile 中；
  它是手工构建的 provider。下一次包必须显式构建 arm64+arm64e 两个 slice，
  对每个 slice 运行 `misc/set_macos_version.py`（macOS 13.0 platform），
  `ldid` 签名并加入 trustcache；安装到 load command 指向的路径（以及现有
  postinst 期望的路径），使用 fresh inode。不能只替换源码不替换 provider。
- Provider 无构造函数；作为直接 dylib 依赖先于 libmachook 初始化加载，
  `MSHookMessageEx/MSHookFunction/MSFindSymbol/MACWSHookPermitV2` 可用。
  `MSHookFunction` permit 表静态清零，由 SkyLight load 回调用
  `MACWSHookPermitV2` 注册；方法 hook 不依赖 permit。
- **双架构：** provider 的 `MSHookFunction` thunk/`ptrauth` 与 LINKEDIT
  解析都必须在 arm64、arm64e 两个 thin slice 上各跑一次 disposable 控制；
  host 只为 arm64/arm64e 编译对象，不等于设备切片验收。

## 6. 最小有序验证计划（combined provider+F1/R008）

**Phase 0 — 冻结与身份（单次构建）**
1. 冻结源快照（`3c4d581` + `4bcf20d` + R008；记录 HEAD 与所有源哈希）。
2. 构建 libmachook 两个 thin slice 与 provider 两个 thin slice（手工命令、
   `set_macos_version.py`、`ldid`、fixup/platform 检查）；只构建一次。
3. 记录 provider/libmachook 的 fat/thin SHA/UUID/架构、load command、
   deployed 路径、CDHash/trustcache；核对 provider 导出符号。
4. 全包安装/postinst exit 0；fresh inode；确认 `/Library` 与
   `/System/Library` provider 路径均解析。

**Phase 1 — disposable provider 控制（不启动生产 WS/tccd）**
5. `misc/macos_mshook_provider_probe.c` 在两个 slice 运行：permit 正/负例、
   原 IMP 可调用、未经许可目标字节不变、拒绝日志与退出码。
6. 增加一次性本地方法/局部符号 fixture（不进入生产）：验证
   `MSHookMessageEx` replacement 生效、原 IMP 可调用、父类不被改写；
   `MSFindSymbol` 能解析局部符号，但 `MSHookFunction` 无 permit 仍拒绝。
7. 对 `__xpc_bootstrap_services` 观察 `fn != 0` 与 `result`；确认无 XPC/launch
   回归或崩溃，否则在进入 WS 前回滚。

**Phase 2 — 实际角色与 hook 状态**
8. WS（owner-managed generation）：核对加载的 provider UUID/hash；检查
   `CGXSYNTH-INSTALL`（仅 diagnostic）；对每个 SkyLight 局部符号确认
   `REFUSED no permit` 且目标 16 字节未变；`StartCompositeForDisplayStream`
   不再 “symbol not found”，但不得安装；`PrepareForUse` 记录选中的符号
   地址（0x31ffc4 vs 0x3243ac），不得把 fallback 误判为成功。
9. pkd：确认 PKD entitlements adapter 安装；对已知插件比较原值与适配值；
   原 IMP 可调用；不改变 TCC。
10. lsd session：确认两个 `_LSDefaults` hook 安装、store URL 重定位、原 IMP
    可调用；若 `dispatch_once` 首次未命中，记录并仅在确有需求时做窄重试。
11. Steam：确认 AX preload/CreateSimpleProcess 适配器生效，Steam helper
    真实 spawn、无重复进程；不把日志当验收。
12. Metal/AGX：先核对精确 Metal UUID 中 `_getMetalPluginClassForService`
    的 local/export 属性；若新激活，验证 AGX device class、bootstrap 与像素。
13. 诊断 hook（MetalFX/AGX/AGC/HID/assert/render_update）只在显式诊断
    sentinel/env 且单独 generation 下验证；生产 preflight 必须拒绝这些变量。

**Phase 3 — 冷启动、像素、输入**
14. 正常停止/启动与 recovery：tccd/WS 新 generation 的就绪、TCC 请求路径、
    WS 崩溃后替换 PID。
15. 像素：真实新 final-composite 序列/画面（VNC/Host），不接受
    `state=ready`、finalize、status 4 或非空图像代替。
16. 输入：Terminal 内容点击/文本/箭头；Dock 图标、菜单栏、上下文菜单；
    标题栏 drag 与菜单 tracking；要求可见变化/新帧、无重复 mouseDown。
17. 两个 thin slice 分别记录 loaded provider/libmachook UUID/hash。

**Phase 4 — 回滚/验收**
18. 回滚边界：恢复上一 provider/libmachook/package、fresh inode、trust；
    停止/重载相关 job；清理 sentinel/marker、探针、debugger。
19. 分别报告 process readiness、populated TCC reply（若可得）、pixels、
    exact input、global input；不合并成单一“成功”。

## 7. 具体不安全激活/缺失门禁与窄建议（不实现）

1. **SkyLight `PrepareForUse` 目标选择错误（FACT）**：symbol-first 选
   `MetalIOSurfaceBacking`（0x31ffc4），22F82 实际失败路径是
   `MetalTiledBacking`（0x3243ac）。在添加任何 permit 前，改为精确
   `MetalTiledBacking` 符号或对 22F82 保留已验证 fallback；不要给错误地址
   加 permit。
2. **“installed” 日志在拒绝后仍打印（FACT）**：SkyLight/IOHID 等 callsite
   不检查 `*result`/目标字节。建议窄修：callsite 在打印 installed 前确认
   `*result != 原符号` 或读取 16 字节确认；这不是 permit 扩张。
3. **`__xpc_bootstrap_services` 无门禁直接调用（FACT/候选）**：预修复
   `SYMBOL NOT FOUND`，修复后可能在所有 libxpc 进程调用。建议在角色/结果
   验证后保留或加窄门禁；不得用 fake XPC 结果。
4. **Steam/Metal 私有符号直接调用（THEORY locality）**：若 local，
   新激活 AX preload/CreateSimpleProcess/AGX plugin redirect。建议先用精确
   UUID 的 symbol table 核实，再决定是否接受；不要用猜测 offset。
5. **IOHID 无条件 targeted no-op**（`11986`）：当前被 permit 拒绝；任何
   permit 扩张都会把 HID matching 静默关掉。保持未 permit，或把它纳入
   `MACWS_HID_BYPASS` 门禁。
6. **AGC verify/fastmath、assert bypass、render_update CBZ**：保持 env/
   sentinel 门禁且未 permit；不得作为 provider 合并的“顺带修复”。
7. **方法 hook 的 UUID/profile**：PKD/LSD 只有角色/selector 门禁，无 UUID
   pin。若需要更强约束，按角色加 UUID/类存在性检查；不要 blanket。
8. **permit 扩张禁止**：合法 SkyLight compositor hook 若必须启用，逐个
   UUID/offset/prologue 审计后单独 permit；不得一次性放开 70 个 callsite。

## 8. 有限下一步补丁范围

- **只做一次合并构建**：provider `3c4d581` + F1 `4bcf20d` + R008；不新增
  callsite、不复制 ElleKit/share、不扩大 permit。
- 若 Phase 1/2 证明需要：仅做以下窄源修改之一，按顺序：
  a. SkyLight `PrepareForUse` 目标选择修正；
  b. callsite 的 installed/refusal 验证（诊断正确性）；
  c. `__xpc_bootstrap_services`/Steam/Metal 私有调用的角色/身份门禁。
- 先验证再改；每次只一个变量，保留回滚点。

## 9. 命令与退出码

| 命令 | 退出 | 结果 |
| --- | --- | ---: |
| `grep -rn "MSHookMessageEx(" libmachook …` | 0 | 7 个 callsite |
| `grep -rn "MSFindSymbol(" libmachook …` | 0 | 26 个 callsite |
| `grep -rn "permitFn(" libmachook/mac_hooks.m` | 0 | 1 个 permit 注册（CGX） |
| `nm -a analysis/SkyLight_22F82 | grep <符号>` | 0 | 9 个目标均为 `t` 局部符号，地址见 §4 |
| `nm -a libxpc.dylib | grep xpc_bootstrap_services` | 0 | `t` @ `0x18017bb84`（15.6，仅版本相邻） |
| `python3` 读取预修复 artifact 标记 | 0 | 见 §2 |
| `sha256sum <§1 文件>` | 0 | 见 §1 |
| `git log --oneline -1`（只读） | 0 | HEAD `7e5d771` |

未运行测试、构建、package、设备命令；未执行 commit/reset/stash/push。

## 10. 限制

- **FACT** 限于源码、本地 22F82 SkyLight/15.6 libxpc 与本地预修复日志；
  Ventura 22F82 的 Metal/HIServices/libtier0/libxpc 精确二进制未本地，
  相关 locality/activation 为 THEORY，需在部署前核对 UUID/symbol table。
- 预修复日志来自 probe/WS 的本地 artifact，不是本轮新设备运行。
- 未验证方法 hook 的运行时类实现/时序；LSD `dispatch_once` 的首次命中风险
  需实际角色检查。
- 未扩展或验证权限策略、TCC、responsibility；本包不涉及。

(handover to codex now)
