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
