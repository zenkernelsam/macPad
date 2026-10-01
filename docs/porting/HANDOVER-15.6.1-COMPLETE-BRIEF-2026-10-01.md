# 完整交接简报：在越狱 iPad 上用 macOS 共享缓存跑起 CLI（2026-10-01）

> **用途**：交给另一个 AI 做独立复核 / 继续推进。
> **阅读须知**：本文按"证据等级"标注每一条。**第 6 节专门列出"我可能错在哪"**——
> 那一节比结论更重要。**我不认为 15.6.1 已经无路可走**；我列出的是"我试过并失败的路线"
> 与"我据此推出的判断"，其中至少 3 处推导链存在被我忽略的可能。

---

## 0. 目标

让 `/bin/echo HI` 在越狱 iPad 的 chroot 里用**真正的 macOS dyld + macOS 共享缓存**跑起来。
最终目标是 macOS WindowServer（GUI），CLI 是里程碑。

---

## 1. 环境与资产（全部已核实的硬事实）

### 1.1 设备
| 项 | 值 |
|---|---|
| 机型 / 芯片 | **iPad13,11**（M1）/ `Darwin Cs-Pad-2 22.3.0` |
| 系统 | **iOS 16.3**（20D47），内核 `xnu-8792.82.2~1 RELEASE_ARM64_T8103` |
| 越狱 | **Dopamine rootless 半越狱**（`/var/jb/basebin/libjailbreak.dylib` 存在） |
| SSH | `root@192.168.64.1 -p 2222`，密码 `cisco`（`sshpass`；`~/.ssh` 为空） |
| ⚠️ zsh 陷阱 | 变量不做分词：**不能** `$SSH 'cmd'`，要用数组 `S=(sshpass -p cisco ssh …); "${S[@]}" 'cmd'` |
| 设备侧 python | `/var/jb/usr/bin/python3`（procursus）。**没有 `os.chroot`**，要用 `ctypes.CDLL(None).chroot()` |

### 1.2 chroot rootfs
| 项 | 值 |
|---|---|
| 路径 | 设备 `/var/mnt/rootfs`（macOS **15.6.1 / 24G90**） |
| 来源 | **不是**从 IPSW 解的，而是从**运行中的宿主 macOS rsync** 的（`misc/build-rootfs-15.6.1.sh`） |
| 缓存路径 | `System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/` |
| 当前 dyld | `/usr/lib/dyld` = **1,239,616 B**（F1 那个尺寸；`dyld.cur.bak` 1,240,752 = pristine） |
| SystemVersion | `ProductBuildVersion 24G90` ✓ |

### 1.3 iOS 内核侧（xnu-8792.81.2 源码，本地 `analysis/xnu-xnu-8792.81.2`）
```c
// osfmk/mach/shared_region.h:90-93
#define SHARED_REGION_BASE_ARM64          0x180000000ULL
#define SHARED_REGION_SIZE_ARM64          0x100000000ULL      // 4 GB
#define SHARED_REGION_NESTING_BASE_ARM64  SHARED_REGION_BASE_ARM64
#define SHARED_REGION_NESTING_SIZE_ARM64  SHARED_REGION_SIZE_ARM64
```

### 1.4 macOS 15.6.1 共享缓存（本机副本在 `analysis/dyld-cache-15.6.1/`，与设备同 SHA）
| 文件 | 字节 |
|---|---|
| `dyld_shared_cache_arm64e` | 2,712,764,416 |
| `dyld_shared_cache_arm64e.01` | 2,203,500,544 |

**映射表**（`RE-confirmed`，`struct` 直读；结构体 `dyld_cache_mapping_and_slide_info` = **56 字节**，见
`include/mach-o/dyld_cache_format.h:141`）：

```
主缓存  sharedRegionStart=0x180000000  sharedRegionSize=0x12c760000
 m0 0x180000000 +0x067f5c000 -> 0x1e7f5c000
 m1 0x1e7f5c000 +0x001e90000 -> 0x1e9dec000   slideInfoVer=5 pageSize=0x4000
 m2 0x1ebdec000 +0x00239c000 -> 0x1ee188000   ver=5   ← 历史 m3 写错误受害者
 m3 0x1ee188000 +0x000024000 -> 0x1ee1ac000   ver=5
 m4 0x1ee1ac000 +0x01200000  -> 0x1ef3ac000   ver=5
 m5 0x1ef3ac000 +0x07cc4000  -> 0x1f7070000   ver=5
 m6 0x1f9070000 +0x05cdc000  -> 0x1fed4c000
 m7 0x1fed4c000 +0x268c0000  -> 0x22560c000
.01
 m0 0x22560c000 +0x54808000 -> 0x279e14000
 m1 0x279e14000 +0x021c4000 -> 0x27bfd8000   ver=5
 m2 0x27dfd8000 +0x038b4000 -> 0x28188c000   ver=5   ← 跨 0x280000000（=共享区末端）
 m3 0x28188c000 +0x00d90000 -> 0x28261c000   ver=5
 m4 0x28261c000 +0x045d4000 -> 0x286bf0000   ver=5
 m5 0x288bf0000 +0x001dc000 -> 0x288dcc000
 m6 0x288dcc000 +0x23990000 -> 0x2ac75c000   ← 异常/越界地址
```
**总跨度 = `0x180000000..0x2ac75c000` = 4.77 GB > 4 GB。**

---

## 2. 核心失败现象（原始证据）

### 2.1 崩溃字段（逐字，来自 `docs/porting/dyld-15.6.1-state.md:2790-2796`）
```
[exc] type=12 code0=0xa000000100000000 code1=0x280000000 thr=3587 tsk=6915
[exc] pc=0x104454f90 lr=0x1044845b0 ...
[exc] x0=27dfd8000 x1=0 x2=3 x3=40012
[exc] x4=4 x5=569cc000 x16=c5
[vm] 0x27dfd8000..0x28188c000 prot=3/3 off=0x569cc000 shared=0
```
解码（`osfmk/kern/exc_guard.h:136-172`、`osfmk/mach/vm_statistics.h:330/334`）：
`type=12`=EXC_GUARD；`code0[63:61]=5`=`GUARD_TYPE_VIRT_MEMORY`；`code0[60:32]=1`=`kGUARD_EXC_DEALLOC_GAP`；
**subcode=code1=`0x280000000`（= 共享区末端 = 洞起点）**；`x0`/`[vm]` = 被删除的 mmap 范围（`.01` 的 m2）。

### 2.2 守卫链（`RE-confirmed`，全部 file:line）
```
用户 mmap(MAP_FIXED)  ← macOS dyld 映射 .01 的 m2
  bsd/kern/kern_mman.c:661   VM_FLAGS_FIXED|VM_FLAGS_OVERWRITE
  vm_map_enter:2416 → :2721  remove_flags = NO_MAP_ALIGN|NO_YIELD (+IMMUTABLE)
  vm_map_delete:2744         ← 关键：不带 VM_MAP_REMOVE_GAPS_FAIL
    vm_map.c:8094  vm_map_round_page(s, PAGE_MASK) < end
                     → state |= VMDS_FOUND_GAP;  gap_start = s      ← "尾部空洞"
  vm_map.c:8693  if (state & VMDS_FOUND_GAP)
  vm_map.c:8698    if (flags & VM_MAP_REMOVE_GAPS_FAIL) → ret = KERN_INVALID_VALUE
  vm_map.c:8701    else vm_map_guard_exception(gap_start, kGUARD_EXC_DEALLOC_GAP)   ← 致命
  vm_map.c:7789  fatal = (task->task_exc_guard & TASK_EXC_GUARD_VM_FATAL)
  vm_map.c:7792  thread_guard_violation → virt_memory_guard_ast:7701-7756 → task_bsdtask_kill
```
- `VM_MAP_REMOVE_GAPS_FAIL` 的**唯一置位者**是 `vm_reclaim.c:600`，**唯一读点**是 `vm_map.c:8698`
  ⇒ 用户态 mmap 永远走**致命**分支（无法靠调用者规避）。
- 产生点判定 = `vm_map.c:8693-8702`（**不是** `vm_reclaim.c:592-604`）：依据是 subcode 恰为共享区末端 +
  现场是用户态 mmap 帧（`x16=0xc5`、`x5` 与 `[vm]` 行 fileoff 一致、pc/lr 在 dyld 内）。

### 2.3 `task_exc_guard` 的位（**源码为准**，`osfmk/mach/task_info.h:546-568`）
```
TASK_EXC_GUARD_VM_DELIVER 0x01 / VM_ONCE 0x02 / VM_CORPSE 0x04 / VM_FATAL 0x08
TASK_EXC_GUARD_MP_FATAL   0x80   ; TASK_EXC_GUARD_THIRD_PARTY_DEFAULT_SHIFT 0x8
```

---

## 3. 我用设备实测确定的地址与偏移（`runtime-confirmed`）

### 3.1 `task_exc_guard` 在 `struct task` 里的偏移 = **`task + 0x5C4`**
方法：`proc_self()` → `+0x18` = `ro` → `ro+0x8` = `task`（**须剥 PAC**：`0xffff800000000000 | (v & 0x7FFFFFFFFFFF)`），
在 `task+0x3E8..0x640` 扫描 4 字节字。**全区间唯一候选**。跨进程交叉验证：

| 进程 | `task` | **`task+0x5C4`** | `task+0x3E8`（shared_region） |
|---|---|---|---|
| pid 0 kernel_task | `0xfffffe1300441328` | **`0x00`** | `0x0` |
| pid 1 launchd | `0xfffffe1300e59328` | **`0x53`** | `0xfffffe14ccb99540` |
| 我们的进程 | — | **`0x99`** | `0xfffffe14ccb99540` |

（`0x99 = MP_DELIVER|MP_FATAL|VM_FATAL|VM_DELIVER`；`0x53` 无 FATAL。）

### 3.2 `task_exc_guard_default` 全局 = **IDB `0xFFFFFE000A9FABE0`**
反编译设初值的函数（IDB `sub_FFFFFE0007FAF0D8`）：
```asm
0xfffffe0007faf160  ADRP  X10, #dword_FFFFFE000A9FABE0
0xfffffe0007faf164  LDR   W10, [X10, #dword_FFFFFE000A9FABE0@PAGEOFF]   ; task_exc_guard_default
0xfffffe0007faf168  LDRB  W11, [X1,#0x79]
0xfffffe0007faf16c  TBNZ  W11, #2, plat
0xfffffe0007faf170  UBFX  W10, W10, #8, #8     ; 第三方 = (default >> 8) & 0xFF
0xfffffe0007faf174  STR   W10, [X19,#0x5C4]
plat: 0xfffffe0007faf1c8  AND  W10, W10, #0xFF ; 平台 = default & 0xFF
      0xfffffe0007faf1cc  STR   W10, [X19,#0x5C4]
      0xfffffe0007faf210  MOV   W10, #0x53 ; 'S'   ; ← 与实测 launchd=0x53 对上
      0xfffffe0007faf214  STR   W10, [X19,#0x5C4]
```
**与源码逐条吻合。** 运行时读回（本 boot slide `0x1a874000`）：
`0xfffffe002526ebe0` = **`0x00000099`**（平台字节 `0x99`、第三方字节 `0x00`）✓

### 3.3 `vm_shared_region_create` = IDB `0xFFFFFE0008060FD0`；`size` 的物化点
```
0xfffffe000806115c  MOV  X19, #0x180000000      ← base_address
0xfffffe0008061160  MOV  X20, #0x100000000      ← size（字节 34 00 C0 D2 = MOVZ X20,#1,LSL#32）
```
⚠️ **IDA 把它显示成 `MOV`（别名）**，按 `movz`/`LDR`/`immediate` 搜**全是 0 命中**；
只能在函数内做**文本搜索** `100000000` 才找得到。该地址在 `com.apple.kernel:__text` 内。

### 3.4 `vm_shared_region` 结构体字段偏移（`RE-confirmed`，由构造函数赋值序列读出）
| 偏移 | 字段 | | 偏移 | 字段 |
|---|---|---|---|---|
| `+0x00` | 引用计数 | | `+0x48` | `sr_pmap_nesting_start` |
| `+0x18` | `root_dir` | | `+0x50` | `sr_pmap_nesting_size` |
| `+0x20` | `cpu_type` | | `+0x70` | `sr_page_shift` |
| `+0x24` | `cpu_subtype` | | `+0x73` | `sr_64bit` |
| `+0x38` | `sr_address` | | `+0x76` | `sr_stale` |
| `+0x40` | `sr_size` | | `+0x77` | `sr_reslide` |
| | | | `+0x90` | `sr_rsr_version` |
| | | | `+0xA0` | `sr_id` |

### 3.5 `exec` 与 `fork` 的语义（**实测 + 反汇编**）
- **`execve()` 会重建 task**：实测同一 pid 下 task 指针从 `0xfffffe13018769f8` 变为 `0xfffffe13018800c8`，
  且 `task_exc_guard` 被重置为默认值 `0x99`。
- **`fork()` 是从父任务复制**：`sub_FFFFFE0007FA31B4` 内 `LDR W8,[X22,#0x5C4]` → `STR W8,[X19,#0x5C4]`
  （`kernel_task` 特判置 0）。

### 3.6 slide 求法（**修正了项目脚本的缺陷**）
项目 `kfind_slide.py` 用 `IDAmemset(0xfffffe0007f18000)` 的 16 字节序言当 needle，
但**步长 0x200000**——而本 boot 的 slide **`0x1a874000` 不是 2 MB 对齐**，因此**必然漏掉**。
改用 `vm_shared_region_create` 的真实序言 `7f2303d5 ff0303d1 e923056d fc6f06a9` + **步长 0x1000**：
`SLIDE = 0x1a874000`（脚本：`misc/e2_slide.py`）。

---

## 4. 已尝试并被否证的路线（**含证伪证据，请勿重试**）

| 路线 | 做法 | 结果 | 证据 |
|---|---|---|---|
| **B1 头部改档** | 6 次改缓存 header（`sharedRegionSize`/`subCacheArrayCount`/`dynamicDataOffset` 等） | ❌ 全无效 | 见 `STATIC-cache-layout-exceeds-4gb-shared-region.md` §9/§10 |
| **B2' 重建 ≤4GB 缓存** | 用官方 builder | ❌ **不可行** | builder 源码在 `analysis/dyld-dyld-1286.10/cache-builder/` 完整存在，但 `cache_builder/NewSharedCacheBuilder.cpp:54` `#include <SharedCacheLinker/SharedCacheLinker.h>`，而 SLC 依赖 **`ld/`（ld64 内部件）——开源 drop 里没有**；`/usr/bin/update_dyld_shared_cache` 是 16 KB 桩 |
| **T4 `DYLD_SHARED_REGION=private`** | 设备实测 | ❌ 同一个 EXC_GUARD | `STATIC-cache-layout…md:288` |
| **T5 `DYLD_SHARED_REGION=avoid`** | 设备实测 | 非模拟器**被忽略** | `DyldProcessConfig.cpp:1394-1396` 注释 `// only support … on simulator` |
| **D③ 无缓存运行 dyld** | 删缓存 + 磁盘 dylib | ❌（**但见 §6.1，这条我可能错**） | `SharedCacheRuntime.cpp:1476-1500` `reuseExistingCache` 是快路径；宿主实测 `DYLD_SHARED_CACHE_DIR=<空目录>` 仍报 `re-using existing shared cache` |
| **E1 放大 `SHARED_REGION_SIZE`** | 改 text 一条指令 | ❌（**但见 §6.3**） | 补丁点已精确到 `0xfffffe0008061160`，但它在 `__text`；项目文档记 text 写会挂死（KTRR/PPL），且**无成功先例** |
| **E2 守卫非致命化（改 task）** | 清自身 `task+0x5C4` 后 exec | ❌ 施加方式错 | 与 baseline 逐字节相同（`EXIT=137`）——因 exec 重建 task |

### 4.1 E2 的两次实验（**第二次有效**）
| 运行 | 动作 | 结果 |
|---|---|---|
| A baseline | `chroot`+`execve /bin/echo` | **`EXIT=137`**（SIGKILL）｜stderr **80 B**（5 条记录） |
| B 改 task | 清 `task+0x5C4` 的 `0x09` 后 exec | **同 A**（137） |
| **C 改全局默认** | `kwrite32(task_exc_guard_default, 0x90)` 后 exec | **`EXIT=90`**（不再被守卫杀）｜stderr **112 B**（7 条） |

C 多出的两条记录（16 字节/条，8 字节 ASCII tag + 8 字节值）：
```
48 47 ("HG") = 0x00000002ac75c000     ← .01 m6 末端
48 41 ("HA") = 0x00000002ac760000     ← 同址 16K 页对齐
```
`0x2ac75c000` **正是 §1.4 里的越界地址**。
带 `DYLD_PRINT_SEGMENTS/LIBRARIES/INITIALIZERS` 复跑：stderr **一条 dyld 文本都没有**。
（stderr blob 的 7 个 tag：`DF/AN/FL/TD/F2/HG/HA`，**性质未定**，本地源码里搜不到。）

**已恢复**：全局改回 `0x99`；rootfs dyld 未被改动。

---

## 5. 其它已建立的技术事实（对后续有用）

### 5.1 dyld 源码要点（`analysis/dyld-dyld-1286.10`，= macOS 15.6.1 的 dyld）
| 事实 | 位置 |
|---|---|
| 缓存加载先走 **`reuseExistingCache` 快路径**；**只有 slow path 才 preflight 缓存文件** | `SharedCacheRuntime.cpp:1476-1500` |
| 缓存文件缺失 → `no shared cache file` → `loadAddress==nullptr` → 退化为 `JustInTimeLoader` 从磁盘加载 | `SharedCacheRuntime.cpp:564/612`、`dyldMain.cpp:683-693` |
| `DYLD_SHARED_REGION=avoid` **仅模拟器** | `DyldProcessConfig.cpp:1394-1396` |
| `opts.forcePrivate = allowEnvVarsSharedCache && cacheMode=="private"`；`allowEnvVarsSharedCache = amfiFlags & AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE` | `DyldProcessConfig.cpp:1350` / `:938` |
| **`DYLD_SHARED_CACHE_DIR` 被支持**（可换缓存目录） | `DyldProcessConfig.cpp:1149-1156`、`dyldMain.cpp:439` |
| **iOS-only** 哨兵 `/System/Library/Caches/com.apple.dyld/enable-dylibs-to-override-cache`（<1024 B）→ 切到 `.development` 缓存变体 | `SharedCacheRuntime.cpp:502-518`（整段在 `#endif //!TARGET_OS_OSX` 内）；`DYLD_SHARED_CACHE_EXT=".development"` |
| 共享缓存 mmap 落址是**编译期常量** `SHARED_REGION_BASE` | `SharedCacheRuntime.cpp:917` ⇒ **改 header 的 `sharedRegionStart` 无法平移缓存** |
| v3 缓存 ⇒ `canUsePageInLinking=false` ⇒ dyld **不走 syscall 550**、改走进程内 `rebaseDataPages` | `SharedCacheRuntime.cpp:1042-1063` |

### 5.2 dyld 本体
- 宿主 `/usr/lib/dyld` = **真 Mach-O**（2,289,328 B fat，含 arm64e），其 arm64e 切片与
  **缓存里抽出的那份 SHA256 完全相同**：`12dc97d541939a8e05d58f265f62eaef93fbce63740d7eefaee434cea7acbac5`
  （UUID `3247E185-CED2-36FF-9E29-47A77C23E004`）⇒ 项目里的 `analysis/dyld_15.6.1_arm64e_thin` 就是它。

### 5.3 macOS 15 的"库只在缓存里"
macOS 11+ 的系统库**代码只存在于共享缓存**，磁盘上的 `/usr/lib/*.dylib` 与框架二进制要么是"缓存桩"、
要么是**只在已安装系统上才解析的 firmlink**。⇒ 这也是 15.6.1 rootfs 必须从"运行中的宿主" rsync 的原因。

### 5.4 路线 D 的工具链（已建好，随时可用）
| 资产 | 位置/说明 |
|---|---|
| 抽取器 | `analysis/dyldwork/extract_host2.py`（真 block，`dsc_extractor`）⇒ 3257 文件 / 4.4 GB |
| 依赖闭包 | `misc/dsc_cache_subset.py`；`/bin/{echo,sh,bash,cat,ls,date}` 的闭包 = **564 dylib / 870.2 MB** |
| dyldextractor | `tmp/dscvenv`（2.2.2 + **v5 补丁** `misc/dyldextractor-2.2.2-slideinfo5.patch`） |
| uncache.py | fork 内 `VirtualMac/vz/uncache.py` + **v5 补丁** `misc/uncache-slideinfo5.patch`（含一处 `amap(rt)==None` 崩溃修复） |
| `.a2s` 符号索引 | `analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e.a2s`（**1.13 GB**，跑了 4h51m，1543 万符号） |
| ipsw-a2sb | fork `VirtualMac/build/toolchain/bin/ipsw-a2sb`（102 MB） |
| 批量脚本 | `misc/uncache_batch.sh` |
| **成果** | **15.6.1 的 dylib 已可"抽出来做成可加载"**：宿主 `dlopen` 实测通过（10 个 CLI 核心库，可验证的 8/8 全过；最大样本 libxml2：3621 rebases/134 binds） |

### 5.5 路线 C（13.2.1）现状
- 资产：`~/Desktop/VirtualMacOniPad/VirtualMac/build/downloads/UniversalMac_13.2.1_22D68_Restore.ipsw`（12.49 GB）
- `ipsw`（fork 内，已编好）可**免 sudo** 挂载：`ipsw mount fs` → `/tmp/098-26649-067.dmg.mount`（OS 卷）；
  `ipsw mount sys` → `/private/tmp/098-26709-070.dmg.mount`（cryptex，**内含真缓存**
  `System/Library/dyld/dyld_shared_cache_arm64e{,.01}` = 1,600,389,120 / 1,719,320,576 B）
- ✅ **Data 卷骨架可得**：`System/Library/Templates/Data/`（`private/etc` 75 项、`Library` 完整）
- ⚠️ **但三个卷里都找不到 `libSystem.B.dylib` 与框架二进制**（见 §5.3）⇒ 需要"已安装的 13.2.1"
- 13.2.1 跨度 3.207 GB，**不跨界**；slide info 全 v3 ⇒ **不需要任何内核补丁**

### 5.6 运行时内核补丁能力
- KRW：`/var/jb/basebin/libjailbreak.dylib`（`jbclient_process_checkin` → `jbclient_initialize_primitives`
  → `kread32/64`、`kwrite32/64`、`kreadbuf_phys/physreadbuf_virt`、`proc_self/proc_find/…`）
- **`kcall` 不可用**（缺 `IOSurfaceRootUserClient` entitlement）；**`kread/kwrite` 可用** ✓
- ⚠️ PAC 数据指针需剥：`0xffff800000000000 | (v & 0x7FFFFFFFFFFF)`
- 项目纪律：**先运行时定位 + 逐字节核对，再写**；PAC 指针字段禁裸写（`v_mount` 裸写 → panic）
- 设备侧脚本：`/var/mobile/{kread,kptr,kscan_sig,e2_*,triage}.py` 等 100+ 个

---

## 6. ⚠️ 我可能错在哪（**这一节最重要，请重点复核**）

### 6.1 D③ 的否证**只在宿主上做过，设备侧没测**（我最担心的一条）
我的依据是：`loadDyldCache` 先走 `reuseExistingCache`，而宿主实测它返回"复用已有区域"。
**但宿主是 macOS 内核**，设备是 **iOS 内核 + iOS 共享区**。`reuseExistingCache` 会**校验区域里的缓存**
（magic/UUID/平台）；macOS dyld 面对 iOS 区域的缓存**很可能校验失败**。
而源码是：
```cpp
if ( reuseExistingCache(options, results) ) { success = !hasError; }
else { success = mapSplitCacheSystemWide(options, results); }   // ← 只有这里才 preflight 文件
```
**若 `reuseExistingCache` 返回 false**，就会走 slow path → 缓存文件缺失 → `loadAddress==nullptr`
→ **退化为从磁盘加载** ⇒ **D③ 复活**。
**⇒ 值得在设备上直接测**：在 chroot 环境设 `DYLD_SHARED_CACHE_DIR=<空目录>` 跑 `/bin/echo`，
看 dyld 报 "no shared cache file"（D③ 活）还是 "re-using existing"（D③ 死）。

### 6.2 B（重建缓存）：我**只试了 Xcode 26.3 / SDK 26.2 一种工具链**
- 失败点是 `include/mach-o/dyld.h:122` 一带 `__API_AVAILABLE(...)` 后 "expected ','"
  （与 `DYLD_DRIVERKIT_UNAVAILABLE` 在 `#ifdef __DRIVERKIT_19_0` 下的展开有关）。
  **换一个旧 SDK / 旧 Xcode 可能就能编过。**
- `ld/` 只被 **SLC（closure 链接器）** 需要。**若把 closure 功能裁掉/给 SLC 造桩**，builder 也许能编出来。
- 我**没有**验证过"官方 builder 在任何配置下都编不出来"。

### 6.3 E1：我把"text 写会挂死"当作既定事实**继承**了下来
- 那条结论来自项目早期文档（KTRR/PPL）。我**没有**对 `0xfffffe0008061160` 这**一个 4 字节写**做过实验。
- 而且：`SHARED_REGION_SIZE` 的值也可能通过**其它机制**改变（例如在区域**重建**前改，
  或改 `vm_shared_region_create` 之外的东西）。我没有穷尽。

### 6.4 "E2 不解决根因"是我的**推断**，不是实测
我据 C 运行里多出的 `0x2ac75c000`/`0x2ac760000` 推断"前进到了布局越界这个根因"。
**但那两条记录的性质我没能识别**（可能是别的失败：m3 写错误？格式 13 分派？某个 assert？）。
**⇒ "绕过守卫之后到底撞上什么"目前是未知的**，而它恰恰是 15.6.1 是否还有机会的关键。

### 6.5 未识别的诊断块
A/B/C 三轮 stderr 都有那个二进制块（tag：`DF/AN/FL/TD/F2/HG/HA`）。**我完全没能识别它来自哪里**
（不是 dyld 的日志格式，本地源码里搜不到）。**若能识别，可能就是关键线索。**
可用线索：设备上有 `/var/mobile/triage.py`（读内核 kdebug triage ring，`KDBG_TRIAGE_SUBSYS_DYLD_PAGER=4`）。

### 6.6 其它可能被忽略的方向（我只做了浅层判断）
- `enable-dylibs-to-override-cache` 哨兵（iOS-only）：我只在源码里看到它"切到 `.development` 缓存"，
  **没有**深挖"让磁盘 dylib 覆盖缓存"的完整语义——那可能是另一条 D 路线的入口。
- 内核侧 `vm_map_store.h:89-90` 的 `UPDATE_HIGHEST_ENTRY_END` 谓词：放大区域会改分配 hint，我未评估其后果。
- 是否**必须**把整个缓存映射进去：缓存是被 dyld **整段**映射的，但我没验证"能否只映射一部分"。

---

## 7. 建议的下一步实验（按"信息量/成本"排序）

| # | 实验 | 成本 | 能回答什么 |
|---|---|---|---|
| **S1** | **识别那个 112 字节 stderr 块**（用 `triage.py` 读内核 triage ring 对照；或对 F1 dyld/libmachook 做 xref） | 低 | 绕过守卫后到底撞上什么 ⇒ **决定 15.6.1 是否还有机会** |
| **S2** | 在设备 chroot 里设 **`DYLD_SHARED_CACHE_DIR=<空目录>`** 跑 `/bin/echo`，看 dyld 说什么 | 低 | 复核 §6.1：D③ 是否真死 |
| **S3** | 用**旧 SDK/裁掉 SLC** 再试编官方 builder | 中 | 复核 §6.2：B 是否真死 |
| **S4** | 对 `0xfffffe0008061160` 做**一次 4 字节 text 写**并重启验证 | 中（有 panic 风险） | 复核 §6.3：E1 是否真死（区域放大后 `.01` 是否不再跨界） |
| **S5** | 走路线 C：起一台 **macOS 13.2.1 VM** → 跑 `build-rootfs` rsync | 高（需用户操作） | 一条**不依赖任何内核算术**的确定路径 |

**S1 与 S2 都便宜且信息量大，建议先做这两个。**

---

## 8. 关键文件索引

| 路径 | 内容 |
|---|---|
| `docs/porting/STATIC-b2-cache-rebuild-pipeline.md` | B2'/D 路线全过程、§16 里程碑、§17 D③调研、§18 D③判定、§19 路线 C 实操 |
| `docs/porting/STATIC-cache-layout-exceeds-4gb-shared-region.md` | 映射表、6 次改档失败、T4/T5 |
| `docs/porting/STATIC-m3-dyld-pager-format13.md` | EXC_GUARD 编码、format-13、F1 |
| `docs/porting/KB-E-shared-region-limit.md` | **路线 E 知识库**（含 §7 全部 IDB/设备实测） |
| `docs/porting/dyld-15.6.1-state.md` | 项目的实时状态台账（原始崩溃字段在 `:2790-2796`） |
| `analysis/xnu-xnu-8792.81.2/` | 内核源码（`osfmk/vm/vm_map.c`、`vm_shared_region.c`、`mach/shared_region.h`） |
| `analysis/dyld-dyld-1286.10/` | dyld 源码（= 15.6.1） |
| `analysis/kc_raw_16.3_T8112.bin*` | 内核 IDB（IDA Instance1，imagebase `0xfffffe0007004000`） |
| `analysis/dyld_15.6.1_arm64e_thin` | dyld IDB（Instance2，imagebase `0x0`） |
| `misc/e2_slide.py` / `misc/e2_patch_default.py` / `misc/e2_launch.py` | E2 的 slide 求解 / 全局改档+执行 / 启动器 |
| `misc/uncache_batch.sh`、`misc/dsc_cache_subset.py` | 路线 D 的批量工具 |
| `AGENTS.md` | 角色纪律、内核写安全规则、IDA 实例表 |

---

## 9. 一句话总结

15.6.1 的**根因**是"缓存 4.77 GB > iOS 共享区 4 GB"，而**最外层症状**是那个致命的 `EXC_GUARD`。
我已证明**症状可以被绕过**（E2 有效），但**绕过后撞上了什么，我还没查清**——
**这是 15.6.1 是否还有机会的关键未知**（§6.4/§7-S1）。若那条失败仍可归因于布局，
则需要"更小的缓存"（B，缺工具）或"更大的区域"（E1，text 写）；
若那条失败是**别的原因**，15.6.1 可能还有路。

---

## 10. S1 / S2 实测结果（**含对我此前结论的两处更正**，2026-10-01 16:20）

### 10.1 S2：`DYLD_SHARED_CACHE_DIR` 指向空目录 —— **不阻塞，但结论不充分**

在 chroot 里设 `DYLD_SHARED_CACHE_DIR=/tmp/dsc_none`（空目录，rootfs 内已建）后 `execve /bin/echo`：
- 那个 blob **依旧是 7 条**，`HG = 0x2ac75c000` **照样出现** ⇒ **dyld 仍然映射了缓存**（没被 env 劝退）。
- 但 `DF` 记录的**值从 3 变成 4** ⇒ 该 env **确实被读到了**（只是不足以让它放弃缓存）。
- `DYLD_PRINT_LIBRARIES=1` **完全没有输出** ⇒ 说明**这类 `DYLD_PRINT_*` 在平台二进制上被剪掉/忽略**。

⇒ **对 D③ 的判定仍然不可靠**：因为 `DYLD_*` 可能被 AMFI 剪除，**用 env 探针无法证明"dyld 不会被劝退"**。
若要真正判 D③，得用**不依赖 env** 的手段（例如把缓存文件真的移走，看 dyld 是报
`no shared cache file` 还是 `re-using existing`）——**尚未做**。

### 10.2 S1：那个 blob 的真实身份 —— **两点更正**

**更正 1：blob 不是 libmachook 写的。**
不注入 `DYLD_INSERT_LIBRARIES`（mode=noinsert）复跑，blob **逐字节相同** ⇒ 与 libmachook 无关。

**更正 2（重要）：blob 不是"守卫绕过后的失败标记"。**
跑**项目自己的 sanity 路径** `run_bash.sh -c "echo hi"`（走 `launchdchrootexec`）：
```
chdir: No such file or directory
[launchdchrootexec] target=/bin/bash arch=arm64 insert=/usr/local/lib/libmachook_arm64.dylib
DF…  （同一个 blob，同样 7 条，HG=0x2ac75c000 / HA=0x2ac760000）
```
⇒ **blob 在"正常"的 chroot exec 上也会出现**，是通用的早期启动诊断，**与守卫、与 E2 无关**。
⇒ 因此我在 §6.4 里据 blob 的 `HG/HA` 推断"绕过守卫后撞上布局越界"——**这条推断没有证据支持，予以撤回**。
（`launchdchrootexec/main.m` 只打文本 banner，不是它。）

### 10.3 ⚠️ 新发现的**设备状态事实**（会改变所有 A/B 的解释）

**项目自己的 sanity 路径当前也是失败的**：
```
run_bash.sh -c "echo hi"  →  EXIT=90，stdout 为空（没有 "hi"）
```
⇒ **当前设备状态下，任何 chroot exec 都返回 `EXIT=90` 且无输出。**
这与 §4.1 里 E2 的"`137 → 90`"**必须合起来读**：
- `137 → 90` **确实证明守卫不再杀进程**（这是 E2 的有效性证据，成立）；
- **但 `90` 并不代表成功**——它就是"当前所有 chroot exec 都失败"的那个码。
⇒ **E2 的结论应修正为**："清守卫后，失败模式从'被守卫 SIGKILL'变成'与普通失败路径相同的 90'"，
而不能说"进程前进到了下一个失败点"。

**⇒ 结论：E2 之后到底卡在哪，目前**仍然未知**；而`EXIT=90` 这个通用失败码的成因
（是 dyld 的？bash 的？还是内核的？）也**没有查清**。这两条是**下一步最该查的**。

### 10.4 未识别项清单（交接给下一个 AI）

| 项 | 现状 | 可用线索 |
|---|---|---|
| 112~238 字节的 stderr blob（7 条 16 字节记录，tag `DF/AN/FL/TD/F2/HG/HA`） | **未识别**；已知：非 libmachook、非 launchdchrootexec、每次 chroot exec 都有 | 设备 `/var/mobile/triage.py`（读内核 kdebug triage ring）；或对设备 dyld（SHA `99569152…`，1,239,616 B，**非** F1）做 xref |
| `EXIT=90` 的成因 | **未识别**；已知：项目 sanity 路径也返回它 | 同上；或对比"能跑通的状态"下的退出码 |
| `HG=0x2ac75c000 / HA=0x2ac760000` 的语义 | **未识别**（已撤回"= 布局失败"的推断） | 同上 |

---

## 11. 交接建议（给下一个 AI）

**先做这三件（都不贵、都能推翻我）**：

1. **搞清 `EXIT=90` 与那个 blob**：它们出现在**每次** chroot exec 上，是当前失败的**共同症状**。
   建议先在设备上找一个**能跑通的**参照点（例如 pristine dyld + 原始缓存、或 `/usr/bin/true`），
   看它的退出码与 stderr 长什么样，再与失败态对比。
2. **不用 env 重测 D③**：把 rootfs 里的缓存文件**改名移走**（可逆），跑 `/bin/echo`，
   看 dyld 报 `no shared cache file`（D③ 活）还是 `re-using existing`（D③ 死）。
3. **复核 B**：用**旧 SDK**（或把 SLC 裁掉/造桩）再尝试编 `dyld_shared_cache_builder`。
   我只在 Xcode 26.3 / SDK 26.2 上失败过一次。

**我对 15.6.1 的当前立场**：**不确定**。根因（布局超区）是硬的，但"绕过守卫之后的失败"我没查清，
且我据以判断"E2 不解决根因"的那条证据（blob 里的 `0x2ac75c000`）**已被我自己撤回**。
所以**15.6.1 仍有可能是通的**——需要上面第 1、2 条来判定。
