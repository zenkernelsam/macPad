# STATIC — syscall 536 全路径 EFAULT(14)/KERN_INVALID_ADDRESS 出口枚举 + dyld 入参逐字段 diff

> 归属：纯静态（源码级），**不碰设备**。源码：`analysis/xnu-xnu-8792.81.2`、`analysis/dyld-dyld-1286.10`。
> 对应问题：files_count=3（含 `fd==-1` 匿名条目）、mappings_count=16 时返回 **-14**（只交前 13 条时返回 0）。

## 0. TL;DR（先看结论）
1. **EFAULT ⟺ `KERN_INVALID_ADDRESS`** —— 唯一映射点在 `bsd/vm/vm_unix.c:2716-2718`（下面 (d)）。
2. 本题输入下，**只有两条路能产出 14**：
   - **匿名条目 `copyin` 失败** → `osfmk/vm/vm_shared_region.c:1611`（`fd==-1` 专属分支）
   - **匿名映射 VA 越界** → `osfmk/vm/vm_map.c:2717`（`vm_map_enter` FIXED 边界检查；同类块亦见 3605）
3. **结构性判断**：stock dyld 把**动态条目**的 VA 设为 `firstFileInfo.dynamicConfigAddress`（本例 = `0x2ac75c000`），
   而 region = `[0x180000000, +0x100000000)` ⇒ 相对偏移 `0x12c75c000` **> 4GB** ⇒ `end > effective_max_offset`
   ⇒ **KERN_INVALID_ADDRESS ⇒ EFAULT(14)**。这与"只交前 13 条（不含匿名/越界项）⇒ 0"完全一致。

## 1. 调用链与 EFAULT 出口（a）
```
syscall 536 → bsd/vm/vm_unix.c:_shared_region_map_and_slide            (2665)
            → shared_region_map_and_slide_setup                       (2189)
            → osfmk/vm/vm_shared_region.c:vm_shared_region_map_file   (1944)
                 ├── vm_shared_region_map_file_setup                  (1408)  ← 逐条映射的主循环
                 └── vm_shared_region_map_file_final                  (2108)
            → osfmk/vm/vm_map.c:vm_map_enter                          (2389)  ← 边界/对齐判定
```
**能返回 `KERN_INVALID_ADDRESS`（⇒ 14）的全部站点（已读上下文者标 ✓）**
| 文件:行 | 所属函数 | 触发条件 | 备注 |
|---|---|---|---|
| **vm_unix.c:2716** ✓ | `_shared_region_map_and_slide` | `kr==KERN_INVALID_ADDRESS ⇒ error=EFAULT` | ★ (d) 的**唯一**映射点 |
| **vm_shared_region.c:1024** ✓ | `vm_shared_region_start_address` (991) | `sr_first_mapping == -1`（region 空） | 属 **check_np** 路径，不在 map 路径 |
| **vm_shared_region.c:1611** ✓ | `vm_shared_region_map_file_setup` (1408) | **`fd==-1` 匿名分支**：`copyin(sms_file_offset, …, sms_size)` 返回 **EFAULT** | ★ **fc=3 专属**；指针不可读即命中 |
| **vm_shared_region.c:2615** ✓ | `vm_shared_region_slide_mapping` (2566) | `copyin(slide_info_addr, …)` 失败 | 走 slide_info 的 536 形态 |
| **vm_shared_region.c:2632** ✓ | 同上 | `memory_object_control_to_vm_object()` == NULL 或 `object->internal` | 同上 |
| **vm_map.c:2717** ✓ | `vm_map_enter` (2389, FIXED 分支) | `start < effective_min_offset` **或** `end(=start+size) > effective_max_offset` **或** `start >= end` | ★ **VA 越界**主站 |
| vm_map.c:3605 ✓(同型块) | 同文件相邻函数（FIXED 分支同样结构） | 同上 | 同一份"边界检查"代码块 |
| vm_map.c（其余）| 多个函数 | 未逐一读上下文（列举见 §5），其中与 536 相关的只有上表 FIXED 两处 | 待补 |
> 注：`vm_map_enter` 对**未对齐**（`start & mask`）返回的是 `KERN_NO_SPACE`（⇒ ENOMEM(12)），**不是** 14 ⇒ 二者可区分。

## 2. 哪些路径"只在 files_count=3 时激活"（b）
- **`fd==-1` 的条目专属分支** ✓：stock dyld 的 `files[numFiles] = { -1, 1, 0 }`。内核侧对 `fd==-1` 的 file：
  1. 先 **`copyin(sms_file_offset, …, sms_size)`**（vm_shared_region.c:1601）——**这是 14 的第一可能来源**；
  2. 再 `vm_map_enter` 映射匿名 object（1625 起）——**越界时是 14 的第二可能来源**。
  ⇒ file-backed 条目**不会**走 copyin（它们用自己的 fd ✓）⇒ 这解释了"fc=2（无匿名）⇒ 0；fc=3 ⇒ -14"。
- 次生路径：带 **slide_info** 的 536 形态（2615/2632）同样由 `fd==-1`/slide 相关参数激活 ✓。

## 3. region 大小 / submap 边界 与 offset>4GB（c）
- **边界判据看的是"VA − sr_base"，不是 file_offset** ✓：
  `vm_shared_region_map_file_setup` 里 `target_address = sms_address - sr_base_address`，随后
  `vm_map_enter(sr_map, &target_address, round_page(size), …, VM_FLAGS_FIXED, …)`。
- `vm_map_enter` 的 FIXED 分支：
  `if ((start & mask)!=0) RETURN(KERN_NO_SPACE);`
  `end = start+size; if (start<effective_min_offset || end>effective_max_offset || start>=end) RETURN(KERN_INVALID_ADDRESS);`
  ⇒ **越界 ⇒ 14**；未对齐 ⇒ 12。
- 本例 `target_address = 0x2ac75c000 - 0x180000000 = 0x12c75c000`（> `SHARED_REGION_SIZE_ARM64=0x100000000`）
  ⇒ `end` 远超 `effective_max_offset` ⇒ **KERN_INVALID_ADDRESS ⇒ EFAULT(14)** ✓（与实测一致）。
- **`sms_file_offset` 超过 4GB 本身不是问题**（它只在匿名分支被当作 userspace 指针做 `copyin` ✓），
  真正超界的是 **映射 VA** ✓ —— 这也解释了"看起来像 offset 超 4GB"的表象。

## 4. KERN_INVALID_ADDRESS → EFAULT 精确映射点（d）
`bsd/vm/vm_unix.c:2705-2724`：
```c
kr = vm_shared_region_map_file(shared_region, files_count, sr_file_mappings);
switch (kr) {
  case KERN_SUCCESS:            error = 0;      break;
  case KERN_INVALID_ADDRESS:    error = EFAULT; break;   // ★ 2716-2718
  case KERN_PROTECTION_FAILURE: error = EPERM;  break;
  case KERN_NO_SPACE:           error = ENOMEM; break;
  case KERN_FAILURE:
  case KERN_INVALID_ARGUMENT:
  default:                      error = EINVAL; break;
}
```
⇒ 想得到 14，**必须**让 `vm_shared_region_map_file()` 返回 `KERN_INVALID_ADDRESS`。

## 5. dyld 真实入参 vs 我们入参（逐字段 diff）
stock dyld-1286.10 `dyld/SharedCacheRuntime.cpp` `mapSplitCacheSystemWide()`（约 1285 起）：
```cpp
files[i].sf_fd            = infoArray[i].fd;
files[i].sf_mappings_count= infoArray[i].mappingsCount;
files[i].sf_slide         = (i == 0) ? (uint32_t)infoArray[0].maxSlide : 0;   // 只有 files[0] 带 maxSlide
files[numFiles]           = { -1, 1, 0 };                                     // 动态条目
mappings[totalMappings]   = { firstFileInfo.dynamicConfigAddress,             // VA
                              dynamicData->size(),
                              (mach_vm_offset_t)dynamicData,                  // file_offset = 堆指针
                              0, 0, VM_PROT_READ, VM_PROT_READ };
__shared_region_map_and_slide_2_np(numFiles + 1, files, totalMappings + 1, mappings);
```
| 字段 | stock（numFiles=2, totalMappings=15） | 我们的输入（实测） | 判定 |
|---|---|---|---|
| files_count | `numFiles+1 = 3` | 3 | ✅ 一致 |
| files[0..1] | `{fd,8,0}` `{fd2,7,0}` | 同 | ✅ |
| files[2] | `{-1, 1, 0}` | `{-1,1,0}` | ✅ 一致 |
| mappings_count | `15+1 = 16` | 16 | ✅ 一致 |
| 前 15 条 | main+.01 的段（VA 0x180000000…，prot 5/3/1，无 SLIDE） | 同 | ✅ |
| **第 16 条 (dyn)** VA | `dynamicConfigAddress`（本例 0x2ac75c000） | 同 | ⚠️ **越界**（region 顶 0x280000000） |
| **第 16 条 prots** | `VM_PROT_READ(1) / VM_PROT_READ(1)` | 观测 `max=1, **init=0x10000000**` | ❌ **不一致**（疑似被 patch/未初始化；0x10000000 非合法 VM_PROT 组合） |
| 第 16 条 file_offset | `(mach_vm_offset_t)dynamicData`（**userspace 堆指针**，供内核 `copyin`） | 观测为"合法用户指针" | ✅ 形态一致（但**必须保证该指针可读 `sms_size` 字节**） |
⇒ **实质差异只有两处**：① 第 16 条的 **init_prot**；② 第 16 条的 **VA 越界**（结构性，非参数错）。

## 6. 最小修复建议（供参考，均属 dyld 侧）
1. **把动态条目的 VA 挪进 4GB region 内**（改 `firstFileInfo.dynamicConfigAddress` 或整套动态区 VA），使其 `VA-sr_base+size ≤ 0x100000000`；否则 stock 形状在 macOS 15.6.1（缓存跨度≈4.77GB）下**必然 14**。
2. 或**去掉动态条目**（files_count=2、mappings_count=15，形状自洽）——但会丢失 dyld 需要的 dynamic config 传递（需另法）。
3. 同时把第 16 条的 `init_prot/max_prot` 恢复为 `VM_PROT_READ`（0x10000000 非法）。
4. 若第 16 条 `copyin` 失败（1611）：确认 `sms_file_offset` 指向的 `dynamicData` 在提交时**确实已提交可读**（同进程用户态 ✓），且长度 ≥ `sms_size`。

## 7. 逐函数体精确枚举（已闭合，替代原"未验证清单"）
函数边界（C 定义列 0 实测）与体内 KERN_INVALID_ADDRESS/NO_SPACE 计数：

| 函数 | 行区间 | KERN_INVALID_ADDRESS | 备注 |
|---|---|---|---|
| `vm_map_enter` | 2389–3449 | **仅 2717** ✓ | 其余 9 处均 `KERN_NO_SPACE`（⇒12）；**536 路径的越界唯一出口** |
| `vm_map_enter_fourk` | 3449–3977 | 仅 3605 | 4K 页变体，**不在** 536 路径（iOS 16K 页） |
| `vm_map_enter_mem_object_helper` | 3977–4854 | **0 处** | ★ 结论：**file-backed 映射不可能经此产出 EFAULT** |
| `vm_map_enter_mem_object` | 4854–4897 | 0 处 | 仅 43 行的薄包装 |
| `vm_shared_region_map_file_setup` | 1408–1926 | **仅 1611** ✓ | `fd==-1` 匿名 `copyin` 失败 |
| `vm_shared_region_map_file_final` | 2108–2340 | 0 处 | grep 无 ⇒ 非 14 来源 |
| `vm_shared_region_slide_mapping` | 2566–2740 | 2615 / 2632 ✓ | 走 slide_info 的 536 形态 |
| `vm_shared_region_start_address` | 991–1040 | 1024 ✓ | 属 check_np，非 map 路径 |

⇒ **因此本任务输入下 EFAULT(14) 只可能来自三处**：
① `vm_shared_region.c:1611`（`fd==-1` 条目 `copyin` 失败）；
② `vm_map.c:2717`（任意映射 `target_address/end` 越 submap 边界）；
③ `vm_shared_region.c:2615/2632`（带 slide_info 的形态）。
结合"只交前 13 条返回 0"，可判定：**我们的 14 由 ② 触发**（第 16 条 dyn 的 `VA−sr_base=0x12c75c000` 超 4GB），
除非第 16 条的 `sms_file_offset` 指向不可读内存（那会先由 ① 命中，且内核会打印
`for fd==-1 copyin() failed, errno=…` —— **设备侧可直接用这行日志区分 ①/②**）。
