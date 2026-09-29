# HANDOVER REPLY — 2026-09-29：536 EFAULT 枚举 + dyld 入参 diff（给接棒 AI 的提醒版）

> 用途：接棒 AI 上下文有限（~200K）且易在压缩后失忆。**每次压缩/重启后先读本文件**，再动手。
> 本文自包含：结论 + 方法学提醒 + 设备纪律 + 判定捷径。

## 0. 先看哪里（省时间，别重新试错）
| 顺序 | 文件 | 作用 |
|---|---|---|
| 1 | `docs/porting/STATIC-536-EFAULT-enumeration.md` | **本次任务全部答案**（(a)-(d) + dyld 逐字段 diff + 最小修复） |
| 2 | `docs/porting/STATIC-libsystem-cache-vs-shim.md` | libSystem 用【缓存】还是【磁盘 shim】的两个闸门（gate1 由 `DYLD_*` override env 决定） |
| 3 | `docs/porting/dyld-15.6.1-state.md` | 单一事实来源（顶部摘要 + 铁律 + 全部地址表） |
| 4 | `docs/porting/TOOLS-AND-PORTING.md` | 项目自带工具清单（sprobe/launchdchrootexec/libmachook/lldb 脚本/extract_dyld_cache.py…） |
| 5 | `docs/porting/HANDOVER-REPLY-2026-09-28-populate.md` | 536 主任务（EINVAL）的完整交付与血泪清单 |

**源码就在本地，优先读源码而不是硬啃 IDA**：
- `analysis/xnu-xnu-8792.81.2/`（XNU 全源码：`bsd/vm/vm_unix.c`、`osfmk/vm/vm_shared_region.c`、`osfmk/vm/vm_map.c`）
- `analysis/dyld-dyld-1286.10/`（dyld 源码：`dyld/SharedCacheRuntime.cpp` 等）
- IDA MCP 当前装载：**I1 = kc_raw_16.3_T8112（内核）**、**I2 = dyld_15.6.1_arm64e_thin**、**I3 = amfid_bin**
  （注意：装载位置会变，先 `server_health` 确认再引用地址）

## 1. 536 EFAULT(14) 全部答案（可直接引用）
**(d) 映射点**：`bsd/vm/vm_unix.c:2705-2724` ⇒ `case KERN_INVALID_ADDRESS: error = EFAULT;`（2716-2718）
（`KERN_NO_SPACE→ENOMEM`、`KERN_PROTECTION_FAILURE→EPERM`、default→EINVAL）

**(a) 536 路径上能返回 KERN_INVALID_ADDRESS 的全部站点（已逐函数体闭合）**
| 站点 | 函数（行区间） | 条件 |
|---|---|---|
| `vm_shared_region.c:1611` ★ | `vm_shared_region_map_file_setup`(1408–1926) | **`fd==-1` 条目的 `copyin(sms_file_offset,…,sms_size)` 返回 EFAULT** |
| `vm_map.c:2717` ★ | `vm_map_enter`(2389–3449) | `start<min_offset ∥ end(=start+size)>max_offset ∥ start>=end`（FIXED 越界） |
| `vm_shared_region.c:2615 / 2632` | `vm_shared_region_slide_mapping`(2566–2740) | slide_info 的 `copyin` 失败 / `object==NULL or internal` |
| `vm_shared_region.c:1024` | `vm_shared_region_start_address`(991–1040) | `sr_first_mapping==-1`（属 **check_np**，非 map 路径） |
| `vm_map.c:3605` | `vm_map_enter_fourk`(3449–3977) | 同型边界块，**4K 页变体，不在 iOS 536 路径** |
**关键计数结论**：`vm_map_enter` 内**只有 2717 一处** INVALID_ADDRESS（其余 9 处都是 NO_SPACE⇒12）；
`vm_map_enter_mem_object_helper`(3977–4854) **0 处** ⇒ **file-backed 映射不可能产出 14**。

**(b) 只在 files_count=3（含 `fd==-1`）时才激活**
`fd==-1` 条目专属：内核**先 copyin**（1601，14 来源①）**再 vm_map_enter**（1625+，越界时来源②）；
file-backed 条目不走 copyin ⇒ 完美解释「只交前 13 条 ⇒ 0；8+7+anon ⇒ -14」。

**(c) 边界判据**：看 **`VA − sr_base`**（`target_address`），不是 file_offset；
`vm_map_enter` FIXED：**越界 ⇒ KERN_INVALID_ADDRESS(14)**，**未对齐 ⇒ KERN_NO_SPACE(12)**。
本例 dyn：`0x2ac75c000 − 0x180000000 = 0x12c75c000 > 0x100000000(4GB)` ⇒ `end > effective_max_offset` ⇒ **14**。

## 2. dyld 逐字段 diff（`dyld-dyld-1286.10/dyld/SharedCacheRuntime.cpp` mapSplitCacheSystemWide）
stock：`files[i]={fd, mappingsCount, (i==0)?maxSlide:0}`、`files[numFiles]={-1,1,0}`、
`mappings[totalMappings]={dynamicConfigAddress, dynamicData->size(), (vm_offset)dynamicData, 0,0, VM_PROT_READ, VM_PROT_READ}`、调用 `__shared_region_map_and_slide_2_np(numFiles+1, files, totalMappings+1, mappings)`。
**逐字段比对后只有两处实质差异**：
1. 第 16 条（dyn）**VA 越界** ⚠️ —— macOS 15.6.1 缓存跨度≈4.77GB > 4GB ⇒ **stock 形状必然 14**（结构性）
2. 第 16 条 **`init_prot` 观测为 `0x10000000`** ❌（stock 是 `VM_PROT_READ=1`；0x10000000 非法）
（`files_count=3`、`mappings_count=16`、`files[0..1]`、第 16 条 file_offset 形态 均一致）

## 3. 最小修复（dyld 侧，无需内核改动）
1. **把 dyn 条目 VA 挪进 4GB region**（`VA−sr_base+size ≤ 0x100000000`）——这是解开 14 的关键；
2. 或**去掉 dyn 条目**（files_count=2/mappings_count=15，形状自洽），代价是丢失 dynamic config 传递；
3. 顺带把第 16 条 `init_prot/max_prot` 修回 `VM_PROT_READ`。

## 4. ★判定捷径（一条内核日志区分 ①/②）
`vm_shared_region_map_file_setup` 的匿名分支在 copyin 失败时会打印：
```
<func>(): for fd==-1 copyin() failed, errno=<n>
```
⇒ 设备侧 grep 到这行 = 来源①（指针不可读）；**没有**这行却仍 -14 = 来源②（VA 越界）。

## 5. 设备纪律（血泪，违反会白跑一趟）
- **永不**把 `/usr/lib/libSystem.B.dylib` 改名/移走（`.OFF` 那个坑：会让 536 对**任何**缓存都 EINVAL）；
- **FS 写必须在 cachereg 之前**（否则 blob 失效）；
- **设备串行**：同一台设备上不要与另一个会话同时做实验（共享 `/usr/lib/dyld`、shim、cachereg、trustcache、region）；
- 动设备前先留状态快照（关键文件 md5 + `ps|grep cachereg` + `sysctl vm.shared_region_count`），动完复位到标准态；
- 判据用 `DYLD_PRINT_LIBRARIES` 看 UUID：`<D161E41A>`=缓存、`<B90391D8>`=shim，各看各的，别互相改文件；
- 本类问题**优先读源码/文档**（`analysis/` 下 XNU + dyld 源码齐全），IDA 只用于确认二进制地址。
