# STATIC —— 何时走「真缓存版 libSystem」vs「磁盘 shim」（不碰设备的静态分析）

> 归属：本条线与「补 shim」并行分工中的 **静态分析侧**。不涉及任何设备操作。
> 证据：IDA `ida-pro-mcp-Instance1`（`dyld_15.6.1_arm64e_thin`，imagebase 0）。
> 相关：`docs/porting/HANDOVER-REPLY-2026-09-28-populate.md`（设备侧交付）、`dyld-15.6.1-state.md`。

## 1. 决定「用缓存还是用磁盘 shim」的代码链（已定位）

`dyld4::Loader::getLoader(...)..._block_invoke` @ **0x1f788** 里两条互斥路径：

```
【用缓存】
0x1fd84  BL  dyld4::ProcessConfig::DyldCache::isProtectedLibSystemPath(path)   ; 0xcb88
0x1fd88  TBZ W0,#0,loc_1FFDC        ; ==0 → 走磁盘覆盖
0x1fd8c  MOV W8,#0x4E (78)          ; errno 78 = "cannot override a protected system dylib"
0x1fd90  STR W8,[SP,…]
0x1fd94  TBZ W23,#0,loc_1FE2C
         → LABEL_72 → dyld4::Loader::makeDyldCacheLoader(...)   ★ 用缓存

【用磁盘 shim（覆盖缓存）】
loc_1FFE0 / LABEL_118 → v49=1 → dyld4::Loader::makeDiskLoader(..., override=1, ...)
         日志：'found: dylib-from-disk-to-override-cache'
```

**守卫标志**（在调用 `isProtectedLibSystemPath` 之前就判）：
```
if ( (*(ProcessConfig+298) & 1) == 0  &&  isProtectedLibSystemPath(path) )  → 用缓存
else                                                                        → 用磁盘
```
⇒ ✅ **唯一开关 = `ProcessConfig+298`**（字节布尔）。语义 =「**允许磁盘覆盖 dyld 缓存**」类。

> ⚠️ 实测旁证（设备侧，非本条线）：同一台设备上两种结果都出现过——有时日志是
> `dylib-from-disk-to-override-cache`（磁盘赢），有时 `libSystem=<D161E41A>`（缓存赢）。
> ⇒ 该标志/路径选择**不是常量**，值得继续静态定位其 setter。

## 2. `isProtectedLibSystemPath` @ 0xcb88 = 对 3 项固定表做 strcmp

```
table = protectedPaths @ 0x9c638（3 个 const char*）
[0] "/usr/lib/libSystem.B.dylib"                 ← 我们的目标路径 **在表内** ✓
[1] "/usr/lib/system/libsystem_secinit.dylib"
[2] "/usr/lib/system/libsystem_sandbox.dylib"
```
⇒ 逻辑上 **`/usr/lib/libSystem.B.dylib` 本就该走缓存**；被拒只可能是因为 `ProcessConfig+298 == 1`。

## 3. 结论（对本项目两条并行线的影响）

| | 「补 shim」线（隔壁） | 「走真缓存 libSystem」线（我） |
|---|---|---|
| 前提 | shim 在位 + 签名有效（铁律；否则 536 对**任何**缓存都 EINVAL） | **同样**需要 shim 在位（铁律）+ 536 通 |
| 分叉点 | 让 dyld 用**磁盘**库 ⇒ 需要把程序 import 的符号补全 | 让 dyld 用**缓存**库 ⇒ 需要 `ProcessConfig+298 == 0` |
| 判据 | `cat/sh` 在 shim 生效下 rc=0 | `DYLD_PRINT_LIBRARIES` 显示 libSystem=`<D161E41A>`（缓存）而非 `<B90391D8>`（shim） |

⇒ **两条线共享同一套基础**（shim 在位 + cachereg + 536 通），只在 libSystem **来源**上分叉；
⇒ 因此**可以并行做研究**（离线），但**设备操作要串行**（同一天先例：`.OFF` 遗留坑 + region 污染）。

## 4. 下一步静态靶子（不需要设备）
1. **找 `ProcessConfig+298` 的 setter**：在 `dyld4::ProcessConfig::ProcessConfig`(0x9358) /
   `DyldCache`(0xbc9c) / `Security`(0xb1a4) 及其 block 里搜对 `+0x298` 的 `STRB`；
   若确为 `internalInstall`/`allowOverrides` 类，则看它由什么决定（AMFI/sysctl/`DYLD_*` env）。
2. **另一条覆盖路径**（`loc_1FE08 → fileExists → 0x1fe24/0x1fe28`）的上游是否也受同一标志影响（上次实测我们走的正是这条）。
3. 若找到 env/全局开关：给出「**不补 shim 也能让缓存赢**」的最小用户态配方（优先 env，其次单点补丁）。

## 5. 已用到的地址速查
| 符号/点 | 地址 |
|---|---|
| `Loader::getLoader` block_invoke | 0x1f788 |
| `isProtectedLibSystemPath` | 0xcb88 |
| `protectedPaths` 表 | 0x9c638（3 项） |
| `DyldCache` ctor / block_invoke | 0xbc9c / 0xc420 |
| `ProcessConfig` ctor / Process ctor | 0x9358 / 0xa460 |
| `Security` ctor | 0xb1a4 |
| 磁盘覆盖日志点（`makeDiskLoader(override=1)`） | LABEL_118 @0x200cc 起，日志串 `aFoundDylibFrom_0` |
| 缓存日志点（`makeDyldCacheLoader`） | LABEL_72 @0x1fd98 起 |

## 6. 本轮静态推进的**负结果**（别再重复扫）
- 在 91 个名字含 `ProcessConfig`/`Security` 的函数体内逐指令搜 `#0x298`/`#664`：**0 命中** ⇒ `ProcessConfig+298` 的写入**不在这些函数**里（可能：① 内联进 `RuntimeState`/`Process` 构造；② 通过寄存器基址间接写；③ 属于某个子对象，`RuntimeState+8` 指向的其实不是 ProcessConfig 首址）。
- 按 `internalinstall` / `allow*over*` / `protection` 关键词搜符号名：只命中 `PathOverrides` 一族，**没有**直接命名的 flag ⇒ 该布尔无独立符号名。
- ⚠️ 也试过 Python 字节模式扫 `STRB/STR #0x298`：模式/对齐假设不可靠，**0 命中**（**结论：本类问题必须走 IDA，别用 Python 字节扫**——与项目铁律一致）。
**下一步建议（静态）**：① 在 IDA 里对 `0x1f788` 那段引用反推：谁在 `RuntimeState` 构造后写过该字节（可对 ProcessConfig 对象做 xref 扫"写入 +0x298 的所有指令"）；② 或先找**其它读取者**（同一 block 里 +272/+304/+305/+312/+291 都是相邻布尔，其中某些已有日志/名字，可用来**反推字段语义**）。

## 7. 本轮静态推进 ①：守卫属 **ProcessConfig**（已定），并拿到 block 的捕获来源

block 字面量在 `dyld4::Loader::getLoader(...)` @ **0x1f018**（字面量 @0x1f368，descriptor `__block_descriptor_tmp.43`@0x9d760），
其 `invoke` 就是我们分析的 0x1f788。关键捕获：
```
v8 = *(RuntimeState + 8)                       ; = ProcessConfig*  ← 由 *(v8+352)=DyldCache、*(v8+312)=logging 反证
v9 = (ProcessConfig+352 != 0) ? (*(ProcessConfig+520) ^ 1) : 0     ; → 写入 block+80 (v56)
v32 = ProcessConfig::DyldCache::indexOfPath(cache, path, &idx)     ; → 写入 block+81 与 block+82 (v57/v58)
```
⇒ ① 守卫 `*((ProcessConfig*)+298)` 的基址**确认是 ProcessConfig** ✓
（此前 `ADD Xn, #0x298` 的两个命中属 `RuntimeState::notifyObjCPatching` / `setObjCNotifiers`，是**另一个结构**，已排除 ✗）。

⇒ ② **`ProcessConfig+520` 是个新线索**：它决定 block+80（= `v56`），而 block+80 正是 `loc_1FE08` 那条"磁盘覆盖"路径的分支条件之一。
⇒ ③ `block+81/82 = indexOfPath(...)`（**该路径是否在缓存里**）；结合 0x1fd94 的 `TBZ W23` 可见"在缓存里"是走缓存分支的必要条件之一。

**下一批静态靶子（更聚焦）**
1. `ProcessConfig+298` 与 `+520` 的**写入者**（两者都属 ProcessConfig；建议在 IDA 里对这两个偏移做"写指令"定位，
   例如扫 `STRB/STR` 的 `op_any` 不可靠（位移不参与匹配），改扫 `ADD/ADDU` 型基址计算或对 ProcessConfig 对象做数据流追踪）。
2. 把 `+272`(isOSBinary ✓ 已由 `loadableIntoProcess` 调用点反证)、`+289`、`+291`、`+304/+305/+312`、`+520` 一起列出，
   **用相邻字段反推语义**（其中多个已被日志使用，可作锚点）。

## 8. ★ 静态链条闭合：为什么"有时缓存赢、有时磁盘赢"—— 上游是 **DYLD_* path-override 环境变量**

（更正 §6/§7 的偏移口径：**Hex-Rays 的常量是十进制**；本节的 0x12A/0x208 才是真实文件偏移。）

### 完整因果链（全部有地址证据）
```
【写者】ProcessConfig::ProcessConfig @0x9358 内：
  0x9414  BL  dyld4::ProcessConfig::PathOverrides::dontUsePrebuiltForApp()
  0x9418  CBZ W0, loc_9428
  0x941c  MOV W8, #1
  0x9420  STRB W8, [X19,#0x208]        ; ★ ProcessConfig+0x208 = dontUsePrebuiltForApp()
  0x9424  STRB W8, [X19,#0x230]

【读者】Loader::getLoader @0x1f018：
  0x1f07c  LDR X8,[X2+8]                        ; ProcessConfig*
  0x1f080  LDR [X8+0x160]                       ; DyldCache 存在？
  0x1f088  LDRB W9,[X8,#0x208]                  ; ← 读上面那个 flag
  v9 = (DyldCache!=0) ? (*(+0x208) ^ 1) : 0     ; → 写入 block+80 (与 block+81/82=indexOfPath 一起)

【判定】block_invoke @0x1f788：
  0x1fd78  LDRB W8,[X8,#0x12A]                  ; 另一守卫（ProcessConfig+0x12A）
  0x1fd84  BL  isProtectedLibSystemPath(path)   ; 0xcb88；protectedPaths@0x9c638
  ⇒ block+80==1 且 +0x12A==0 且 路径∈保护表 ⇒ makeDyldCacheLoader    ★ 用【缓存】
  ⇒ 否则                                     ⇒ makeDiskLoader(override=1) ★ 用【磁盘 shim】

【上游】PathOverrides::dontUsePrebuiltForApp @0x950c
  return a1[0]||a1[1]||a1[4]||a1[5]||a1[10]||a1[11]||a1[12]||a1[13]||a1[6]||a1[7]!=0
  ⇒ 任一 PathOverrides 字段非空 = 存在 DYLD_* path-override 类环境变量
```

### 🔑 可检验的预测（用户态、零补丁）
| 运行方式 | DYLD_* 覆盖 | 预期 libSystem 来源 |
|---|---|---|
| **直接 `chroot`（仅带 `DYLD_SHARED_CACHE_DIR`）** | 无 | **缓存 `<D161E41A>`** ✓（今天验收即此 ✓） |
| `launchdchrootexec`（注入 libmachook ⇒ `DYLD_INSERT_LIBRARIES`） | 有 | **磁盘 shim `<B90391D8>`**（⇒ 需按 import 清单补符号） |
| 直接 `chroot` + 显式 `DYLD_INSERT_LIBRARIES=...` | 有 | 应变回磁盘 shim（**A/B 判定实验**） |

⇒ ① `DYLD_SHARED_CACHE_DIR` **不是** path-override（由 `DyldCache` 解析）⇒ **可保留**；
② `DYLD_INSERT_LIBRARIES` / `DYLD_LIBRARY_PATH` / `DYLD_FRAMEWORK_PATH` / `DYLD_FALLBACK_*` 等**会**触发"磁盘覆盖"。

### 与两条并行线的关系（结论）
- **补 shim 线**（隔壁）：他们的运行方式（`launchdchrootexec`，为注入 libmachook 必须带 `DYLD_INSERT_LIBRARIES`）**天然**走磁盘覆盖 ⇒ 补 shim 是**该路径下**的正确做法 ✓
- **走缓存线**（我）：只要**不引入** path-override env（仅 `DYLD_SHARED_CACHE_DIR`）⇒ libSystem 走缓存 ✓；若将来要"注入 + 缓存"兼得，则需处理 `dontUsePrebuiltForApp` 触发的 `+0x208`（或 `+0x12A`）——**这是下一个静态靶子** ✓

### 遗留静态项
1. `ProcessConfig+0x12A` 的**写入者**（全 `.text` 无 `STRB [Xn,#0x12A]`；疑经 `ADD Xn,…,#0x12A` 或由子对象/内联路径写入）——它是"保护分支"的第二个闸门。
2. 若要"注入 libmachook 同时让缓存赢"：最干净的入口是 `PathOverrides` 的非空判定（`dontUsePrebuiltForApp` @0x950c，单点、可读性高），而不是去动 `isProtectedLibSystemPath`。

## 9. ★ 收口：`ProcessConfig` 结构布局 + 两个闸门的**确切来源**

`ProcessConfig::ProcessConfig` @0x9358 里逐个构造子对象，布局（由调用点的 `ADD X0,X19,#imm` 直接读出）：
```
Process      @ +0x010
Security     @ +0x110     ← ★ 闸门 2 在此子对象内
Logging      @ +0x130
DyldCache    @ +0x160     ← ★ 闸门 1 在此子对象内
PathOverrides@ +0x240     ← dontUsePrebuiltForApp() 的 this
```
⇒ 两个闸门换算：
| 闸门 | 真实地址 | 子对象内偏移 | 写入者 / 来源 |
|---|---|---|---|
| **① 会话开关** | ProcessConfig**+0x208** | **DyldCache+0xA8** | `ProcessConfig` ctor **0x9420** ← `PathOverrides::dontUsePrebuiltForApp()`(0x950c) ← **任一 DYLD_* path-override env 存在即 true** |
| **② 安全闸** | ProcessConfig**+0x12A** | **Security+0x1A** | `Security` ctor **0xB2BC**：`UBFX W8,W0,#9,#1` ← **AMFI 信息字的 bit9**（位域由 `Security::Security` 0xb1a4 从 `Security::getAMFI()`@0xb378 解出，同时拆出 +0x11/12/16/17/18/19 等布尔） |

### 可操作性（本条的最终结论）
- **闸门 ① 完全由用户态决定** ✓ ⇒ **这就是"让缓存赢"的最小杠杆**：不引入任何 `DYLD_LIBRARY_PATH / FRAMEWORK_PATH / FALLBACK_* / INSERT_LIBRARIES` 等 path-override env（`DYLD_SHARED_CACHE_DIR` 不算 ✓，它在 DyldCache 里解析 ✓）。
- **闸门 ② 由 AMFI 位决定** ✗（非用户态）—— 但**今天验收已实证它为 0**（直接 `chroot` 下 libSystem 来自缓存 `<D161E41A>` ✓）⇒ 至少在 chroot 场景下不构成障碍 ✓。
- ⇒ 两条并行线的**分界线正式确定**：
  - 隔壁（`launchdchrootexec`，必须带 `DYLD_INSERT_LIBRARIES` 注入 libmachook）⇒ 闸门①=1 ⇒ **磁盘 shim 必被选用** ⇒ 补 shim 是唯一解 ✓
  - 我（直接 `chroot`，仅 `DYLD_SHARED_CACHE_DIR`）⇒ 闸门①=0 ⇒ **缓存赢** ✓
  - 若将来要"注入 libmachook **且** 用缓存 libSystem"：单点入口是 `PathOverrides`（0x950c 的判定或 0x9420 的写入），**不要**动 `isProtectedLibSystemPath` ✓

### 遗留（供后续）
- 闸门② 的 AMFI bit9 具体是哪一个策略位（可对照 `Security::getAMFI`@0xb378 的解包逻辑与 XNU 的 AMFI 返回位定义）；本设备实测为 0 ✓。
- 若要做"注入+缓存兼得"的最小补丁：优先改 `0x9420` 处写入（或 `0x950c` 的返回），把闸门①钉成 0。
