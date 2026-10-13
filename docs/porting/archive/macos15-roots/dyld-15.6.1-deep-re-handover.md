# dyld 15.6.1 深度逆向交接：Super Handover + 启动 Prompt

> 读者：接手做**纯逆向分析**的 AI（不是你 Devin）。
> 本文件 = 任务说明 + 分析目标 + 已知事实 + 硬纪律。
> **产出物是一份 `dyld-15.6.1-full-analysis.md` 报告，不写代码、不改二进制。**

====PROMPT====

**任务：用 IDA Pro MCP 把 macOS 15.6.1 的 dyld 完整扒开——所有分支判断、
所有路径决策、所有参数构造，全部逆向成结构化文档。不改二进制，只产报告。**

## 0. 环境（先确认再动手）

- IDA Pro 已加载 `dyld_15.6.1_arm64e_thin`（arm64e 薄片，基址 0）。
- MCP server：`ida-pro-mcp-Instance1`，endpoint `http://127.0.0.1:13337/mcp`。
- 工具（只用这些）：`decompile`、`disasm`、`xrefs_to`、`list_funcs`、
  `func_query`、`find_regex`、`search_text`、`py_eval`（参数名 `code`）。

### 硬纪律（违反 = 白干）

1. **只用 IDA Pro MCP 分析。禁止用 Python/otool/strings/grep 读二进制字节。**
   用户明确要求："python 会错，浪费 token 和上下文。IDA Pro 把所有东西
   很有逻辑连在一起。" `py_eval` 里可以跑 ida python（idautils/idc）遍历
   函数引用/字符串/指令——那是 IDA 内部 API，不是外部 python 脚本。
2. **不 patch 二进制**——只反编译、画控制流、列决策点。patch 由我
   （Devin）拿着你的报告在另一会话里做。
3. 每个结论标注 `IDA <thin offset>`（例如 `0x3538c`），便于我映射到
   fat 偏移写补丁。
4. 遇到数据表（DSC header 字段、flags 常量）逐一列出偏移和语义。

## 1. 逆向对象清单（按序挖完）

### A. 入口与分发：`loadDyldCache` @ 0x34240

已知它是分发器：私有模式 → `mapSplitCachePrivate`；否则先试
`reuseExistingCache`，失败才 `mapSplitCacheSystemWide`。需要：

- **options 结构体的所有字段**（偏移 + 语义 + 谁写入）：`+4` 是 private
  flag、`+6` 是 verbose flag。还有哪些？`DYLD_SHARED_REGION=` 环境变量
  在哪解析、可取值有哪些？
- 私有 vs 系统路径的**全部判据**（哪些 env/flags/mode 开关驱动）。
- `security` 参数（调用约定里的第几个？哪些字段在 restricted 进程里
  被跳过？）。

### B. `reuseExistingCache` @ 0x351a8

- 完整伪码：检查什么 magic（`dyld_v1 arm64e`）、检查什么 UUID/路径
  （会不会校验缓存文件身份？——**如果它会校验 cache UUID 或路径前缀，
  我们也许可以让 iOS 缓存"看起来不对"**）。
- 返回值语义（什么时候返回 0/1）。
- 它如何拿到 existing cache 的 slid base（`shared_region_check_np` 的
  参数结构）。
- **关键：它调用 `dynamicRegion()`/`slide()` 时会不会解引用
  dynDataOffset 之后越界的地址？**（我们 SIGSEGV 的嫌疑点。）

### C. `mapSplitCacheSystemWide` @ 0x352bc — 主战场

这是 syscall #536 `__shared_region_map_and_slide_2_np` 的提交侧。
需要挖出：

- **`files[]` 数组的完整构造**：每个 entry 结构（fd、mappingCount、
  `shared_file_np` 里每个字段的语义）。最后一项 `fd=-1` 的
  `DynamicRegion` 伪映射的地址/大小从哪来。
- **`mappings[]` 数组的构造**：48 字节每条，字段：`address, size,
  file_offset, max_prot, init_prot`。prot 位的编码、`MAP_` 标志位怎么塞。
- 文件计数 `v62` / `v8`：读自 record+0x1A8 = `numSubCaches+1`（含
  main）。**减到 1 的影响**：`.01` 的 fd 没被提交时会发生什么？
  （它还会被 preflight 吗？`preflightCacheFile` 里 per-file 做了什么，
  哪些数据结构会留着 .01 的引用？）
- **`record+0x1B0` 的意义**：preflightCacheFile 尾部写入
  `dynDataOff+regionStart`，它是共享区尾部的 DynamicRegion VA。
  这个地址会被用到 syscall 参数里吗？具体是 `files[]` 最后一项的
  address 吗？（对比 `sms_init_size`/`sms_max_size` 等字段）
- **syscall 参数的完整布局**（`shared_region_mapping_np` 结构体，
  `x0..x6` 各寄存器内容）。特别是 `x2` = `files_count`、`x3` =
  `files[]`、`x4` = `slide`（slide 值从哪来）、`x5`/`x6`。
- **失败路径的全部分支**：syscall 返回 errno 后走向哪？
  （0x35754 error 出口；0x356f4 成功汇合；reuse 复检）

### D. `preflightMainCacheFile` @ 0x3576c + `preflightCacheFile` @ 0x35a98

- 主缓存文件的打开顺序、路径查找逻辑（`dyld_shared_cache_arm64e` +
  `.development`、cryptex 路径探测逻辑）。
- **DSC header 的完整字段枚举**（`mappingOffset@0x10`、
  `sharedRegionStart@0xe0`、`sharedRegionSize@0xe8`、
  `dynamicDataOffset@0x1f0`、`subCacheArrayOffset@0x188`、
  `subCacheArrayCount@0x18c`、`cacheType@0x68`…**全表**——ds_format
  的所有字段偏移，我目前只探到一部分）。
- subcache 数组每个 entry 的语义（extension、uuid、path），`.01`
  如何被关联打开。
- **per-file 的映射条目是怎么从 header 里 buildingMapping 出来的**。
- **header 里所有"起始地址/大小"字段在 iOS 共享区边界
  0x180000000..0x280000000 内 vs 外的判定**——哪个字段决定"这个缓存
  对 4GB 区是否合法"？

### E. `mapSplitCachePrivate` @ 0x342dc

- 完整循环：每条 mapping 的 `mmap` 参数（尤其 `MAP_FIXED`、prot、
  `flags=0x80012` 里的 `0x80000`）。
- `deallocateExistingSharedCache` 之前用 `check_np(0)` 解除共享区；
  实现细节。
- mmap 循环里**每个文件每条 mapping 的失败分支**（是继续还是 return）。

### F. 关键数据结构与全局变量

- `errno` 全局（`0xa9b10`）、console 打印函数、options blob。
- `DyldSharedCache::dynamicRegion()` @ 0x50dfc、`DynamicRegion::make`
  @ 0x35438、`getDyldCacheFileID`、进程 `builtFromDyldCache` 状态。
- `dyld4::KernelArgs` / `DyldProcessConfig` / `DyldRuntimeState` 里
  所有 shared-cache 相关字段。

## 2. 报告要求

写入 `docs/porting/dyld-15.6.1-full-analysis.md`，结构：

```markdown
# dyld 15.6.1 arm64e — complete RE analysis
## 1. `loadDyldCache` decision tree
   - 伪码 + 分支表（每个条件 → 走向）
## 2. syscall #536 参数完全构造
   - files[] entry 布局、mappings[] 48B 布局、寄存器对应表
## 3. DSC header 全字段表（15.6.1 版，全部偏移+语义）
## 4. 分支与标志位完整表（options+0/+4/+6、security、env vars）
## 5. 失败路径完整映射（每个 errno/abort/assert 的触发点）
## 6. 逆向结论：哪一步是 macOS 15.6.1 缓存对 iOS 16.3 内核
       不兼容的精确断点
## 7. 给 Devin 的 patch 候选清单（每条：thin 偏移、原指令、
        目标指令、语义依据、风险）
```

每节都要 `IDA <offset>` 标注证据位置。

## 3. 我已确认的 ground truth（不要重新验证，直接引用）

- `dyld3::loadDyldCache` @ 0x34240，`mapSplitCacheSystemWide` @ 0x352bc，
  `reuseExistingCache` @ 0x351a8，`preflightMainCacheFile` @ 0x3576c，
  `preflightCacheFile` @ 0x35a98，`DynamicRegion::make` @ 0x35438，
  `mapSplitCachePrivate` @ 0x342dc，`DyldSharedCache::dynamicRegion`
  @ 0x50dfc，crossarch_trap stub @ 0x76270。
- 代码洞（NOP 区）@ 0x38d08，56 字节（0x38d40 起是函数）。
- `_errno` 全局 @ 0xa9b10，`console` 函数已定位。
- DSC header 已确认字段：`sharedRegionStart@0xe0=0x180000000`、
  `sharedRegionSize@0xe8=0x12c760000`、`dynamicDataOffset@0x1f0=
  0x12c75c000`、`subCacheArrayOffset@0x188=0x333e8`、
  `subCacheArrayCount@0x18c=1`、`cacheType@0x68=0`、
  `mappingOffset@0x10`。
- iOS 16.3 内核共享区边界：`SHARED_REGION_BASE=0x180000000`、
  `SHARED_REGION_SIZE=0x100000000`（4GB，源 xnu-8792.81.2）。

## 4. 逆向关键问题（务必回答）

1. **`files_count` 减少后 `.01` 的命运**：`preflightCacheFile` 是否会
   仅按 record+0x1A8 遍历？`.01` 文件没进 files[] 时，dyld 会不会
   在别处（如 imagesCount、subcache uuid 查找）再引用它？
2. **动态区 fd=-1 映射的地址**：`record+0x1B0` 是不是提交给
   syscall 的那个 address？去掉它（patch `dynamicRegion()` 返 NULL）
   会不会导致 dyld 后续访问 `slidBase+dynDataOffset` 时 SIGSEGV？
   （我 patch 过、139 没变，需要确认地址的计算链条）
3. **iOS 缓存复用的绕过点**：`reuseExistingCache` 里哪个判断可以让
   它"不认"iOS 缓存？（magic strcmp？uuid？路径？）
4. **有没有合法的"小区域"模式**：`DYLD_SHARED_REGION`/`use_private`
   之外的官方路径让缓存不装进共享区？（例如 `dyld` 是否支持
   `DYLD_CACHE_DIRECTORY`/`DYLD_IN_CACHE`/`DYLD_PRINT_APIS` 模式把
   缓存当普通文件 mmap）
5. **`slide` 参数怎么取**：syscall 的 x4 是随机 slide 还是固定值？
   iOS 侧 slide 与 macOS slide 语义差异？

====PROMPT====

---

## 交接说明（给你看，不进 prompt）

### 为什么拆出去做

这个 dyld RE 任务已经消耗了大量上下文在试错上（patch→签字→设备测→
exit code 二值推断）。**根本问题是没人系统逆向 dyld 的决策树**——每次
猜测都靠"改一字节看退出码"，这正是 CLAUDE.md 里"evidence discipline"
批判的做法。把这个任务独立出去，让一个 AI 专心用 IDA Pro MCP 做
纯逆向（不改文件、只产报告），出来后我拿着报告精确 patch，效率会
高一个数量级。

### 为什么锁死 IDA-only

在 `AGENTS.md` 已立规矩："能用 IDA Pro 就用，尽量用 IDA Pro MCP
（py_eval）处理，python 会错，浪费 token 和上下文。"

**本任务只允许：**
- `decompile`（伪码，最重要）
- `disasm`（指令逐条）
- `xrefs_to` / `xrefs_from`（找调用者/被调者）
- `list_funcs` / `func_query` / `find_regex` / `search_text`
- `py_eval`（IDA 内部 API：`idautils`、`idc` 遍历）

**禁止：**
- `python3 -c "open(dyld,'rb')..."`（外部文件 IO）
- `otool -tV`（外部反汇编器——**本项目历史教训**：`otool` 对 fat 文件
  和 arm64e 切片的处理出过错，IDA 不会）
- `strings`（连设备都没有）
- 手算偏移写补丁

### 引用约定

所有地址用 **thin 偏移**（IDA 里 `idc.get_func_attr`/`decompile` 输出的
那个 `sub_XXXXX` 地址）。thin→fat 映射由我写补丁时按"当前 fat header
里 arm64e slice 的 offset"换算（在 `dyld-15.6.1-state.md` 里有规则）。
