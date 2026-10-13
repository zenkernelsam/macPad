# 命中率探针结果（handover §3）— macOS 15.6.1

> 生成：2026-09-24 · 本机 `VirtualMac2,1` macOS **15.6.1 (24G90)**
> 目的：把"要重推多少条"变成数字 → 喂给 handover §6 决定点。
> **结论先行：函数级命中 ≈ 90%（>70%）→ 按 §6 判定"值得全量 port"。
> 字节签名@硬编码偏移 命中率 = 0/19（预期内，每次 build 偏移必变）；
> 12 个 UUID 闸门结构性全失效（预期内）。真正工作量 = 逐条重推偏移/UUID，
> 仅 1 处签名变更（`EndUpdateEb→Ebb`）需改 hook 代码语义。**

---

## 0. 方法与原始命令

工具链实际情况（与 handover §3 预估的差异）：

- `dyldex`（DyldExtractor）**无法解析 15.6.1 缓存**——`processSlideInfo` 报
  `Unknown slide info version`（新 slide-info 格式）， traceback 见下。13.2.1 正常。
- **改用 `ipsw-a2sb dyld extract <DSC> <dylib> -o <dir> --slide`**，两套缓存各抽 11 个镜像，21/22 成功。
- 抽取产物：`/tmp/dsc15/<Name>`、`/tmp/dsc13/<Name>`（thin arm64e，`__TEXT.fileoff=0`
  ⇒ **镜像内偏移 == 文件偏移**，可直接按源码里的 `header+offset` 校验）。
- ⚠️ 工具不对称：ipsw 给 **15.6.1** 抽取件重建了丰富符号表（含 non-external 本地符号）；
  **13.2.1** 抽取件 symtab 为空（dyldex 抽出的也一样）。故"符号存在性"列只在 15.6.1 侧有效；
  13.2.1 列只用于字节对照，符号缺失 ≠ 函数不存在。

```bash
DSC15=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e   # 2.7G
DSC13=~/Desktop/VirtualMacOniPad/VirtualMac/build/inputs/macos/22D68__MacOS/dyld_shared_cache_arm64e  # 1.6G
IPSW=~/Desktop/VirtualMacOniPad/VirtualMac/build/toolchain/bin/ipsw-a2sb
for nm in Metal QuartzCore IOKit IOGPU SkyLight AGXCompilerCore IOSurface \
          IOMobileFramebuffer HIToolbox CoreServicesInternal DesktopServicesPriv; do
  "$IPSW" dyld extract "$DSC15" "$nm" -o /tmp/dsc15 --slide
  "$IPSW" dyld extract "$DSC13" "$nm" -o /tmp/dsc13 --slide
done
/usr/bin/dwarfdump --uuid /tmp/dsc15/* /tmp/dsc13/* \
  /System/Library/Extensions/AGXMetal13_3.bundle/Contents/MacOS/AGXMetal13_3
```

dyldex 在 15.6.1 上的报错（证据存档）：

```
Extracting /System/Library/Frameworks/Metal.framework/Versions/A/Metal
  File ".../DyldExtractor/converter/slide_info.py", line 460, in processSlideInfo
    mappingInfo = _getMappingInfo(extractionCtx)
  File ".../DyldExtractor/converter/slide_info.py", line 313, in _getMappingInfo
    logger.error("Unknown slide info version: " + slideInfoVer)
TypeError: can only concatenate str (not "int") to str
```

签名来源：直接解析 `libmachook/{mac_hooks.m,Metal_hooks.x,AppInputBridge.m,Compatibility/*}`
里的 `uint32_t`/`uint8_t` 数组 + `memcmp` 调用点（脚本见附录 A），不是 handover §1.2 的
前-8-字节近似（该表把 `uint32` 词数组误记成字节数，实际签名 8–48B）。

---

## 1. UUID 闸门命中表（12 去重，§1.3 全量）

源码 UUID 全部推自 **macOS 13.4**（22Fxx）；对照基线是 **13.2.1**（22D68，不是同一 build，
所以 13.2.1 列也理应全 miss——验证了我们 DSC13 只能当"邻近版本参照"，不是补丁原点）。

| 目标镜像 | 源码期望 UUID (13.4) | 13.2.1 实测 | 15.6.1 实测 | 15.6.1 命中 |
|---|---|---|---|---|
| QuartzCore | `CF853BBD-01B6-3F46-ADA1-EC70FD2DC9DC` | `6FB199AB-500F-3CF4-84B4-08BC6BD22A79` | `31921699-8990-3ACE-8D83-16E7BE814C6F` | ❌ |
| Metal | `2BAB169C-42DA-36E3-955A-F30B709EC2AD` | `03DCB34F-F25E-35CE-B534-054C35444C69` | `F83EE1A6-49CC-3A46-80AF-1B8B07EE0322` | ❌ |
| IOSurface | `2B44B850-7D19-34F3-AB8E-A3B93016A96D` | `B027BE5D-20A9-3C38-9E24-63DBE4B55296` | `B681DAE8-0089-38F1-98E0-FAC680608E1F` | ❌ |
| IOGPU | `CE2B5551-857F-3EDD-9E4F-435215CC8C27` | `E0268D54-1276-3873-AB39-9637AF4F8936` | `68B70E83-BBDA-3411-A487-E52797174518` | ❌ |
| AGXMetal13_3 (磁盘) | `727C250E-554D-3921-A5B3-48DAE6195B79` | —（不在缓存） | `B303B4E8-5F17-39B8-8505-326AAF870F39` (arm64e) | ❌ |
| HIToolbox | `D800278B-4E6C-3032-B56F-027A938A51D6` | `C68FE2E2-E0F3-3CAF-ADA3-BCC72FBA79D0` | `1A037942-11E0-3FC8-AAD2-20B11E7AE1A4` | ❌ |
| CoreServicesInternal | `DC429505-F838-3D6D-9B03-5A826EFE86A4` | `39C7432A-4A2A-3053-9584-BA2F2E196027` | `059EC98D-B718-3028-B844-6CB1590B2E2A` | ❌ |
| DesktopServicesPriv | `C76AB28E-02A7-3D20-A294-9DABB94C4313` | `70D09D20-3332-377A-A448-6D409C2F7DAD` | `C01F3662-2FD3-3A0C-9F37-CFF1ABFA0ABF` | ❌ |
| Image.qlgenerator | `388DEE66-0DF5-3DDC-85E4-DF8F9C9B8332` | —（磁盘 bundle） | `B6E7BDE8-EFED-3F4B-9992-1CC96483DC8A` (arm64e) | ❌ |
| locationd | `DA334E85-02CE-306B-A7B3-7A9DB0966EA1` | —（可执行体，不在缓存） | 未查（可执行体不在缓存） | ➖ |
| Steam `gameoverlayrenderer` | `529F4E8F-0FFB-30F9-88EF-AE0918F4C325` | — | —（App 内文件） | ➖ |
| Stray (UE4 游戏) | `C72D3F73-25F4-333B-9108-83432E09E687` | — | —（App 内文件） | ➖ |
| Electron Framework | `4C4C4442-5555-3144-A1A8-564169F3FF00` | — | —（App 内文件） | ➖ |
| Sublime Text | `4C4C44C0-5555-3144-A108-4370E23442E0` | — | — | ➖ |
| Geekbench 6 | `49124C96-2DB4-319D-B083-3B90C2074777` | — | — | ➖ |
| Steam client / UI | `2F008D6B-…` / `1372EF86-…` | — | — | ➖ |

**系统镜像 9/9 全 miss——结构必然（UUID 每次编译都变），不代表移植受阻；
每个闸门改成"新 UUID 或去 UUID 化"即可。App 侧 UUID 与系统版本无关，按原状保留。**

其余系统镜像实测 UUID（台账备用）：SkyLight `4E052846-80C2-38AF-85BF-1482E070A32B`、
IOKit `1B056404-8B47-31A9-B1F3-ED0693EE9684`、IOMobileFramebuffer `6DE81CE6-C629-3194-AD6A-B0E8A02A3937`、
AGXCompilerCore `949844CF-95DC-382D-9978-CF4EC78EE805`。

---

## 2. 字节签名扫描（19 条系统镜像签名）

`@off` = 在源码硬编码偏移处逐字节相等（补丁原样装上才算 hit）；
`anywhere` = 签名串在整个文件里的出现次数（>1 多为通用序言，只说明"这种序言形状还在"）。

| # | 目标/函数 | 偏移 | 长度 | 13.2.1@off | 13-any | 15.6.1@off | 15-any |
|---|---|---|---|---|---|---|---|
| SL1 | SkyLight `CGXHideCursor` | `0x1322b0` | 20B | miss | 250 | miss | 509 |
| SL2 | SkyLight `EndCurrentComposite` 回退 | `0x14753c` | 48B | miss | 1 | miss | **0** |
| SL3 | SkyLight `EndUpdate` 回退 | `0x1470b0` | 48B | miss | 0 | miss | **0** |
| QC1 | QC `enable_frame_info_tag_list` | `0x29285c` | 16B | miss | 20 | miss | 12 |
| QC2 | QC `finish_skylight_update` | `0x291220` | 16B | miss | 1663 | miss | 1891 |
| QC3 | QC `begin_skylight_update` | `0x291288` | 16B | miss | 1663 | miss | 1891 |
| QC4 | QC `MetalContext::update_image` | `0x6f750` | 24B | miss | 6 | miss | 7 |
| IS1 | IOSurface protection getter | `0x3df8` | 8B | miss | 0 | miss | **0** |
| FB1 | IOMFB public `SwapEnd` wrapper | `0x11cc` | 16B | miss | 0 | miss | **0** |
| HI1 | HIToolbox `SetMenuBarObscured` | `0x467f4` | 16B | miss | 972 | miss | 895 |
| HI2 | HIToolbox `RecalcBar` | `0x11878` | 12B | miss | 672 | miss | 783 |
| HI3 | HIToolbox `GetAppObject` | `0x59778` | 16B | miss | 0 | miss | **0** |
| HI4 | HIToolbox `FrontUILost` | `0x4d608` | 16B | miss | 2757 | miss | 2210 |
| CR1 | Chromium overlay prologue (Electron) | `0x0ca1254` | 32B | — | — | — | — |
| CR2 | capture_adapter (Electron) | `+0x44` | 32B | — | — | — | — |
| AG1 | AGX `objc_msgSendSuper2` stub | static `0x1e5a5dfc0` | 16B | — | — | miss | **0**（slice 内） |
| EL1/EL2 | Electron thunk/ctor | `0x61d2398/0x61d2354` | 24/28B | — | — | — | — |
| ST1 | Steam overlay label 序列 | `0x154e0` | 24B | — | — | — | — |
| LD1 | locationd idle callsite | `0x46579c` | 20B | — | — | — | — |

**@off 命中率：0/19（两套缓存都 0）—— 预期内**：签名全部按 13.4 的地址排布推的，
13.2.1 都位移了（13.4≠13.2.1 也是两个 build）。**这不是移植难度信号**；
真正的信号是下表的"补丁点是否还存在"。

单指令校验（也随偏移全灭，但语义仍在）：

| 校验 | 偏移 | 13.4 期望 | 15.6.1 实测 |
|---|---|---|---|
| IOMFB `kern_SwapEnd+0x24` | `0x4424` | `mov w3,#0x468`（补成 `#0x46c`） | `mov w3,#0x514`（函数移到 `+0x5450`，**输入结构变成 0x514**，补丁语义待定） |
| IOMFB `kern_SwapEnd+0x30` BL | `0x4430` | `bl 0x94001f64` | `bl 0x940023a7`（结构同，目标址变） |
| Metal `dyld_get_active_platform` 返回点×5 | `0xedf14..0xee690` | 固定返回址 | 该函数仍被 Metal import；15.6.1 有 **15 个调用点**（见 §3.5） |

---

## 3. 补丁点存活检查（决定性指标）

对每条补丁问"15.6.1 里目标函数/机制还在吗"。`新偏移 = 符号址 − __TEXT.vmaddr`。

### 3.1 SkyLight — 10/11 存活（最高价值簇）

| 补丁点 | 15.6.1 | 新偏移 | 备注 |
|---|---|---|---|
| `CGXHideCursor` | ✅ `0x186616e3c` | `+0x167e3c` | 序言前 4 词与旧签名**完全相同**，仅第 5 词变（`fd430091`）→ 签名近似重用 |
| `MetalContext::StartCompositeForDisplayStream` | ✅ `0x1866373a4` | `+0x1883a4` | 存活 |
| `MetalContext::StartComposite(WSCD)` | ✅ `0x186636a10` | `+0x187a10` | 存活 |
| `MetalContext::StartComposite(MTLTex)` | ✅ `0x1866375e4` | `+0x1885e4` | 存活 |
| `MetalContext::EndCurrentComposite` | ✅ `0x186635c50` | `+0x186c50` | 存活（回退签名需重推） |
| `MetalContext::EndUpdate` | ⚠️ 改名 | — | 15.6.1 只有 `EndUpdateEbb`（**多一个 bool 参**，`0x1866357b8`/`+0x1867b8`）→ hook 原型+调用语义要适配 |
| `MetalIOSurfaceBacking::PrepareForUse` | ✅ `0x186898364` | `+0x3e9364` | 存活 |
| `deque<RenderState>::pop_back` | ✅ `0x1866341a0` | `+0x1851a0` | 存活 |
| `WSCompositeDestinationCreateWithMetalTexture` | ✅ `0x186639c10` | `+0x18ac10` | 存活 |
| `MetalContext::StopCapture` | ✅ `0x1866358cc` | `+0x1868cc` | 存活 |
| `WS::Globals`/`CGXSession` 静态布局 | ❓未查 | — | `0x1d8bcd460`/`+0x20`/`+0x108` 全局布局需 IDA 复核 |

15.6.1 新增同类（新合成器架构证据，语义影响待评估）：
`__ZN10Compositor9EndUpdateEb`、`__ZN15CompositorMetal9EndUpdateEb`、
`MetalTiledBacking::PrepareForUse`、`MetalIOAccelHybridBacking::PrepareForUse`、
`MetalIOAccelSurfaceBacking::PrepareForUse`。

### 3.2 QuartzCore — 4/4 存活

| 补丁点 | 15.6.1 符号址 | 新偏移 |
|---|---|---|
| `IOMFBServer::enable_frame_info_tag_list` | `0x1898813d8` | `+0x2c63d8` |
| `IOMFBServer::finish_skylight_update` | `0x18987f500` | `+0x2c4500` |
| `IOMFBServer::begin_skylight_update` | `0x18987f568` | `+0x2c4568` |
| `OGL::MetalContext::update_image` | `0x1896299cc` | `+0x6e9cc` |

资源文件：`Resources/default.metallib` **仍在**；另新增 `default.pipelinelib`（macOS 15 新物，意义待查）。

### 3.3 IOMobileFramebuffer — 2/2 存活

- `_IOMobileFramebufferSwapEnd`（导出）`+0x1750`：4 词 wrapper 里仅 `ldr x1,[x0,#0x728]`
  变成 `ldr x1,[x0,#0x880]`（`f9439401`→`f9447001`）→ 签名改一词即可。
- `_kern_SwapEnd` `+0x5450`：结构同（`mov w1,#5` sel、`mov w3,#0x514`、BL IOConnect 调用）。
  **inputStructCnt 13.4=0x468 → 15.6.1=0x514**，补丁目标值需重新语义推导（原补丁 0x468→0x46c 是 +4）。

### 3.4 IOSurface — 机制存活，ivar 变了

- `IOSurfaceGetProtectionOptions`/`IOSurfaceClientGetProtectionOptions`/`-[IOSurface protectionOptions]` 均在。
- `-[IOSurface protectionOptions]` @`+0x4844` = `ldr x0,[x0,#0x8]; b …`——**ivar 从 0xc8 挪到 0x8**，
  恰好命中源码里 `ivar_getOffset(impl) != 8` 的检查 → 语义大概率仍对，`clientGetter` 期望字节改 `f9400400`+尾跳。
- `ldr x0,[x0,#0xc8];ret` 这种字节串在 15.6.1 IOSurface 里 0 命中（旧检查必失效）。

### 3.5 Metal — 机制存活

- `dyld_get_active_platform` 仍被 Metal import → interpose 有效。
- 15.6.1 有 **15 个调用点**（返回偏移）：`0x03b30, 0x0ac34, 0x28db8, 0x8645c, 0xa5090,
  0xe671c, 0xe677c, 0xf22a0, 0x138908, 0x138920, 0x138a88, 0x139088, 0x1390c0,
  0x13d8ec (MTLGetCompilerOptions+0x164), 0x13f528 (-[MTLCompiler …]+0x138)`。
  13.4 只白名单 5 个"source builder"点 → **需在 IDA 里逐个确认哪几个是 source-builder 路径**。
- 其余 Metal 补丁大量靠 swizzle/ObjC 层（不受字节偏移影响）。

### 3.6 HIToolbox — 4/4 存活（本地符号在缓存符号视图里都有）

| 补丁点 | 15.6.1 符号 | 备注 |
|---|---|---|
| `SetMenuBarObscured` | `_SetMenuBarObscured` `0x18c1a3b8c`（private external） | 存活 |
| `RecalcBar` | `_RecalcBar` `0x18c0333c0` | 存活（`RecalcBarIfRoot` 也在） |
| `HIApplication::GetAppObject` | `__ZN13HIApplication12GetAppObjectEv` `0x18bf5431c` | 存活 |
| `HIApplication::FrontUILost` | `__ZN13HIApplication11FrontUILostEv` `0x18bf56168` | 存活 |

### 3.7 IOKit — 导出全在

`_IOHIDEventSystemClientCreate` `@+0x14780`、`_IOHIDEventSystemClientSetMatching` `@+0x9870`
等 15 个 IOHIDEventSystem* API 全是导出符号 → 与版本无关的符号 hook，**0 重推成本**。

### 3.8 AGXMetal13_3（磁盘 bundle，15.6.1）

- bundle 仍在 `/System/Library/Extensions/AGXMetal13_3.bundle`；arm64e UUID `B303B4E8-…`。
- `objc_msgSendSuper2` stub 机制同款：arm64e slice 里有 **1163 处 `braa x16,x17` stub**
  （`adrp x17; add x17,#off; ldr x16,[x17]; braa x16,x17` 表），旧签名那一条（page/imm 特定值）
  已不存在 → **要在 IDA 里按 GOT 槽位反查属于 msgSendSuper2 的那条**。
- `MacWSAGXNoCopyABIReady` 的 `agx_initializer +0x1f4bb4` 落在
  `AGX::FramebufferDriverConfig<G13B…>::C2(…)+0x224`；`iogpu_initializer +0x1c24` 落在
  `-[IOGPUMetalBuffer …]+0xd0`——**该检查疑似针对 iOS 16.3 侧镜像（build=="20D67"），
  属 iOS 侧第二条 port 轴，不在本次 A 类范围**（待 §5 核实）。

### 3.9 其它

- `CoreServicesInternal`：镜像在；`_FileCacheFinalize` 补丁是 `MACWS_FILECACHE_DIAG`
  env 诊断件，价值低。（dsc15 抽取件 symtab 为空，未做符号验证——不阻塞结论。）
- `DesktopServicesPriv`：镜像在；同为 env 诊断件。
- `Image.qlgenerator`：磁盘在，新 UUID `B6E7BDE8-…`，`GenerateThumbnailForURL` 类入口需重定位。
- App 特例（Stray/Steam/Electron/Sublime/Geekbench/locationd）：全部 UUID 闸门+偏移，
  与系统版本无关——按 §7.3 最低优先级处理。

---

## 4. 命中率判读（喂 §6）

| 口径 | 命中 | 判读 |
|---|---|---|
| UUID 闸门（12） | 0%（结构必然） | 不作数——每个 build 必变 |
| 字节签名@硬编码偏移（19） | **0%** | 不作数——13.2.1 同样 0%，偏移每 build 必漂 |
| **补丁点/目标函数存活（系统镜像 23 项）** | **≈21/23 ≈ 90%** | **有效信号：>70% ⇒ 值得全量 port** |
| 需语义适配（非机械重推） | 2 项 | `EndUpdateEb→Ebb` 签名变更；IOMFB `inputStructCnt 0x468→0x514` 新语义 |
| 需 IDA 反查（非纯偏移） | 3 项 | AGX super2-stub 槽位、Metal source-builder 5 站点、SkyLight `WS::Globals` 布局 |

**按 handover §6：落 ">70%" 桶 → 建议全量 port（预估 3–7 天）。**
工作形态 = 机械重推（新偏移/新 UUID/近似签名）为主，小量 IDA 定点反查；
图形栈"实现演进"风险局部化（新 Compositor/Backing 类、EndUpdate 多参化、
default.pipelinelib），不构成"补丁点消失"级别的重写。

## 5. 独立风险线（与命中率无关，§6 已列）

1. **15.6.1 rootfs 准备未验证**（密封系统卷 + Cryptex 布局与 13.x 不同）——开工前先做
   rootfs 可行性 spike，不要先投补丁重推。
2. **iOS 侧第二 port 轴**：AGENTS.md 写 "hardcoded for iOS 16.5"，README/宿主实测
   iPadOS **16.3**。`MacWSAGXNoCopyABIReady` 的 agx/iogpu UUID+偏移疑似推自 iOS 镜像——
   **下一步需确认 iOS 侧签名按 16.3 还是 16.5 推的**（不查清楚可能 A 类白干）。
3. **项目当前真正 blocker 与 macOS 版本无关**：AGX 内核 UC 对 sel=0x9/queue-create 返回
   `0xe00002c2`（AGENTS.md 结构性 blocker #1/#2）——15.6.1 port 不会自动解它；
   反过来 15.6.1 的 Metal 可能走不同的 IOC 调用面，blocker 暴露面会变。

## 6. 下一步（按 §4 顺序）

1. ✅ 本表（步骤 0 完成）
2. 回填 `docs/porting/patch-ledger.tsv`（已随本表产出骨架，含 15.6.1 新偏移）
3. 进 MVB 前先 spike：15.6.1 rootfs 可否挂起 + iOS 侧签名版本轴确认
4. IDA 定点：AGX super2-stub、Metal 5 站点、SkyLight `WS::Globals` 布局、
   `EndUpdateEbb` 语义差分（13.4 vs 15.6.1 反编译对照）

---

## 附录 A：签名提取口径

- 全部签名以 `uint32_t[]`（ARM64 指令字）或 `uint8_t[]` 存于源码；扫描时按
  little-endian 展开成字节串。提取脚本解析 `libmachook/**` 中
  `(const|static) (uint32_t|uint8_t|uintptr_t) name[] = {…}`。
- `memcmp(record+0xd8/0x1f8/0x4e8, …)`、`memcmp(commands+0x1dc/0x1e0, …)` 等
  ~30 处是对**运行时 XPC 命令记录**的 12B 哨兵比对（`ff*12` 或 `01000000 ff*8`），
  不是镜像字节 → 不参与命中率，归"协议常量"（协议若变另行 RE）。
- `Metal_hooks.x:7073`/`mac_hooks.m:8751/12343/13637` 等为运行时数据比较，同上不计。

## 附录 B：复核入口

- 抽取件：`/tmp/dsc15/*`、`/tmp/dsc13/*`（本会话临时目录，重启后需重抽）
- 符号→偏移换算：`new_off = symaddr − __TEXT.vmaddr`（`otool -l` 取 vmaddr，`nm -m` 取符号）
- `dyld_get_active_platform` 调用点：`otool -tV /tmp/dsc15/Metal | grep dyld_get_active_platform`
