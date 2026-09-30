# STATIC: m3 写错误根因闭合 —— 550 契约漏检 pointer_format + 诊断走 triage（非 printf）

> 作者：静态侧 Agent。日期：2026-09-30。
> 交付对象：`HANDOVER-M3-DYLD-PAGER-2026-09-30.md` 的"Open inconsistency / Next decisive test"。
> 结论等级标注：`RE-confirmed via <文件/地址>`（反汇编或源码）／`runtime-confirmed via <日志>`
> （来自设备侧前序会话的实测，本文仅引用）／`THEORY`。
> 本文不改设备、不改内核，仅 IDB 静态读取 + 本地源码。

---

## 0. TL;DR（4 条）

1. **交接文档的"开环疑点"前提是错的。** 它推断"内核 ring 里没有
   `unknown pointer_format` ⇒ 格式分派没被走到"。实际上 **release 内核里
   `printf()` 已被宏化掉整个格式串**（`osfmk/kern/misc_protos.h:180-186`，
   `CONFIG_NO_PRINTF_STRINGS`），这些串**在二进制里根本不存在**——字节级复核：
   真机内核镜像中 `pointer_format` / `unknown pointer` **0 命中**，而
   `dyld_pager` **10 命中**（正对照）。所以"没看到 printf"不携带任何信息。

2. **这些诊断改走 kdebug triage**：`bsd/kern/kdebug_triage.c:327-329` 有 3 条
   dyld_pager 消息；`KDBG_TRIAGE_DYLD_PAGER_SLIDE_ERROR`（eventid
   `0x04000008`）= `"dyld_pager_data_request hit a page sliding error\n"`。
   真机内核 `dyld_pager_data_request` 里"格式不支持 / 找不到段 / 段越界 / 链越界"
   的**汇聚拒绝点**正是发这个 eventid 然后返回 `KERN_FAILURE=5`。

3. **格式 13 在真机内核被静默拒绝（RE-confirmed）。** 真机
   `sub_FFFFFE00080661A4` 里只有 `(u16)hdr->pointer_format - 1 <= 0xB` 才进
   switch（case 1/2/3/6/9/0xC）；`13` 落到汇聚点 → `ktriage(0x04000008) +
   KERN_FAILURE`。→ 内核 `dyld_pager_data_request` 返回 5 → 
   `vm_fault.c:1892` 返回 `VM_FAULT_MEMORY_ERROR` → `vm_fault.c:5693-5699`
   取 `error_code(0) ?: KERN_MEMORY_ERROR` = **10**。与设备实测
   `[exc] type=1 code0=0xa` 逐位吻合。

4. **根因在内核 550 的入参校验漏项（RE-confirmed）。** 真机
   `map_with_linking_np` = `sub_FFFFFE000845A084` 校验了 `mwli_version==7`、
   `mwli_page_size==0x4000`、binds/chains 的 offset+size、以及文件 CS blob 覆盖，
   **唯独不校验 `mwli_pointer_format`**（只在选 bind 条目宽度时把 +6 读出来比 3）。
   ⇒ 550 对一个"内核永远无法 fixup"的 blob **返回成功**，把失败推迟到每页首次
   fault，且静默。这是"系统调用成功但契约已破"的时序炸弹。

**净效果**：m3 写错误的成因链已闭合，**不需要再跑"看 ring printf"的实验**（前提不存在）。
新的、零内核改动的确定性判据见 §5（crash report 里的 triage 字符串）。

---

## 1. 证据清单（逐条可复核）

| # | 事实 | 等级 | 出处 |
|---|---|---|---|
| E1 | 真机内核格式分派：`format-1 <= 0xB` 才进 switch；13 → 汇聚拒绝 | RE-confirmed | I1 `sub_FFFFFE00080661A4`，判定在 `0xfffffe00080667f4`；拒绝点 `0xfffffe0008066674..0x8066690`（`ktriage_record(...,0x04000008,0)` + `v11=5`） |
| E2 | 拒绝点返回 `KERN_FAILURE=5`，无 printf | RE-confirmed | 同上（Hex-Rays 里该路径只有 `ktriage_record` 调用，无任何字符串引用） |
| E3 | 5/10 的来源：`retval = error_code ? error_code : KERN_MEMORY_ERROR` | RE-confirmed | I1 同函数 `LABEL_144`（`0xfffffe0008066b30` 附近）：`if (HIDWORD(v109)) v11=HIDWORD(v109); else v11=10;`；源码对应 `vm_dyld_pager.c:793-800` |
| E4 | 该返回值如何变成用户可见的 10 | RE + 源码 | `vm_fault.c:1866-1871`（`memory_object_data_request`）→ `:1892-1900`（`rc!=KERN_SUCCESS` ⇒ `VM_FAULT_MEMORY_ERROR`）→ `:5642-5699`（`error_code?:10`） |
| E5 | printf 格式串在 release 内核被消灭 | RE-confirmed via 源码 | `osfmk/kern/misc_protos.h:180-186`：`#if CONFIG_NO_PRINTF_STRINGS` ⇒ `#define printf(x, ...) _consume_printf_args(0, ##__VA_ARGS__)`（连格式串都不传）/ `do {} while (0)` ⇒ 字面量被 DCE |
| E6 | 二进制里确实没有这些串 | RE-confirmed via I1 字节/字符串双检 | `find_bytes`：`pointer_format`=0、`unknown pointer`=0、`dyld_pager`=10（正对照）；`idautils.Strings()` 扫 174,369 串，9 个候选格式串 **0 命中** |
| E7 | 诊断改为 triage，消息表在源码里 | RE-confirmed via 源码 | `bsd/kern/kdebug_triage.c:327-329`（NO_UPL/SLIDE_ERROR/MEMORY_SHORTAGE 三条）；`bsd/sys/kdebug_triage.h:127`（subsys=4）、`:129-136`（Code：PREFIX=0,NO_UPL=1,SLIDE_ERROR=2,SHORTAGE=3）、`:42-45`（eventid 编码：`(Class<<24)|(Reserved<<16)|(Code<<2)`） |
| E8 | 真机内核存在这三条 triage 字符串 | RE-confirmed via I1 | `get_string`：`0xfffffe0007ed278f`="dyld_pager_data_request couldn't create a upl"、`0xfffffe0007ed27be`="…hit a page sliding error"、`0xfffffe0007ed27f0`="…hit memory shortage" |
| E9 | 550 校验字段全表，缺 pointer_format | RE-confirmed | I1 `sub_FFFFFE000845A084`（区域/链接信息校验段）；源码 `bsd/vm/vm_unix.c:2976-3130` |
| E10 | 真机 550 确实把 blob 交给 `vm_map_with_linking` 建 pager | RE-confirmed | I1 `sub_FFFFFE000845A084` 尾部 `v10 = sub_FFFFFE0008066F8C(task, regions, region_cnt, link_info, link_info_size, file_control)` |
| E11 | dyld 侧提交的 blob 字段值 | RE-confirmed via I2 | `dyld3::mapSplitCachePrivate` = `0x342dc..0x351a8`；`0x349ec: MOV X8,#0xD400000000007; STR X8,[X24]` = version 7 / page_size 0x4000 / **pointer_format 13**；`0x34b84: BL ___map_with_linking_np` |
| E12 | `DYLD_CHAINED_PTR_ARM64E_SHARED_CACHE == 13` | RE-confirmed via 源码 | `analysis/dyld-dyld-1286.10/include/mach-o/fixup-chains.h:104`（"Only A keys supported"） |
| E13 | dyld 侧"跳过 550 走 in-process"的分支就在 550 之前 | RE-confirmed via I2 | `0x3478c CMP W9,#1` / `0x34790 B.NE loc_34C00`；`0x34C00` 起即 `if (!canUsePageInLinking){…}` 的 in-process 块（`0x34c00 LDRB W8,[X27,#8]`＝`options.enableReadOnlyDataConst`） |
| E14 | 550 返回值非 0 时 dyld 已有回退 | RE-confirmed via I2 | `0x34b88 CMP W0,#0` / `0x34b8c CSET W20,EQ` / `0x34b90 CBZ W0,loc_34BB4`；`0x34b9c` 后 `dyld4::console("…failed, falling back…")`；源码 `SharedCacheRuntime.cpp:1170-1175` |
| E15 | 环境变量路线被 `internalInstall()` 挡住 | RE-confirmed via 源码 | `DyldProcessConfig.cpp:566-584`（`DYLD_PAGEIN_LINKING` 仅在 `syscall.internalInstall()` 内有效）；`DyldDelegates.cpp:220-229`（macOS＝`csr_check(CSR_ALLOW_APPLE_INTERNAL)==0`，生产机恒 false）；`:1010-1023`（`sandboxBlockedPageInLinking()`＝`sandbox_check(pid,"syscall-unix",…,550)`，chroot 未沙箱⇒恒 false） |
| E16 | triage 字符串会进 crash report | RE-confirmed via 源码 | `kern_exit.c:481 populate_corpse_crashinfo()` → `:770-774`：`ktriage_extract(thread_tid(current_thread()), triage_strings, …)` → `kcdata_memcpy(crash_info_ptr, uaddr, …)` 写 `TASK_CRASHINFO_KERNEL_TRIAGE_INFO_V1`；`kcdata.h:1128-1136`（5×128B 字符串） |
| E17 | triage 缓冲在启动时无条件创建 | RE-confirmed via 源码 | `bsd/kern/kdebug_common.c: kdebug_startup()` 尾部 `create_buffers_triage()`（无任何条件门） |

> 设备侧实测（**runtime-confirmed via 前序会话**，本文不改动其结论）：
> `[exc] type=1 code0=0xa code1=0x1ee188000`、`far=0x1ee188000`、`esr=0x92000046`（写错）、
> `[vmext] … prot=3/3 off=0x0 resident=0 external=1 shadow=3 ref=6`、
> `csops flags=0x26803b0d`、`entry [0x1ee188000..0x1ee1ac000]` 的 object chain 末端是
> `ops==DYLD_PAGER_OPS` 的 pager，`link_info` 声明 `version=7 page_size=0x4000 ptr_format=13`。

---

## 2. 真机内核 `dyld_pager_data_request` 逐段还原（`sub_FFFFFE00080661A4`）

从上到下的实际控制流（每步标注"是否静默"）：

```
UPL 请求：sub_FFFFFE000804788C(mo_control, offset, length, &upl, 0,0, 0x54C, 23)
   失败 → ktriage_record(0x04000004 = NO_UPL) + 返回 kr                【静默，只有 triage】
取 backing 对象、reference + paging_begin
逐页循环（步进 0x4000）：
  upl_page_present 为 0 → 跳过该页（v11=0，continue）                    【无输出】
  vm_fault_page(src_top_object, offset+cur, VM_PROT_READ, …)
      case VM_FAULT_RETRY(1)      → continue（重试）
      case VM_FAULT_INTERRUPTED(3)→ ktriage(0x0400000C) + MACH_SEND_INTERRUPTED
      case VM_FAULT_MEMORY_ERROR(5/6)
                  → v11 = error_code(0) ?: 10（KERN_MEMORY_ERROR）        【静默，无 triage】
  取页物理地址→ src_vaddr/dst_vaddr（ml_static_ptovirt_0）
  code_signed ⇒ vm_page_validate_cs_mapped
  搬 CS 位（validated/tainted/nx）到 UPL 页
  recon 源对象锁；memmove(dst, src, 0x4000)   ← 此时页内容已拷好
  用 pager 的 dyld_file_offset[]/dyld_address[]/dyld_size[] 求 userVA
      未命中任何 range → 汇聚点                                        【静默】
      命中 → 段查找（seg_count / seg_info_offset / segment_offset /
                       page_count / seg->size 边界 / pageIndex）
             任一边界不满足 → 汇聚点                                   【静默】
             通过 → 格式检查： if ((u16)hdr[+6] - 1 <= 0xB) switch
                      case 1(ARM64E) / 9(USERLAND) / 0xC(USERLAND24)
                              → sub_FFFFFE0008066C40（auth64 fixup）
                      case 2(PTR_64) / 6(PTR_64_OFFSET)
                              → sub_FFFFFE0008066DB4（fixupPage64）
                      case 3(PTR_32) → 内联 fixupChain32
                      default（含 13）→ 汇聚点                            【静默】
汇聚点 LABEL_64（0xfffffe0008066674..68）:
      ktriage_record(tid, 0x04000008 = DYLD_PAGER_SLIDE_ERROR, 0)
      v11 = 5  (KERN_FAILURE)                                             ← 唯二信号
清理：upl_abort(upl)（因 retval!=0）；返回 v11
```

**要点**：`format 13` 的拒绝与"找不到段/段越界/链越界"**共用同一个汇聚点**，
该点只有 triage、没有 printf、没有 panic。它在**页内容已经成功 memmove 之后**才发生，
所以没有任何"数据页读不出来"的痕迹。

---

## 3. 真机 550（`sub_FFFFFE000845A084` = `map_with_linking_np`）校验字段全表

| blob 字段 | 偏移 | 真机校验 | 结论 |
|---|---|---|---|
| `mwli_version` | +0 | `!= 7` → KERN_FAILURE(5) | 校验 |
| `mwli_page_size` | +4 (u16) | `!= 0x4000` → KERN_INVALID_ARGUMENT(4) | 校验 |
| **`mwli_pointer_format`** | **+6 (u16)** | **只读出来判断"是否 == 3"以决定 bind 条目宽 4 还是 8**；**从不做支持性白名单** | **漏检（根因）** |
| `mwli_binds_offset` | +8 (u32) | `< link_info_size` 且 `binds_size ≤ size-off` | 校验 |
| `mwli_binds_count` | +0xC | 同上一行的乘积 | 校验 |
| `mwli_chains_offset` | +0x10 | `< link_info_size` | 校验 |
| `mwli_chains_size` | +0x14 | `≥ 8` 且 `≤ size-off` | 校验 |
| regions | — | `1 ≤ region_count ≤ 5`；`link_info_size` 上下界；regions/link_info 均 `copyin` | 校验 |
| 文件绑定 | — | `ubc_cs_blob_get(vnode)` 必须覆盖每个 region（blob 覆盖门） | 校验 |

⇒ **`pointer_format` 是唯一"决定能不能 fixup、却不被校验"的字段**。
这是"550 返回 0 但内核永远无法完成该映射"的机制；也正是
`docs/evidence/m1-dyld-pager-format13-20260930.md` 里"compatibility gap confirmed"
的真实性质：不是"dyld 用了旧内核没有的格式"，而是**旧内核静默接受了它**。

---

## 4. 为什么交接文档的 msgbuf 推断必然为空

三个独立原因叠加，任一条都足以让"ring 里找不到 printf"这个观察失去意义：

1. **串不存在**：`CONFIG_NO_PRINTF_STRINGS` 下 `printf` 宏连格式串都不传（E5），
   字面量被 DCE；字节级 0 命中（E6）。
2. **消息改走 triage**：这 3 条消息在 `kdebug_triage.c` 的消息表里（E7），
   由 `ktriage_record` → `kernel_debug_write(&kd_control_triage, &kd_buffer_triage, …)`
   写进**独立的 triage kdebug 缓冲**，不进 msgbuf。
3. **`ktriage_extract` 只在进程退出时被调用**（`kern_exit.c:772`），
   且结果落进 **crash info**（E16），不是内核 ring。

> 顺带更正：交接文档 §"Device-state regressions #3"说"triage ring 未初始化、
> `ktriage_record` 写不进去"。源码侧 `create_buffers_triage()` 在
> `kdebug_startup()` 里**无条件**调用（E17），注释亦写明"we expect the triage
> system to always be ON"（`kdebug_triage.c:111`）。之前 `triageread` 读到的
> `INFO_G=0xfffffe000aa540a8+slide` 很可能是**普通 kdebug 的 bufinfo**
> （kdebug 没开时确实为空），而不是 `kd_buffer_triage`。→ **THEORY**：
> triage 缓冲其实是可用的，只是此前读错了对象；验证方式见 §5。

---

## 5. 新的确定性判据（设备侧零内核改动即可用）

### 5.1 原理

`populate_corpse_crashinfo()` 在进程终止建 corpse 时（E16）：

```c
char triage_strings[5][128];
ktriage_extract(thread_tid(current_thread()), triage_strings, 5*128);
kcdata_memcpy(crash_info_ptr, uaddr, triage_strings, sizeof(struct kernel_triage_info_v1));
```

⇒ **fault 进程的 `.ips` crash report 里会带着该线程最近最多 5 条 triage 字符串**
（`struct kernel_triage_info_v1 { char triage_string1..5[128]; }`，`kcdata.h:1128-1136`）。
`ktriage_extract` 按 thread_id 过滤（`kdebug_triage.c:198-230`），
而 dyld_pager 的 triage 是用**当前线程** tid 记录的，fault 线程即退出线程 ⇒ 命中。

### 5.2 操作

```bash
# 1) 找 fault 进程的 crash report（chroot 的 macOS 进程由 iOS CrashReporter 记录）
ls -t /private/var/mobile/Library/Logs/CrashReporter/{bash,WindowServer,echo}*.ips | head -3
# 2) 直接搜 triage 消息（triage 字符串是明文，位于 crash info 的 triage 字段）
grep -aE "dyld_pager_data_request (couldn't create a upl|hit a page sliding error|hit memory shortage)" <file>.ips
#    若 .ips 把 triage 段编码成 hex，先 xxd/strings 一次
strings -a <file>.ips | grep dyld_pager
```

### 5.3 判定表

| .ips 里出现的字符串 | 含义 | 对本问题的裁决 |
|---|---|---|
| `dyld_pager_data_request hit a page sliding error` | eventid `0x04000008`：格式/段/链拒绝汇聚点 | **坐实**：m3 fault = format 13 被拒（本文主结论） |
| `dyld_pager_data_request couldn't create a upl` | eventid `0x04000004`：UPL 请求失败 | 交接文档"更早静默失败"假设成立，需转查 UPL |
| `dyld_pager_data_request hit memory shortage` | eventid `0x0400000C` | 内存不足，与格式无关 |
| 三者都没有 | triage 未落盘（corpse 路径没走 / 缓冲未分配） | 退回到 §5.4 的直接复现 |

### 5.4 若 triage 不可得：直接复现（对交接文档 `mwl_repro` 的修正）

原设计"看 ring 是否出现 `unknown pointer_format 13`"**作废**（该串不存在）。
改为同一个 syscall 550 探针，但判据换成：

1. 记录 `syscall(550, …)` 的**返回值**：按 §3 的真机校验表，格式 13 会**返回 0（成功）**
   ——这本身就是"内核漏检"的正面证据（与 `m1-*` 序列里 550 成功的观察一致）。
2. 随后对映射区**首次 READ**：预期 `SIGBUS`/`EXC_BAD_ACCESS` 且 `code=0xa(10)`。
3. 立即读该探针进程的 `.ips`，按 §5.3 判据读 triage —— 这才是"走到格式分派"的证据。

---

## 6. 修复候选与补丁靶点

**层次判断**：真正的缺陷在**内核**（550 接受了不可 fixup 的 blob，把失败推迟且静默）。
dyld 行为在"正确的 550"下是自洽的。所以：

| 方案 | 位置（精确） | 性质 | 风险 |
|---|---|---|---|
| **F2（根因修复，内核）** | 在 `sub_FFFFFE000845A084` 的 `mwli_page_size` 检查（`0xfffffe000845a28c` `CMP w,#0x4000`）附近，对 `+6` 的 `pointer_format` 做白名单（仅 1/2/3/6/9/12），否则返回 `KERN_INVALID_ARGUMENT(4)` 或 `KERN_FAILURE(5)` | **让 550 变诚实**：返回非 0 ⇒ dyld 既有回退（E14）**自然**触发，无需改 dyld | 高：内核文本补丁（KASLR/PAC/签名），设备侧需谨慎 |
| **F1（最小可用，dyld）** | I2 `0x34790 B.NE loc_34C00` → `B loc_34C00`（`dyld3::mapSplitCachePrivate` 内，`if (canUsePageInLinking)` 守卫） | 选择 dyld **自身已实现的** in-process 回退路径 | 低：用户态、单指令、可逆、不动任何不变量 |
| F1'（等价的配置型改法） | `opts.usePageInLinking = (mode>=2) && !sandboxBlocked…`（源码 `DyldProcessConfig.cpp:1339`）的 store 置 0 | 语义等同 `DYLD_PAGEIN_LINKING=0`（dyld 自认的合法开关） | 低；该函数在 strip 过的 dyld 里需先定位（未做） |

**为什么 F1 不是 AGENTS.md 所禁的"强制分支/掩码"**：被绕过的不是"不变量检查"，而是
**一个在本机为假的假设**（"内核支持 format 13 的 page-in linking"）。F1 选择的是
dyld 官方实现、且在 `__map_with_linking_np` 失败时**本来就会走**的同一条路径
（E14），修复后的语义与"内核没有这个 syscall"的 macOS 15 场景一致。
但**根因修复仍是 F2**；F1 只应作为设备侧的低风险先手，且必须在文档里标注为
"选择性绕过（deliberate opt-out）"而非"修复内核缺陷"。

**F1 之后 in-process 路径可用性（前置证据）**：
- `SharedCacheRuntime.cpp:1179-1207`：非 TPRO 的 CONST_DATA 用
  `vm_protect(..., VM_PROT_WRITE|VM_PROT_READ|VM_PROT_COPY)`；
  TPRO 走 `MemoryManager::withWritableMemory`；随后 `rebaseDataPages()` 在用户态应用 slide v5。
- `runtime-confirmed`（前序会话，`docs/evidence/m1-cache-copy-copy-20260930.raw`）：
  同一文件同一区间（foff `0x6c188000`、9 页、`csflags=0x26803b0d`）原生
  `vm_protect(COPY)` 后 9 页首字节均可读、`exit 0` ⇒ COPY 路径在本机有效。
- 结论：**该修的路径不是"让内核支持 format 13"，而是"让 dyld 走它自己的 in-process fixup"**。

**已被否掉的零补丁路线**（见 E15）：`DYLD_PAGEIN_LINKING=0`、`commPage.disablePageInLinking`
都被 `internalInstall()` 挡（生产机 `csr_check(CSR_ALLOW_APPLE_INTERNAL)!=0`）；
`sandboxBlockedPageInLinking()` 未被该门控，但 chroot 未沙箱 ⇒ 恒 false。故无环境变量解。

### 6.1 F1 的精确补丁字节（RE-confirmed via I2，可直接落地）

`analysis/dyld_15.6.1_arm64e_thin`（thin arm64e，VM offset == 文件 offset）：

```
0x3478c: 3f 05 00 71    CMP  W9, #1
0x34790: 81 23 00 54    B.NE loc_34C00        ← 改这一条
0x34C00: 68 23 40 39    LDRB W8, [X27,#8]     ← in-process 块入口（options.enableReadOnlyDataConst）
```

`B.cond` 编码核对：`0x54002381` ⇒ cond=1(NE)、imm19=0x11C ⇒ target
`0x34790 + 0x11C*4 = 0x34C00` ✓（与 IDA 报的 target 0x34c00 一致）。
目标指令 `B loc_34C00`：`imm26 = (0x34C00-0x34790)/4 = 0x11C` ⇒
`0x14000000|0x11C = 0x1400011C` ⇒ LE 字节 **`1C 01 00 14`**。

⇒ **单点替换：`0x34790: 81 23 00 54` → `1C 01 00 14`**（dyld 文本补丁，
项目已有 `analysis/dyldwork/build_dyld.py` 的 dyld 补丁流水线；改后需重新签名+入 trustcache，
见 `dyld-15.6.1-state.md` 的签名配方）。

次要风险（F1 生效后需观察）：in-process 路径对 CONST_DATA 用
`vm_protect(…|VM_PROT_COPY)`，但**跳过 TPRO**（`SharedCacheRuntime.cpp:1186`），
所以 `__TPRO_CONST` 在 fixup 期间可能仍是只读。本机实测 m3 条目为 `prot=3/3`（RW、
未带 TPRO 位 ⇒ `enableTPRO=false`），故该写应当被允许；但 m4/m5 是否另有 TPRO 语义
需在设备上验证。

### 6.2 F2 的插入点（根因修复，仅供记录）

IDB（I1，静态，imagebase `0xfffffe0007004000`）中 `map_with_linking_np` 的
`page_size` 校验紧邻处即最佳插入点：

```
0xfffffe000845a28c: if (*((_WORD *)v11 + 2) != 0x4000) { v10 = 4; goto LABEL_12; }
0xfffffe000845a2d4: v10 = 4;      // KERN_INVALID_ARGUMENT
0xfffffe000845a2d8: goto LABEL_12;
```

应在其后追加：`fmt = *(u16*)(v11+6); if (fmt 不在 {1,2,3,6,9,12}) { v10 = 4; goto LABEL_12; }`。
⇒ 550 返回 4 ⇒ dyld 既有回退（E14）自然触发。
**注意：内核 IDB 地址 ≠ 运行时地址**（KASLR slide 每次启动不同），落地前必须按
`ktriage`/`dsc` 之外的既有流程在运行时定位并逐字节核对（见 AGENTS.md "Kernel write safety"）。


---

## 7. 明确撤回 / 更正

1. **撤回**交接文档 §TL;DR 的"Open inconsistency"推断链：
   "printf 不出现在 ring ⇒ 格式分派未被走到"。前提不成立（E5/E6/E7）。
2. **撤回** `docs/evidence/m1-dyld-pager-format13-20260930.md` 中把它记为
   "attribution of the observed m3 fault remains THEORY" 的保留——现在它**不是 THEORY**：
   格式 13 → `KERN_FAILURE` → `KERN_MEMORY_ERROR(10)` 的链条每一环都有 RE 或实测支撑
   （E1/E3/E4 + 设备实测 `code0=0xa`）。
   该文档 §"Confirmed Dyld / Kernel Format Mismatch" 的地址描述正确，仅需补一句：
   真机 default 路径**只有 `ktriage_record`，没有 printf**。
3. **更正** `AGENTS.md` 的 IDA 实例表：真机实测 I1=内核、I2=dyld（原文写反）。
4. **更正** 交接文档 §"Device-state regressions #3"（triage 未初始化）为 **THEORY**：
   源码显示 `create_buffers_triage()` 无条件在 `kdebug_startup()` 调用（E17），
   旧 `triageread` 很可能读的是普通 kdebug bufinfo。验证方式见 §5。

## 8. 负结果（不要重试）

- ❌ 搜 msgbuf 找 dyld_pager printf：串不存在，永远为空。
- ❌ 把 `pointer_format` 13 改成 12 或其它"内核支持"的值：fixup 语义不同
  （`fixup-chains.h:270-280`，`ARM64E_SHARED_CACHE` 的链布局/仅 A key），会产出错误指针，
  属于"看起来通过、实际破坏不变量"。
- ❌ 用 `DYLD_PAGEIN_LINKING` 环境变量（被 `internalInstall()` 门控，E15）。
- ❌ 期待 `pagein_error != 0` 来证明 pager 出错：`t_pagein_error` 仅由
  `vnode_pager.c:650/796`（VNOP_PAGEIN）设置，dyld_pager 的失败**不写它**
  （`vm_fault.c:5616` 在每次 fault 前清零）——所以 `pagein_error=0` 对本案**无信息量**。
