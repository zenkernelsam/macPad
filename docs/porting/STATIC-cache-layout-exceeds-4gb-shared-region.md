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

## 7. `DYLD_SHARED_REGION` 两格实测（2026-09-30，F1 在位，均已回滚并复核 SHA/inode）

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

