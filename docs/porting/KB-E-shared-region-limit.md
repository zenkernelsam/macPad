# KB：路线 E —— iOS 共享区 4 GB 限制的解除可行性（完整知识库）

> 建立日期：2026-10-01。来源：xnu-8792.81.2 源码 + 内核 IDA(IDB kc_raw_16.3_T8112.bin) +
> 项目既有 RE 记录 + 三路并行调研。
> 级别标注：`RE-confirmed`（源码/反汇编/原始崩溃字段）／`runtime-confirmed`（设备实测）／`THEORY`。

---

## 0. 结论速览

| 变体 | 判定 | 一句话依据 |
|---|---|---|
| **E1 放大 `SHARED_REGION_SIZE_ARM64`** | ❌ **不可行**（补丁点已精确到 1 条指令，见 §7） | 是**编译期常量**（要写 kernel `__text`）；区域在**开机首个 exec** 就建立；且项目**无内核 text 写成功先例**、文档记 KTRR/PPL 下 text 写挂死 |
| **E2 让该守卫非致命** | ✅ **可行（首选）** | `task_exc_guard` 位控制：清 `VM_FATAL` 后 `vm_map.c:8701` 成空操作、`return KERN_SUCCESS`，`MAP_FIXED` 继续建立映射 |
| **E2′ 让进程不被判为"平台二进制"** | ✅ **可能零内核补丁**（待实测） | 平台二进制 `default & 0xff = 0x99`（含 FATAL）；第三方 `(default >> 8) & 0xff = 0`（**全静默**）⇒ 非平台进程本来就不会致命 |
| E3 只特判 submap 边界 | ⛔ 不建议 | 等价于 E1 的 text 补丁，且连带更多 |
| E4 dyld 侧绕开该映射 | ❌ 已否证 | `SHARED_REGION_BASE` 是 dyld **编译期常量**；6 次改档全无效（见主文档 §9/§10） |
| C 换 macOS 13.2.1 | ✅ **基线对照** | 缓存 3.21 GB < 4 GB、slide info v3 ⇒ dyld 自带进程内 fixup（见 §12） |

**一句话**：真正卡住我们的**不是"共享区太小"，而是"越界删除时撞到空洞被判致命"**。
把"致命"这一环解除，比把共享区改大要便宜得多。

---

## 1. 完整机制链（含原始崩溃字段）

### 1.1 原始崩溃字段（逐字，来自 `docs/porting/dyld-15.6.1-state.md:2790-2796`）

```
[exc] type=12 code0=0xa000000100000000 code1=0x280000000 thr=3587 tsk=6915
[exc] pc=0x104454f90 lr=0x1044845b0 ...
[exc] x0=27dfd8000 x1=0 x2=3 x3=40012
[exc] x4=4 x5=569cc000 x16=c5
[vm] 0x27dfd8000..0x28188c000 prot=3/3 off=0x569cc000 shared=0
```

解码（`osfmk/kern/exc_guard.h:136-172`、`osfmk/mach/vm_statistics.h:330/334`）：

| 字段 | 值 | 含义 |
|---|---|---|
| `type` | 12 | `EXC_GUARD` |
| `code0[63:61]` | 5 | `GUARD_TYPE_VIRT_MEMORY` |
| `code0[60:32]` | 1 | `kGUARD_EXC_DEALLOC_GAP` |
| `code1`（subcode） | **`0x280000000`** | **洞起点 = 共享区末端** ⇐ 决定性证据 |
| `x0` / `[vm]` | `0x27dfd8000..0x28188c000` | 被删除的 mmap 范围（`.01` 的 m2） |
| `x16` | `0xc5`=197 | mprotect/mmap 类 syscall 号，现场在用户态 mmap 线程 |

**⇒ 产生点判定 = `osfmk/vm/vm_map.c:8693-8702`（用户态删除跨空洞），不是 `vm_reclaim.c:592-604`。**
判据：subcode 恰为共享区末端；现场是用户态 mmap 帧（`x5` 与 `[vm]` 行 fileoff 一致、`pc/lr` 在 dyld 内）。
（注：`STATIC-m3-dyld-pager-format13.md:413-435` 曾把更早的 `0x2ac75c000` 归为 reclaim，该文自标 THEORY，
且那处现场同样是 `x16=0xc5` 的 mmap —— 本 KB 以此处逐字字段为准。）

### 1.2 链路（RE-confirmed，全部 file:line）

```
用户 mmap(MAP_FIXED)  ← dyld 映射缓存 .01 的 m2（0x27dfd8000, 0x38b4000）
  bsd/kern/kern_mman.c:661        VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE
  vm_map_enter:2416
  vm_map_enter:2721               remove_flags = NO_MAP_ALIGN | NO_YIELD (+IMMUTABLE)
  vm_map_delete:2744              ← 关键：不带 VM_MAP_REMOVE_GAPS_FAIL
      扫描 entry… s 停在共享区 submap 末端 0x280000000
      vm_map.c:8094   vm_map_round_page(s, PAGE_MASK) < end
                        → state |= VMDS_FOUND_GAP;  gap_start = s
  vm_map.c:8693   if (state & VMDS_FOUND_GAP)
  vm_map.c:8698     if (flags & VM_MAP_REMOVE_GAPS_FAIL) → ret = KERN_INVALID_VALUE   ← 用户路径走不到这里
  vm_map.c:8701     else vm_map_guard_exception(0x280000000, kGUARD_EXC_DEALLOC_GAP)
  vm_map.c:7789   fatal = (task->task_exc_guard & TASK_EXC_GUARD_VM_FATAL)
  vm_map.c:7792   thread_guard_violation → virt_memory_guard_ast:7701-7756
                    · 无 VM_DELIVER → 静默 return
                    · 有 VM_FATAL   → task_bsdtask_kill / exit_with_guard_exception
```

### 1.3 三个关键杠杆（决定 E2 可行）

| 杠杆 | 位置 | 现状 | 含义 |
|---|---|---|---|
| `VM_MAP_REMOVE_GAPS_FAIL` | 唯一读点 `vm_map.c:8698`；**唯一置位者 `vm_reclaim.c:600`** | 用户 mmap **永远不带** | ⇒ 用户态走的是**致命**分支，无法靠调用者规避 |
| `task->task_exc_guard` | 判定 `vm_map.c:7789` / `virt_memory_guard_ast:7701-7756` | 默认 `_TASK_EXC_GUARD_ALL_FATAL`（`task.c:470`）；boot-arg 可改（`task.c:924`）；`kern.task_exc_guard_default` sysctl **只读**（`sys_generic.c:2856-2859`）；运行时清 FATAL **被禁止**（`task.c:463/8443`） | ⇒ **不能靠 sysctl，但可以直接改这个 per-task 字段**（数据写） |
| 平台 vs 第三方默认值 | 平台取 `default & 0xff` = **0x99**（含 FATAL）；第三方取 `(default >> 8) & 0xff` = **0**（全静默） | 本项目已打 `plataccept` ⇒ 被判**平台** | ⇒ **E2′：让目标进程不是平台二进制，则它本来就不会致命** |

---

## 2. 共享区生命周期（决定 E1 的生死）

- 全局队列 `vm_shared_region_queue`（`vm_shared_region.c:180`）；
  lookup 的 key（`:386-398`）：`cpu_type` / `cpu_subtype` / **`root_dir`** / `sr_64bit` / `sr_page_shift` /
  `sr_reslide` / `sr_driverkit` / `sr_rsr_version`，且 `!sr_stale`。命中即引用 +1 复用（`:400-402`）。
- **每 boot 一份、之后复用**，不是每 exec 重建：首次由 `vm_map_exec`
  （`vm_map.c:13376` / 调用点 `:13397`，唯一调用者 `bsd/kern/kern_exec.c:1558`）触发 `vm_shared_region_enter`。
- **boot 之后仍可能重建的 4 条路径**：
  1. 所有引用者退出 → ref=0 → 延迟 `shared_region_destroy_delay=120s` 销毁（`:147`/`:577-596`）→ 下次 exec 重新 create（**新尺寸生效**）；
  2. `vm_shared_region_pivot()`（`:3773-3781`）标全部 stale —— 调用者仅 `bsd/vm/vm_unix.c:3931`（macOS Authenticated Root 启动），iOS 不走；
  3. `vm_shared_region_reslide_stale()`（`:3833-3846`）只对 `sr_reslide==TRUE` 生效；
  4. dyld `shared_region_check_np(0)` → `vm_shared_region_remove`（`vm_unix.c:2080-2083`）。
- **但 E1 依旧死**：常量是 text（`shared_region.h:91` → `vm_shared_region.c:693-694` 物化进局部 `size`），
  而项目**没有内核 text 写成功先例**，且文档记 text 写会挂死（KTRR/PPL，`state:885/1046`、
  `CONSOLIDATED:281`）；`HANDOVER-REPLY-2026-09-27.md:51-56` 明确"未 patch 任何内核字节"。
- 数值上放大 4→8 GB **不违反硬约束**（`ARM64_MIN_MAX_ADDRESS` 仍 < `MACH_VM_MAX_ADDRESS`=63 GB；
  pmap ASID bitmap 亦够）——**所以 E1 的瓶颈是"改不动 text"，不是"改了会炸"**。
- 唯一连带副作用：`vm_map_store.h:89-90` 的 `UPDATE_HIGHEST_ENTRY_END` 谓词会多排除一段 entry ⇒ 分配 hint 漂移。

**已知可用偏移（供将来）**：`task+0x3E8` = `task->shared_region`（RE-confirmed via `sub_FFFFFE00080608E8`）；
`sub_FFFFFE0008063720` = `vm_shared_region_enter`；`sub_FFFFFE00080608E8` = region getter；
内核主 `__text = 0xfffffe0007f1c000..0x868c000`。

---

## 3. 运行时内核补丁能力现状（Q4）

- **KRW**：Dopamine `libjailbreak.dylib` + 设备 python3/ctypes（`jbclient_initialize_primitives` →
  `kread32/64`、`kwrite32/64`、`kreadbuf_phys/physreadbuf_virt`、`physrw`、`proc_task`…）。
  脚本在 `analysis/dyldwork/`（设备同名件常驻 `/var/mobile/`）。
- **kcall 不可用**（需 `IOSurfaceRootUserClient` entitlement，python3 无）。
- **无内核 text 写成功先例**。真正成功的内核写都是**数据字段**：`csb_end_offset`（46 个分片）、
  `kwrite32(p_csflags)`。`v_mount` 裸写 → `Ptrauth failure with DA key` panic（2026-09-26）。
- **强制校验流程（必须照做）**：
  `kscan_sig.py` 运行时按序言签名定位 → `kpatch_c2.py --dry-run`（仅 `--apply` 才写，写后读回）→
  与 IDB `find_bytes` 唯一性 + 设备 `kread32` 逐字节核对 → 才施加。
  dry-run 已成功挡下一次误写（`_memset_s+0x17000000` 假阳性）。
- **禁止**：PAC 签名指针字段裸写。
- ⚠️ **未决矛盾（采信保守方）**：`state:894` 称 `runtime = IDB + 0x158B4000` 对 text 成立并"推翻旧论"；
  而 `HANDOVER-REPLY-2026-09-27.md:31` 明确"别用 IDA_addr+slide"、`CONSOLIDATED:42` 称 slide 每 boot 变。
  本 KB 采信**后者**：一律先运行时定位、再逐字节核对。

---

## 4. 反证条件（什么证据会推翻上面的判定）

| 结论 | 反证条件 |
|---|---|
| 产生点是 `vm_map.c` 而非 `reclaim` | 同刻设备日志出现 `vm_reclaim:` 或 "Skipping non fatal guard exception" |
| E2 清 FATAL 后 MAP_FIXED 能成功 | 清位后仍被杀，或出现 `vm_map.c:8690` 的 kernel-pmap panic 等旁路 |
| E2′ 非平台即静默 | 目标进程实为**平台**二进制（`plataccept` 自证）却被观测到非致命以外的行为；或第三方默认值不是 0 |
| E1 不可行 | 出现一次成功的内核 text 写（哪怕一个字节），或证明该常量不在 text 而在可写数据 |
| 区域每 boot 只建一次 | 发现 `vm_map_exec` 之外的 `vm_shared_region_create` 调用点，或存在 boot 后 resize 路径 |
| `task_exc_guard` 偏移可绕过 | 若该字段是 PAC 保护的或位于只读区 ⇒ 数据写路线也要重估 |

---

## 5. 下一步最小可验证实验（E2 优先，全部设备侧、可回滚）

> 前置：**唯一未解的技术缺口 = `task_exc_guard` 在 `struct task` 里的字节偏移**。
> IDA 的类型库对该结构不可用（`type_inspect("task")` 返回无成员、size 异常），需另行 RE：
> 建议用源码 `task.c:470/924` 的使用点反查，或在设备上以已知默认值（`0x…99` 形态）扫描 task 结构定位。

- **S1（先做，零内核补丁）**：让 chroot 里的 dyld/echo **不被判为平台二进制**，观察 `EXC_GUARD` 是否消失。
  **失败判据**：仍见 `type=12 code0=0xa000000100000000`（同一 guard）。
- **S2（若 S1 因其它依赖不可行）**：KRW **数据写**目标 task 的 `task_exc_guard`，清 `TASK_EXC_GUARD_VM_FATAL`（必要时连 `VM_DELIVER` 一起清），重跑。
  **失败判据**：进程仍被杀；或日志出现 `vm_map.c:8690` 一类的旁路 panic。
- **S3（E1 的判定复核，可选、纯静态）**：在 IDB 里定位 `vm_shared_region_create` 中物化 `0x100000000` 的指令，
  确认它确实在 text；并复核是否存在 `vm_map_exec` 之外的 create 调用点。
  IDA 现状：内核符号已剥离、重型查询会 300s 超时；可用线索是字符串 `"vm_shared_region.c"`
  @ VA `0xfffffe0007ea1e43`（文件偏移 `0xE9DE43`）反向 xref。
- **S4 全部失败** ⇒ 回路线 C（13.2.1，资产已在 `~/Desktop/VirtualMacOniPad/…/UniversalMac_13.2.1_22D68_Restore.ipsw`）。

### 5.1 附注：规模比想象的小得多（对 D 是好消息）

早先为 **B2'（重建缓存）** 算过一份**依赖闭包**（种子 = `/bin/{echo,sh,bash,cat,ls,date}`），
证据在 `STATIC-b2-cache-rebuild-pipeline.md:119-125`：

| 范围 | 数量 | 体积 |
|---|---|---|
| 缓存全量抽取（`extract_host2.py`） | 3257 个 dylib | 4.4 GB |
| **CLI 依赖闭包**（`dsc_cache_subset.py`） | **564 个 dylib** | **870.2 MB** |
| mini-root（闭包 + 6 个种子 + `SystemVersion.plist`） | 570 个文件 | **871.8 MB** |

两个含义：
1. **对 B2'**：证明"≤4 GB 的缓存"**在体积上完全做得到**（871.8 MB ≪ 4 GB）—— 那条路缺的只是 builder，不是体积。
2. **对 D**：要"做成可加载"的范围可以从 3257 个缩到 **564 个**，工程量降一个数量级。

⚠️ 注意别混淆三个不同的产物（都曾被记成"从 4.x GB 解压出来的东西"）：
`extract_host2.py` 的 **3257 个 Mach-O dylib / 4.4 GB**、
`dsc_cache_subset` 的 **570 文件 / 871.8 MB mini-root**、
以及 `ipsw a2sb` 的 **`.a2s` 查找表 / 713 MB（22D68 同款）** ——
前两者是**可执行文件**，最后一个是**"地址→符号"索引**（里面没有代码），服务于**不同路线**（B2' vs D）。

**风险分级**：S1 最低（配置/签名层）；S2 中等（数据写，有先例，但要先拿到偏移并遵守校验流程）；
E1/E3 最高（text 写，无先例，可能 panic **或开不了机**）。

---

## 6. 交叉引用

- `docs/porting/STATIC-b2-cache-rebuild-pipeline.md` §13（路线 D/E 计划）、§15（E 的机制初判）
- `docs/porting/STATIC-cache-layout-exceeds-4gb-shared-region.md`（映射表、6 次改档失败）
- `docs/porting/STATIC-m3-dyld-pager-format13.md:382-445`（EXC_GUARD 编码、`task_exc_guard`）
- `docs/porting/dyld-15.6.1-state.md:2790-2796`（**原始崩溃字段**）、`:604-607`、`:1534-1548`（KRW 能力）
- `docs/porting/CONSOLIDATED-2026-09-29.md:44-50`（地址约定）、`:83/116`（`task+0x3E8`）
- `AGENTS.md`（内核写安全规则、IDA 实例表、设备访问）

---

## 7. Phase 2：内核 IDB 字节级交叉验证（2026-10-01，IDA Instance1）

> 实例自检：`module = kc_raw_16.3_T8112.bin`，`imagebase = 0xfffffe0007004000`，
> `auto_analysis_ready / hexrays_ready = true`。内核**符号已剥离**（全为 `sub_*`），
> 故以下全部靠**字符串/xref 锚点 + 源码比对**定位。

### 7.1 `vm_shared_region_create` = `sub_FFFFFE0008060FD0`（0x574 字节）`RE-confirmed`

锚点：字符串 `"vm_shared_region.c"` @ `0xfffffe0007ea1e43`，其 xref 只有两处
（`sub_FFFFFE0008060FD0`、`sub_FFFFFE0008063294`）。反编译前者，**逐条对上源码**：

| 源码（`vm_shared_region.c`） | 反编译所见 |
|---|---|
| 全局队列 `vm_shared_region_queue`（`:180`） | `off_FFFFFE000A9F2300`，遍历 `*(v20+8)` 双链表 |
| lookup 键 `cpu_type/cpu_subtype/root_dir/64bit/page_shift/reslide/driverkit/rsr_version`（`:386-398`） | `v20[8]==a2`、`v20[9]==a3`、`*((_QWORD*)v20+3)==a1`、`*((unsigned __int8*)v20+115/112/120/119)`、`v20[36]==a8` |
| 命中则 `reference_locked`（ref++，`:400-402`） | 调 `sub_FFFFFE00080609E0(v20)` |
| 未命中则 `kalloc_type(..., Z_WAITOK\|Z_NOFAIL)`（`:685`） | `zalloc_flags(&unk_FFFFFE00079E9910, 0x8000, ...)` |
| **`size = SHARED_REGION_SIZE_ARM64`（`:693-694`）** | **见 §7.2** |
| panic `"shared_region: vm_shared_region_lastid wrapped @%s:%d"`（`:422`，源文件 424 行） | 完全一致（字符串 `aSharedRegionVm` @ `0xfffffe0007ea1e0d`） |

**⇒ 编译产物与源码一致**，可作为后续补丁的依据。

### 7.2 ⭐ E1 的补丁点：**一条 4 字节指令**

```
0xfffffe000806115c   MOV  X19, #0x180000000     ← base_address = SHARED_REGION_BASE_ARM64
0xfffffe0008061160   MOV  X20, #0x100000000     ← size = SHARED_REGION_SIZE_ARM64
                     字节: 34 00 C0 D2 = MOVZ X20, #1, LSL#32   （RE-confirmed by get_bytes）
```

- 分支上下文：`a2 == 0x100000C`（= `CPU_TYPE_ARM64`）且 `a4`（is64bit）非 0 时取这对常量；
  紧随其后 `sub_FFFFFE00080222FC(v31, 0, v25, 1)`（建 map）与把 `v25` 存进 region 结构。
- **放大到 8 GB 只需把 `0xfffffe0008061160` 的 `MOVZ X20, #1` 改成 `#2`**（`34 00 C0 D2` → `54 00 C0 D2`）。
- ⚠️ 注意：IDA 把它显示为 `MOV`（别名），所以按 `movz`/`LDR`/`immediate` 搜都**搜不到**——
  本轮先按 mnemonic 搜 0 命中，改用**函数内文本搜索** `100000000` 才定位到。
- **但该地址在 `com.apple.kernel:__text` 内** ⇒ E1 的障碍**依旧是 KTRR/PPL 下的 text 写入**，
  与 §3 的结论一致：**补丁本身极简，难的是"能不能写进去"**。

### 7.3 附带产出：`vm_shared_region` 结构体字段偏移（`RE-confirmed`）

由 `v16`（新建的 region）的赋值序列读出：

| 偏移 | 字段 | 来源 |
|---|---|---|
| `+0x00` | 引用计数（初始 1） | `*(_QWORD*)v16 = 1` |
| `+0x18` | `root_dir` | `*((_QWORD*)v16+3) = a1` |
| `+0x20` | `cpu_type` | `*((_DWORD*)v16+8) = a2` |
| `+0x24` | `cpu_subtype` | `*((_DWORD*)v16+9) = a3` |
| **`+0x38`** | **`sr_address`** | `*((_QWORD*)v16+7) = v24` |
| **`+0x40`** | **`sr_size`** | `*((_QWORD*)v16+8) = v25` |
| `+0x48` | `sr_pmap_nesting_start` | `*((_QWORD*)v16+9) = v24` |
| `+0x50` | `sr_pmap_nesting_size` | `*((_QWORD*)v16+10) = v25` |
| `+0x70` | `sr_page_shift` | `v16[112] = v61` |
| `+0x73` | `sr_64bit` | `v16[115] = a4 != 0` |
| `+0x76` | `sr_stale`（查找时要求为 0） | `v16[118] = 0` |
| `+0x77` | `sr_reslide` | `v16[119] = v54` |
| `+0x90` | `sr_rsr_version` | `*((_DWORD*)v16+36) = a8` |
| `+0xA0` | `sr_id`（`vm_shared_region_lastid` 递增而来） | `*((_DWORD*)v16+40) = v40` |

> 这些是**数据字段**，将来若走"重建 region"或"运行时改 region 尺寸"的路，需要它们。

### 7.4 更正：`task_exc_guard` 的位与默认值（**源码为准**）

调研 subagent 报"平台二进制 `default & 0xff = 0x99`"，**与源码不符**，以源码更正：

```c
// osfmk/mach/task_info.h:546-559
TASK_EXC_GUARD_VM_DELIVER 0x01 / VM_ONCE 0x02 / VM_CORPSE 0x04 / VM_FATAL 0x08
TASK_EXC_GUARD_MP_FATAL   0x80            ; THIRD_PARTY_DEFAULT_SHIFT 0x8
// osfmk/kern/task.c:460,470
#define _TASK_EXC_GUARD_ALL_FATAL (_TASK_EXC_GUARD_MP_FATAL | _TASK_EXC_GUARD_VM_FATAL)   // = 0x88
uint32_t task_exc_guard_default = _TASK_EXC_GUARD_ALL_FATAL;
```
**⇒ E2 要清的是 bit `0x08`**（要更保险就连 `0x01` 一起清）。

并且一个重要的新事实：设置该默认值的 boot-arg **只在 `#if DEVELOPMENT || DEBUG` 下编译**
（`osfmk/kern/task.c:923-929`）⇒ **release 内核上无法用 boot-arg 改**，`_TASK_EXC_GUARD_ALL_FATAL`
（=0x88）是硬编码默认。

### 7.5 ~~仍未解~~ → **已解：`task->task_exc_guard` = `task + 0x5C4`**（2026-10-01 设备实测）

> 下面保留当初"未解"的记录与失败线索；**结论在 7.5.1**。

<details>
<summary>当初的未解记录</summary>

- `type_inspect("task")` 返回 size 异常、无成员 ⇒ IDB 无可用 `struct task` 类型。
- 尝试经 `kern.task_exc_guard_default` 的 sysctl oid
  （字符串 @ `0xfffffe0007ed97c6` → oid @ `0xfffffe0007991618`）反查全局变量**未成功**：
  该 oid 各指针解引用后**没有**任何一处等于 `0x88`/`0x08`/`0x99`，其 `+0x28`
  指向 `0xfffffe00079df180`，而那是个自指的链表结构（不是默认值本身）。**该线索判定为不通。**
</details>

#### 7.5.1 定位方法（设备侧只读扫描，已验证）

用设备上的 Dopamine KRW（`/var/jb/basebin/libjailbreak.dylib` + `kread64/kread32`）：
`proc_self()` → `+0x18` = `ro` → `ro+0x8` = `task`（**PAC 需剥离**：`0xffff800000000000 | (v & 0x7FFFFFFFFFFF)`），
然后在 `task+0x3E8 .. 0x640` 里找 4 字节对齐、值形如 `0x88/0x99/0x89/0x08` 的字。
脚本：设备 `/var/mobile/texg_scan.py`（只读）。

**结果：全区间只有唯一候选 `task+0x5C4 = 0x00000099`。**

#### 7.5.2 跨进程交叉验证（三个任务，值各不相同且都合理）

| 进程 | `task` | **`task+0x5C4`** | `task+0x3E8`（shared_region） |
|---|---|---|---|
| pid 0（kernel_task） | `0xfffffe1300441328` | **`0x00`** | `0x0` |
| pid 1（launchd） | `0xfffffe1300e59328` | **`0x53`** | `0xfffffe14ccb99540` |
| 我们的 python3 | — | **`0x99`** | `0xfffffe14ccb99540` |

- `kernel_task` 无共享区、guard 为 0 ✓；`launchd` 与我们的进程**同一个** shared_region 指针 ✓；
- `0x99` = `MP_DELIVER|MP_FATAL|VM_FATAL|VM_DELIVER` ⇒ **`VM_FATAL(0x08)` 置位**，正是 DEALLOC_GAP 致命的原因；
- `launchd` 是 `0x53`（**无 FATAL**）——说明该字段确实按任务差异设置，不是常量。
⇒ **`task+0x5C4` = `task_exc_guard` 定案。**

#### 7.5.3 写路径已验证

`/var/mobile/texg_write.py`：对自己这个任务 `kwrite32(task+0x5C4, 0x99 & ~0x09 = 0x90)`，
读回确认：

```
task_exc_guard @+0x5C4: before=0x99  kwrite32 rc=0  after=0x90
VERDICT: WRITE OK
```
（顺带确认：**`kcall` 在本机不可用**（缺 `IOSurfaceRootUserClient` entitlement），
但 **`kread/kwrite` 可用** —— E2 只需要后者。）

#### 7.5.4 E2 的施加方式（**已实测修正**）

~~原以为"exec 保留 task，所以可先改自己再 exec"。**实测否定**：~~

```
[e2] reexec pid=3774 task=0xfffffe13018769f8 guard 0x99 - rc0 -> 0x90
[e2] check  pid=3774 task=0xfffffe13018800c8 guard=0x99     ← 同一 pid，task 变了，值回到 0x99
```

**`execve()` 会重建 task**（pid 不变但 task 指针改变）⇒ 对 task 字段的补丁在 exec 瞬间丢失。
⇒ **只能改"新任务的默认值"**（见 7.5.5）。

> 另更正：**`fork()` 是"从父任务复制"**（`sub_FFFFFE0007FA31B4` 内
> `LDR W8,[X22,#0x5C4]` → `STR W8,[X19,#0x5C4]`，且对 `kernel_task` 特判置 0），
> 而 **`exec` 是"按默认值重建"**。此前"fork 会重新取默认值"的说法作废。

#### 7.5.5 ⭐ `task_exc_guard_default` = `0xFFFFFE000A9FABE0`（IDA 静态地址）

反编译设置新任务 guard 的函数（`sub_FFFFFE0007FAF0D8`）：

```asm
0xfffffe0007faf160  ADRP  X10, #dword_FFFFFE000A9FABE0
0xfffffe0007faf164  LDR   W10, [X10, #dword_FFFFFE000A9FABE0@PAGEOFF]   ; = task_exc_guard_default
0xfffffe0007faf168  LDRB  W11, [X1,#0x79]
0xfffffe0007faf16c  TBNZ  W11, #2, loc_FFFFFE0007FAF1C8                  ; 平台分支
0xfffffe0007faf170  UBFX  W10, W10, #8, #8        ; 第三方 = (default >> 8) & 0xFF   ← 源码 SHIFT 0x8
0xfffffe0007faf174  STR   W10, [X19,#0x5C4]       ; task->task_exc_guard
...
0xfffffe0007faf1c8  AND   W10, W10, #0xFF         ; 平台 = default & 0xFF
0xfffffe0007faf1cc  STR   W10, [X19,#0x5C4]
0xfffffe0007faf210  MOV   W10, #0x53 ; 'S'        ; ← 与实测 launchd=0x53 对上
0xfffffe0007faf214  STR   W10, [X19,#0x5C4]
```

**与源码逐条吻合**（`osfmk/mach/task_info.h:566` `TASK_EXC_GUARD_THIRD_PARTY_DEFAULT_SHIFT 0x8`；
平台取低字节；第三方取次字节）。观察到的平台值 `0x99` ⇒ 该全局低字节 = `0x99`。

**⇒ E2 的正确做法（未执行）**：把 `task_exc_guard_default` 的**低字节** `0x99` 改成 `0x90`
（清 `VM_DELIVER|VM_FATAL`）。这是**一次 4 字节内核数据写**，且**没有时序问题**——
之后任何新 exec 的平台进程都会拿到 `0x90`。
⚠️ 施加前必须：① 用运行时 slide 把 `0xFFFFFE000A9FABE0` 换算成运行时地址
（**不得**直接用 IDB 地址；先用 KRW 读回确认低字节 == `0x99` 再写）；
② 改回或重启即可回滚（纯数据字段，可逆）。

#### 7.5.6 E2 首轮实测：**清自己的 task 位无效**（已做，判定为"施加方式错"）

`/var/mobile/e2_launch.py` 的 `patch` 模式（清自身 `task+0x5C4` 的 `0x09` 后再 `chroot+execve`）
与 `baseline` 模式**结果逐字节相同**：`EXIT=137`，stderr 都是同一段 80 字节二进制块
（`44 46 …/41 4e …/46 4c …/54 44 …/46 32 …`，每 16 字节 = 8 字节 tag + 8 字节值）。
**原因已定位**：exec 重建 task，补丁丢失（7.5.4）——**不是"守卫不是凶手"**。
（该 80 字节块的作用**未定**：本地源码里搜不到这些 tag；设备上有 `triage.py` 可读内核 kdebug triage ring，下一步可用它对照。）

### 7.6 调用点与守卫函数：做到哪一步

- **`vm_shared_region_enter`（`sub_FFFFFE0008063720`，沿用项目既有标注）只有 1 个 code xref**：
  `0xfffffe000802d4b4`，位于 `sub_FFFFFE000802D40C`（0x174 字节）内。
  **这与源码"`vm_shared_region_enter` 唯一调用点在 `vm_map_exec` 内"（`vm_map.c:13397`）一致**，
  即"exec 是唯一入口"。
  （保留意见：调用者仅 372 字节，比典型的 `vm_map_exec` 小；未进一步证明其身份，但"唯一调用者"这一
  结构事实本身已足够支撑 §2 的时序结论。）
- **`vm_map_guard_exception` 未在 IDB 定位**：内核无符号，且该函数缺少唯一字符串锚点。
  **判定：不阻塞** —— E2 需要的是 `task->task_exc_guard` 的**位**（已由源码确认，见 §7.4）与
  该字段的**偏移**（§7.5 未解），而不是这个函数的地址；定位它不会改变任何结论。
  将来若需要，可经 `vm_map.c` 的 panic 字符串簇做 xref 收敛。

### 7.7 Phase 2 小结

| 计划项 | 结果 |
|---|---|
| 定位 `vm_shared_region_create` | ✅ `sub_FFFFFE0008060FD0`，与源码逐条对上（§7.1） |
| 定位 `size` 的赋值指令 | ✅ **`0xfffffe0008061160` = `MOVZ X20,#1,LSL#32`**（字节 `34 00 C0 D2`）（§7.2） |
| `vm_map_exec` 内的调用点 | ✅ 间接：`vm_shared_region_enter` 唯一 xref = `0xfffffe000802d4b4`（§7.6） |
| `vm_map_guard_exception` 实现 | ⚠️ 未定位（无唯一锚点）；**不阻塞**，且不影响任何判定（§7.6） |
| 附带 | `vm_shared_region` 字段偏移表（§7.3）、`TASK_EXC_GUARD` 位更正（§7.4） |
