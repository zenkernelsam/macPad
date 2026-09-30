# KB：路线 E —— iOS 共享区 4 GB 限制的解除可行性（完整知识库）

> 建立日期：2026-10-01。来源：xnu-8792.81.2 源码 + 内核 IDA(IDB kc_raw_16.3_T8112.bin) +
> 项目既有 RE 记录 + 三路并行调研。
> 级别标注：`RE-confirmed`（源码/反汇编/原始崩溃字段）／`runtime-confirmed`（设备实测）／`THEORY`。

---

## 0. 结论速览

| 变体 | 判定 | 一句话依据 |
|---|---|---|
| **E1 放大 `SHARED_REGION_SIZE_ARM64`** | ❌ **不可行** | 是**编译期常量**（要写 text）；区域在**开机首个 exec** 就建立；且项目**无内核 text 写成功先例**、文档记 KTRR/PPL 下 text 写挂死 |
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
