# STATIC: CLI 主障碍已定量 —— 15.6.1 缓存布局超出 iOS 4GB 共享区

> 作者：静态侧 Agent。日期：2026-09-30（goaf 第 1 轮）。
> 结论等级：`RE-confirmed`（反汇编/源码/实测字节）／`runtime-confirmed`（设备日志、crash report）／`THEORY`。

---

## 0. TL;DR

1. **15.6.1 的共享缓存总布局 = `0x180000000..0x2ac75c000`（≈4.77GB），而 iOS 16.3 的共享区是
   `0x180000000..0x280000000`（4GB）** ⇒ `.01` 子缓存从 `0x280000000` 往后的
   **≈745MB（`0x2c75c000`）落在共享区之外**（RE-confirmed，见 §1 映射表）。
2. 这与 m3 写错误、以及当前 `EXC_GUARD(DEALLOC_GAP)` **是同一根因的两个症状**；
   异常地址 `0x2ac75c000` 恰是 **`.01` 最后一个映射（m6）的末端**（`0x288dcc000+0x23990000`）。
3. 出错代码是 **dyld 自己**：pc 恒为 `dyld_base+0xae8`，该 16 字节序列
   （`e4 03 08 aa 05 00 80 d2 b0 18 80 d2 01 10 00 d4` = `mov x4,x8; mov x5,#0; mov x16,#0xc5; svc #0x80`，
   `x16=0xc5=197=mmap`）**在根文件系统中仅出现于 `/usr/lib/dyld`**（文件偏移 `0xa0c/0xa88/0xad8`）。
4. **负结果**：把 `.01` 文件藏起来**不改变故障** ⇒ 布局由**缓存头元数据**决定，不是"文件在不在"；
   要"只用主缓存"必须**重建缓存**（或改布局），不是挪文件。

---

## 1. 映射表（RE-confirmed，本地 `analysis/dyld-cache-15.6.1/`，与设备同 SHA）

主缓存 `dyld_shared_cache_arm64e`（`mappingOffset=0x228`, `mappingCount=8`）：

| # | VA | size | end | max/init |
|---|---|---|---|---|
| m0 | `0x180000000` | `0x67f5c000` | `0x1e7f5c000` | r-x |
| m1 | `0x1e7f5c000` | `0x1e90000` | `0x1e9dec000` | rw-/r-- |
| m2 | `0x1ebdec000` | `0x239c000` | `0x1ee188000` | rw-/rw- |
| m3 | `0x1ee188000` | `0x24000` | `0x1ee1ac000` | rw-/r-- ← **历史 m3 写错误受害者** |
| m4 | `0x1ee1ac000` | `0x1200000` | `0x1ef3ac000` | rw-/rw- |
| m5 | `0x1ef3ac000` | `0x7cc4000` | `0x1f7070000` | rw-/r-- |
| m6 | `0x1f9070000` | `0x5cdc000` | `0x1fed4c000` | r-- |
| m7 | `0x1fed4c000` | `0x268c0000` | **`0x22560c000`** | r-- |

`.01` 子缓存 `dyld_shared_cache_arm64e.01`（`mappingCount=7`）：

| # | VA | size | end |
|---|---|---|---|
| m0 | `0x22560c000` | `0x54808000` | `0x279e14000` |
| m1 | `0x279e14000` | `0x21c4000` | `0x27bfd8000` |
| m2 | `0x27dfd8000` | `0x38b4000` | `0x28188c000` ← **越界开始**（> `0x280000000`） |
| m3 | `0x28188c000` | `0xd90000` | `0x28261c000` |
| m4 | `0x28261c000` | `0x45d4000` | `0x286bf0000` |
| m5 | `0x288bf0000` | `0x1dc000` | `0x288dcc000` |
| m6 | `0x288dcc000` | `0x23990000` | **`0x2ac75c000`** ← **异常地址** |

共享区：`SHARED_REGION_BASE=0x180000000`、`SHARED_REGION_SIZE=0x100000000`（4GB）⇒
**越界量 = `0x2ac75c000 − 0x280000000 = 0x2c75c000` ≈ 745.5MB**（`.01` 的 m2..m6 全部或部分）。

---

## 2. 两条独立证据链（都指向 §1）

### 2.1 真实 crash report（runtime-confirmed，无 runner）
`/private/var/mobile/Library/Logs/CrashReporter/Retired/echo-2026-09-29-220513.ips`：
```
exception: EXC_BAD_ACCESS / SIGSEGV, KERN_INVALID_ADDRESS at 0x00000002ac75c000
vmRegionInfo:
  0x2ac75c000 is not in any region. ...
    unused __TEXT   1fed4c000-22560c000 [616.8M] r--/r-- SM=COW  ...ed lib __TEXT   ← 正是主缓存 m7
  --->  GAP OF 0xd9a9f4000 BYTES
    commpage (reserved) fc0000000-1000000000
```
⇒ `0x2ac75c000` 位于主缓存末端之后的**巨大空洞**里（既非缓存、也非保留区）。

### 2.2 当前故障（runtime-confirmed，逐次一致）
6 次运行（pristine/F1、echo/bash、含/不含 `.01`）异常都一样：
`EXC_GUARD` `type=12`、`code0=0xa000000100000000`（= `GUARD_TYPE_VIRT_MEMORY(5)` +
`kGUARD_EXC_DEALLOC_GAP(1)`）、**`code1(gap)=0x2ac75c000`**、`esr=0x56000080`（EC=0x15=SVC）、
`pagein_error=0`，`pc = dyld_base + 0xae8` 且 pc 处字节恒为上述 `mmap` 桩。

**机制（RE+源码）**：dyld 对缓存段做 `mmap(VA, …, MAP_FIXED|MAP_PRIVATE|0x40000, fd, 0)`
（`x3=0x40012`）；该 VA 区间落在"部分已映射/含空洞"的地址段时，
`vm_map.c:8693-8702` 走 `VMDS_FOUND_GAP` → `vm_map_guard_exception(gap_start, kGUARD_EXC_DEALLOC_GAP)`
→ 本机 `task_exc_guard` 使其**致命** ⇒ 进程被杀（`vm_reclaim.c:311-321` 是同一 reason 的另一产生点，
两者共用 `kGUARD_EXC_DEALLOC_GAP`，故 §9.6 的 reclaim 归因**不能排除**"MAP_FIXED 跨洞"这条；本条为准）。

---

## 3. 本轮设备变更与结果（Phase 0/A）

| 项 | 结果 |
|---|---|
| dyld 基线 | SHA `99569152…51a1`、inode `245791518` ✓（多轮实验后逐次复核一致） |
| `$R/dev` + devfs | **之前根本不存在**；`mkdir -p` + `/var/jb/usr/macOS/bin/mountdevfs $R/dev` → `mounted devfs`，**`/dev/ptmx` 出现** ✓（作者文档所述 pty 缺失问题已解） |
| cachereg | 原有 PID 2255（`/tmp/dsc/*`）；新增 PID 8817 覆盖 **cryptex 路径两个 inode**（内容与 `/tmp/dsc` 逐字节相同、inode 不同）⇒ 两条路径都被 CS blob 覆盖 |
| trustcache | 复核 dyld/libSystem.B.dylib(×2 切片)/libdyld.dylib(×2)/echo(×2) 均 `hit>0`；补挂 bash/sh/cat/ls/date/libmachook_arm64 各切片（`hit=1`）；共约 385 项 |
| 作者冒烟 `run_bash.sh -c echo __CHROOT_OK__` | **`Killed: 9`**，stdout 0B（`[launchdchrootexec] … insert=/usr/local/lib/libmachook_arm64.dylib`） |
| 裸 `chroot` | 同样 `Killed: 9` |
| `run_dbg_hold_v2 chroot $R /bin/echo|bash` | 能进 dyld（4 个进程打印 `libSystem.B.dylib`），随后即 §2.2 的 EXC_GUARD |

**关键判读**：`launchdchrootexec` **不调用** `jbctl proc_set_debugged`，而
`misc/run_dbg.c:297-351` 是"spawn suspended → `jbctl proc_set_debugged` → 装异常端口 → resume"。
⇒ 在本机（MACOS 平台二进制 + AMFI），**只有经 run_dbg 家族启动的 exec 能活**；作者的
`run_bash.sh` 路线之所以在我们 fork 上必挂，极可能就是缺这一步（**可修：给 launchdchrootexec 补
`proc_set_debugged`**，属独立改进，需设备 Theos 构建验证）。

---

## 4. 修复候选（按代价）

| 方案 | 做法 | 代价/风险 |
|---|---|---|
| **A. 缩容缓存使其 <4GB** | 用仓库 `cache_builder`/`analysis/dyldwork/build_dyld.py` 重建一个**主缓存单独**或**裁掉部分 dylib** 的缓存，令最高端 ≤ `0x280000000` | 中：需重建 4.9GB 级缓存并重挂 CS blob/TC；但**是唯一能"继续 15.6.1"的正路** |
| B. 扩充内核共享区 ≥4.9GB | 改 `SHARED_REGION_SIZE`（内核） | 高：内核文本补丁，本机已 panic 过一次，**不做** |
| C. 回退 macOS 13.x | 13.4 布局天然 <4GB，且作者设施齐备 | 低风险但换版本面（用户已选继续 15.6.1） |
| D. 藏 `.01` 文件 | —— | **已实测无效**（§0.4），不要再试 |
| E. 保持现状调 dyld | 让 dyld 把越界段映射到**别处**（改布局基数） | 高：要重算所有 slide/fixup，等于做 A |

---

## 5. 遗留/更正

1. **更正** `HANDOVER-REPLY-2026-09-30-upstream-merge-and-clues.md` §2.5：作者
   `ensure_jb_usr_bind.sh` 绑的是 **`/var/jb/usr → $ROOTFS/var/jb/usr`**（为 XPC 代理 bundle 的绝对路径），
   **与 `/usr/lib`、libSystem 来源无关**；原文"专门维护 /usr/lib 绑定脚本"是错的。
2. **更正** 本文档族 §9.6 的"reclaim"归因：`kGUARD_EXC_DEALLOC_GAP` 有两个产生点
   （`vm_map.c:8701` 用户态跨洞删除 / `vm_reclaim.c:603` 内核回收），本条实测更符合前者
   （pc 在 dyld 的 MAP_FIXED mmap 桩上）；两者都可用 §2.1 的 crash report 判据复核（该地址不在任何 region）。
3. 本地 `analysis/dyld_15.6.1_arm64e_thin`（IDB 输入）在文件偏移 `0xa0c/0xa88/0xad8` 处是**零**
   （设备原版是上述 mmap 桩）⇒ **IDB 的输入是上一会话的"highreserve/emptysr"类补丁变体**，
   引用其地址时须以设备原版 SHA 件为准（除本会话已逐字节核对过的点）。

---

## 6. 下一步（建议）

1. **先做 A 的最小验证**：用 `cache_builder` 生成"主缓存单独"（或裁到 <4GB）的 15.6.1 缓存 →
   重挂 CS blob + TC → 用 `run_dbg_hold_v2` 跑 `/bin/echo HI`，看 EXC_GUARD 是否消失、CLI 是否前进。
2. 顺带修 `launchdchrootexec` 补 `proc_set_debugged`（让作者的 `run_bash.sh`/`chroot_works` 路线可用），
   需 Theos 设备构建。
3. 每次实验保持：`mv`-only 换文件、原版 SHA/inode 双复核、每阶段更新 `dyld-15.6.1-state.md` 并 push。

---

## 9. B1（"主缓存单独"）三次尝试 —— **均无效**（2026-09-30，每次均已回滚并复核）

### 9.1 元凶候选字段实测

主缓存头字段（本地副本 = 设备副本，逐字节同）：
```
dynamicDataOffset  = 0x12c75c000   dynamicDataMaxSize = 0x4000
sharedRegionStart  = 0x180000000   sharedRegionSize  = 0x12c760000   (末端 0x2ac760000)
subCacheArray: offset=0x333e8 count=1 → entry[0] vmOffset=0xa560c000 (= 0x22560c000, 即 .01 起点)
sharedRegionStart + dynamicDataOffset = 0x2ac75c000   ← 与故障地址逐位一致
.01 的 m6: va=0x288dcc000 size=0x23990000 END=0x2ac75c000   ← 另一条同址线索
```

### 9.2 三次尝试（cryptex 与 /tmp/dsc 两份都改，改完立刻重挂 cachereg）

| # | 改动（`$CR` 与 `$DST` 两份主缓存头） | 结果 |
|---|---|---|
| B1-1 | 直接**藏起 `.01` 文件**（mv，不是删除） | 异常逐位相同（gap `0x2ac75c000`） |
| B1-2 | `dynamicDataOffset` → `0x77080000`（落在区内 32MB 空洞，VM 地址 `0x1f7080000`） | 相同 |
| B1-3 | `subCacheArrayCount` → 0 **且** `sharedRegionSize` → `0x100000000`（≤4GB）**且** `ddo` → `0x77080000` | 相同 |

三次运行的其他量均不变：`x0=0x2ac75c000`、`x2=5`(R+X)、`x3=0x40012`(MAP_FIXED\|PRIVATE\|SUPERPAGE_SIZE_ANY)、
`x4=3`(**fd 3**，即缓存文件)、`x16=0xc5`(197=mmap)、`pc=dyld_base+0xae8`、
`[vmext] 0x2ac75c000..0x2ac760000 prot=1/3 resident=0 external=1 shadow=1`。

### 9.3 结论与遗留问题（**这是下一轮的第一个待解问题**）

- 该 `MAP_FIXED` **可执行**映射的**目标地址与"哪份缓存/哪些头字段"无关**（三次改动皆无效）⇒
  地址 `0x2ac75c000` 由**其他地方**产生。现存的同址线索只剩两条：
  (i) **`.01` 自身映射表 m6 的末端**（`0x288dcc000+0x23990000`）；
  (ii) 主缓存声明区末端 `sharedRegionStart+sharedRegionSize` 的最后 16KB（该字段已改无效，故更可能是 (i)）。
- 因 `fd=3`（缓存文件）且 `PROT_READ|EXEC`，它**不是** dynamic-data（RW）⇒ 更像是 dyld 为
  **某段可执行内容**（`.01` 的某段 / atlas / TPRO 表）做的 `MAP_FIXED`。
- **下一步（B2 之前的最后一跳）**：用 IDA（I2，注意其输入是上一会话的变体，须以设备原版 SHA 件为准）
  或 `DYLD_PRINT_SEGMENTS`-类手段定位 dyld 里"对 fd 做 MAP_FIXED 可执行映射"的调用点，
  并核对它取地址的来源字段；或**直接改 `.01` 的头**（把其 m2..m6 重排进 ≤4GB —— 等于缓存手术）。
- 若这一跳仍不下，则 B2（重建 ≤4GB 缓存）或 C（13.x）就是必选项。

### 11. 已施加的补丁 + 重启验证流程（2026-09-30，等待重启）

**当前设备状态（已复核）**：dyld = 原版（SHA `9956…51a1`、inode `245791518`）；
5 个缓存文件**已打补丁且全部走新 inode**（原件以隐藏名保留在同目录，可一键还原）：

| 文件 | 新 inode | 补丁 |
|---|---|---|
| `$CR/dyld_shared_cache_arm64e` | 245847887 | `sharedRegionSize=0x100000000`、`subCacheArrayCount=0`、`dynamicDataOffset=0x77080000` |
| `$DST/dyld_shared_cache_arm64e` | 245847898 | 同上 |
| `$CR/dsc_main_orig` | 245847902 | 同上（第三个主缓存副本，先前搜索发现的） |
| `$CR/dyld_shared_cache_arm64e.01` | 245847907 | `m2.size=(0x280000000−m2.va)`、`m3..m6.size=0` ⇒ 有效范围止于 `0x280000000` |
| `$DST/dyld_shared_cache_arm64e.01` | 245847910 | 同上 |

每个文件都重挂过 CS blob（`cachereg … READY ok=1`）。

**工具（已入库，也已在设备 `/var/mobile/`）**：
- `misc/apply_4gb_layout_patch.sh apply|restore` —— 施加/还原（`mv`-only、原件保留）。
- `misc/post_reboot_cli_test.sh` —— **重启后一键**：校验补丁状态 → `restore_env.sh` 复原 TC →
  `mountdevfs` + 断言 `/dev/ptmx` → 对 4 个缓存文件重跑 `cachereg` → 部署 F1（已签名产物）→
  跑 `/bin/echo HI` 见证（带 `DYLD_PRINT_LIBRARIES`）→ 回滚原版 dyld 并复核 SHA。

**重启是验证的前提**：内核的 shared region 在创建时按当时缓存声明尺寸（`0x12c760000`）记录范围并跨进程持久，
所以缓存侧改动必须等 region 重建（= 重启）才生效。**重启会杀掉设备上正在运行的 10.8GB
`com.apple.Virtualization.VirtualMachine`（用户已授权）**；Dopamine 为 semi-untethered，重启后需重新越狱。

**重启后判据**（`post_reboot_cli_test.sh` 的输出）：
- 若 `[exc] type=12 code0=0xa…  code1=0x2ac75c000` **消失**且 `/bin/echo HI` 打出 `HI` ⇒ **B1 成功、CLI 里程碑达成**
  （届时把 F1 标注为"deliberate opt-out"，并按 §6 继续 r1..r3 阶梯）。
- 若仍是同一异常 ⇒ "region 状态"解释被否证，回头看 dyld 侧（IDA 定位 R+X `MAP_FIXED` 取址点）或改走 B2/C。

## 10. B1 追加三次尝试（新 inode！）—— 全部无效 ⇒ 地址来自**内核 region 状态**（2026-09-30）


第 9 节的三次尝试都是**原地改同 inode**，可能被内核的按-vnode 页/blob 缓存掩盖。本节三次全部改用
**新 inode**（`cp` → 改副本 → `mv` 就位 → 重新 `cachereg` 挂 blob，`READY ok=1`），仍逐位相同：

| # | 改动（新 inode） | 结果 |
|---|---|---|
| B1-4 | 仅 cryptex 主缓存：`sharedRegionSize→0x100000000` + `subCacheArrayCount→0` + `ddo→0x77080000` | 相同（gap `0x2ac75c000`） |
| B1-5 | **两份**主缓存同时做上面三字段（CR inode 245847338 / DST inode 245847459） | 相同 |
| B1-6 | **两份 `.01`** 的映射表裁剪（m2 size→`0x2028000` 使其末端=0x280000000；m3..m6 size→0） | 相同 |

**结论（证据闭环）**：故障地址 `0x2ac75c000` 与**两个缓存文件的任何元数据都无关**
（六次改动：文件在/不在、ddo、declared size、subCacheArray、`.01` mapping 表，全无效）。
唯一自洽的解释是：**该地址来自内核里已建立的 shared region 状态**——region 在**创建时**按当时
缓存的声明尺寸（`0x12c760000` ≈4.7GB）记录了范围，此后**跨进程持久**（`vm.shared_region_persistence=0`
但 iOS 侧进程一直持有，故不会被 destroy-delay 回收）。

**⇒ 直接推论（与项目文档一致）**：**任何缓存侧改动都必须先重建 region ⇒ 必须重启**。
这也解释了 state doc 里的"实测：shared region 跨进程持久；所以要换缓存必须重启"。

**附带事实**：
- `vm.shared_region_destroy_delay` 可写（120→5 实测成功，已还原 120），但因 iOS 侧持有 region，休眠不会回收。
- `JetsamEvent-2026-09-30-230519.ips`：设备内存压力主要来自一个 **10.8GB 的
  `com.apple.Virtualization.VirtualMachine`**（非本项目）—— `free` 仅 5540 页(≈90MB)、压缩器 ~3.9GB。
  **重启 iPad 会杀掉该 VM**，故重启需用户明确授权。
- 本轮全部设备改动均已回滚并复核：CR/DST 主缓存与 `.01` 的 inode/尺寸/字段恢复原值，dyld SHA `9956…51a1`
  + inode `245791518` 复核；无文件被删除（`mv`-only；备份见 §9.4 与本轮 `/.orig_*_v5|v6`、`/.f1*` 隐藏文件）。

### 9.4 本轮设备副作用清单（均已回滚/保留，未删除任何文件）


- 主缓存两份头的**头部页备份**：`/var/mobile/hdr_{CR,DST}{,_v2,_v3}.bak`（各 0x1000 字节）。
- `$CR`、`$DST` 两份主缓存头已恢复原值（`ddo=0x12c75c000`、`sharedRegionSize=0x12c760000`、`subC=1`，已复核）。
- dyld 恢复原版（SHA `99569152…51a1`、inode `245791518`，每轮均复核）。
- 新增 cachereg 实例若干（cryptex / tmpdsc / 补丁版），日志在 `/var/mobile/cachereg_*v*.log`。
- 实验用隐藏归档：`$R/usr/lib/.f1orig4..6`、`.f1tested4..6`（`mv` 换文件，未删）。
- `$R/dev` 已建并挂 devfs（`/dev/ptmx` ✓）。


**做什么**：把上一会话自建的探针 `misc/srteardown.c`（其注释已逐条描述本故障：
`mmap FIXED @0x2ac75c000 → gap zone: DEALLOC_GAP guard → SIGKILL`）在设备上补签+入 TC，
经 `run_dbg_hold_v2` 送进 chroot 执行。

**结果**：`/var/mnt/rootfs/tmp/srteardown` 的输出**一个字节都没有**（OUT 仅 runner 的
`Successfully marked proc of pid ... as debugged`），异常与 echo/bash 完全相同
（`type=12 code0=0xa000000100000000 code1=0x2ac75c000`、`pc=dyld_base+0xae8`、`x2=5`、`x3=0x40012`、`x16=0xc5`）
⇒ **它连 `main` 都没到，死在 dyld 的缓存映射里**。

**推论（RE + runtime）**：
1. 任何 chroot 内 macOS 二进制都会在 dyld 映射 `.01` 尾部时死掉 ⇒ 该故障**发生在 main 之前**，
   所以"改用户态代码/环境变量"无法绕过（A 路线不成立）。
2. 该地址区间**不是**"共享区在不在"的问题：`srteardown.c` 的设计本身已表明，`check_np(NULL)`
   清掉永久 region 后 `0x180000000` 可以 FIXED 映射，但 `0x2ac75c000` **仍然**触发
   `DEALLOC_GAP` ⇒ `[0x280000000, 0xfc0000000)` 是**内核守卫区**（共享区末端与 commpage 之间），
   macOS 任务的普通映射被禁止。
3. ⇒ 只有两条真出路：**(B) 让缓存布局（含 `.01`）整体落在 `≤0x280000000`**（缩容/重建/重排），
   或 **(C) 扩大内核共享区**（改 `SHARED_REGION_SIZE`，内核文本补丁，本机已 panic 过一次，**不做**）。
   "private/avoid 环境变量 + 清 region"这条便宜路线**到此为止，不要再试**。


**发现（RE-confirmed，dyld 源码）**：`DyldProcessConfig.cpp:1321` 读
`cacheMode = environ("DYLD_SHARED_REGION")`，`:1350`
`opts.forcePrivate = security.allowEnvVarsSharedCache && (cacheMode=="private")`；
而 `security.allowEnvVarsSharedCache` 来自 **AMFI 输出位 `AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE`**（`:938`）。
另有 `cacheMode=="avoid"` ⇒ `:1396` 跳过加载共享缓存。

| 实验 | 命令（要点） | 结果 |
|---|---|---|
| T4 | `DYLD_SHARED_REGION=private` + F1 + `/bin/echo HI` | **同一个 EXC_GUARD**（gap `0x2ac75c000`，pc=`dyld_base+0xae8`，`x16=0xc5`）⇒ private **不解决** |
| T5 | `DYLD_SHARED_REGION=avoid` + F1（对照） | **runner 自身 `Abort trap: 6`**，out/raw 皆空 ⇒ 该 env **确实被读取/放行**（T4 的"private"也应已生效） |

**判读（提高了机制精度）**：守卫的成因不是"地址落在区域外"，而是
**dyld 对 `.01` 尾部区间（`0x288dcc000..0x2ac75c000`）做 `MAP_FIXED` 时，该区间横跨
"已被 536 共享区映射（≤`0x280000000`）/ 未映射（>`0x280000000`）"的边界** ⇒
`vm_map.c:8693-8702` 的 `VMDS_FOUND_GAP` → `kGUARD_EXC_DEALLOC_GAP` 致命。
⇒ 真正的解要么**让布局不再跨界**（缩容/重建缓存，或让 `.01` 尾部整体落在区内/区外），
要么**消除"部分已映射"这一前提**（例：干净 region + private 让 dyld 自行整体 mmap；
本机 shared region 跨进程持久，需重启才干净——见 state doc 的"需重启拿干净 region"）。

