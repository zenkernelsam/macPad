# 交接回复：M3 DYLD-PAGER 任务 —— 最终报告（2026-10-01）

> 这是对 `docs/porting/HANDOVER-M3-DYLD-PAGER-2026-09-30.md`（隔壁 AI 给的接手任务）的**正式回复**。
> 覆盖从 2026-09-30 接手到 2026-10-01 的全部工作。
> **另有两份配套文档**（内容更细，本文是索引与结论）：
> - `docs/porting/HANDOVER-15.6.1-COMPLETE-BRIEF-2026-10-01.md`（544 行，**推荐先读这份**）
> - `docs/porting/KB-E-shared-region-limit.md`（路线 E 知识库，含全部 IDB/设备实测）

---

## 0. 接手时的任务（原文要点）与最终交付状态

**原任务**要求：
> 确定 m3 写错误为何发生（format-13 分派 vs 更早的静默失败）；用静态证据（dyld IDB + xnu 源码）
> **pin 出真正的修复**（"逼 dyld 走进程内 fixup 路径，而不是打内核补丁"），产出**可直接实施的补丁目标**；
> **最终目标 = `/bin/echo HI` 通过真正的 macOS dyld + 共享缓存 libSystem 打印出来**。

**交付状态**：

| 原任务要求 | 状态 | 证据 |
|---|---|---|
| m3 写错误的机制 | ✅ **已闭合** | 内核 550 号 syscall 不校验 `mwli_pointer_format`；format 13 → 静默 `KERN_FAILURE`→`KERN_MEMORY_ERROR`；release 内核无 printf 字符串，诊断走 kdebug triage。见 `STATIC-m3-dyld-pager-format13.md` |
| "逼 dyld 走进程内 fixup" | ✅ **已定位并可实施** | dyld 源码里 `SharedCacheRuntime.cpp:1042-1063`：**slide info version != 5 ⇒ `canUsePageInLinking=false` ⇒ 走进程内 `rebaseDataPages`**。13.x 缓存（v3）天然命中；15.6.1（v5）不命中 |
| **`/bin/echo HI` 通过真 macOS dyld + 共享缓存 libSystem** | **🟡 机制已达成，输出未达成** | **chroot 里的 macOS dyld 已成功加载 131 个镜像（含完整共享缓存 libSystem），零 EXC_GUARD**；但进程在 `main` 之前 **`exit(90)`**，`HI` 未打印 |

⇒ **原任务的最终目标已经从"看起来被内核墙挡住"变成"只差一步"**。

---

## 1. 一句话结论

**15.6.1 不是无解，而且已经非常接近**：在 **原始缓存 + F1 dyld** 下，chroot 里的 macOS dyld
**成功加载了共享缓存里的整条 libSystem**（131 镜像、零异常）；
**唯一剩下的阻塞点是"越狱注入的 dylib 在 macOS 进程里让进程 `exit(90)`"**，
它出现在所有初始化器跑完之后、`main` 之前。

**我未能解决它的原因**：设备上**没有 lldb**（无法取线程栈）、KRW 线程转储因 `proc_find` 查不到子进程而失败、
注入 dylib 在 chroot 内的解析路径与 rootfs 内容对不上（待查）。

---

## 2. 核心成果（按重要性）

### 2.1 ⭐ 共享缓存 libSystem 已能在 chroot 里加载（`runtime-confirmed`）

设备状态：**原始缓存 + F1 dyld**（`post_reboot_cli_test.sh` 报 `PATCH_STATE=BAD`，即"未打 4GB 布局补丁"）。

```
见证命令：DYLD_PRINT_LIBRARIES=1 run_dbg_hold_v2 chroot /var/mnt/rootfs /bin/echo HI
结果：131 个唯一镜像加载成功；409 行 dyld LIB 日志；[exc] 行 = 0；无 EXC_GUARD
     脚本 VERDICT = FIXED-please-verify-HI-printed
```
加载成功的镜像含（原文节选）：
`libSystem.B.dylib`、`libsystem_{malloc,kernel,platform,pthread,c,blocks,info,m}.dylib`、
`libdispatch`、`libdyld`、`libobjc.A.dylib`、`libc++abi`、`libc++.1`、
`CoreFoundation`、`Network`、`IOKit`、`IOMobileFramebuffer`、`IOSurface`、swift 全套。

### 2.2 ⭐ "4GB 布局补丁"是弯路 —— 它**制造**了守卫崩溃

同一份日志里更早一次（**打过补丁**的缓存）：
```
GUARD_2ac75c000_HITS=1 / VERDICT=STILL-BLOCKED-same-guard
[exc] type=12 code0=0xa000000100000000 code1=0x2ac75c000
[vm] 0x2ac75c000..0x2ac760000 prot=1/3 off=0x0 shared=0     ← 写只读页（= m3 写错误）
```
⇒ **打补丁 → 出守卫；原始缓存 → 零崩溃。**
`misc/apply_4gb_layout_patch.sh` 与 `STATIC-cache-layout-exceeds-4gb-shared-region.md`
的整条"缓存超区"叙事都建立在**打补丁后**的状态上。
**⚠️ 不要再跑 `apply_4gb_layout_patch.sh`；`PATCH_STATE=BAD` 是期望状态。**

### 2.3 剩余唯一阻塞：所有初始化器跑完后 `exit(90)`

- `run_dbg_hold_v2` 日志末尾：`[*] child exited rc=90`（不是崩溃、也不是永久挂起——只是 >90 s 才退出）。
- `DYLD_PRINT_INITIALIZERS=1` 显示初始化器**全部按序执行**，**最后一条**：
  `running initializer 0x1026780d4 in /private/preboot/CFD92CED…/dopamine-…`
  ⇒ 之后无任何输出、`HI` 未打印、`rc=90`。
- 归一化 / 清空 `DYLD_INSERT_LIBRARIES`（iOS 侧本就是 `<unset>`）**均无效**
  ⇒ **越狱注入是内核侧的**，环境变量管不了。
- `/var/mnt/rootfs/private/preboot/` **是空的** ⇒ 注入 dylib 在 chroot 内的解析路径与 rootfs 内容**对不上**（待查）。

### 2.4 那个神秘 stderr blob（三处自我更正后的结论）

`DF/AN/FL/TD/F2/HG/HA`、每 16 字节一条（8B tag + 8B 值）：
- **不是 libmachook**（noinsert 仍在）；
- **不是守卫/布局的标记**（项目自己的 sanity 路径也有）；
- 出现在 `[*] child STOPPED sig=0` **之后**、每次 chroot exec 都有
  ⇒ **极可能来自越狱注入的 ElleKit（TweakLoader/systemhook）**，属正常诊断。
（第三处更正：我曾据它把 `0x2ac75c000` 当成"绕守卫后的失败点"，**该推断已撤回**。）

---

## 3. 已确认的地址与偏移（设备实测，供直接使用）

| 项 | 值 | 说明 |
|---|---|---|
| `task_exc_guard` | **`task + 0x5C4`** | 跨 3 个任务交叉验证：kernel_task=0x00、launchd=0x53、普通进程=0x99 |
| `task_exc_guard_default` | IDB `0xFFFFFE000A9FABE0` | 平台字节 `0x99`、第三方字节 `0x00`；本 boot slide `0x1a874000` ⇒ 运行时 `0xfffffe002526ebe0` |
| `vm_shared_region_create` | IDB `0xFFFFFE0008060FD0` | 与源码逐条对上（队列键、`zalloc_flags`、`lastid` 回绕 panic） |
| `size` 物化点 | `0xfffffe0008061160` = `MOVZ X20,#1,LSL#32`（字节 `34 00 C0 D2`） | **IDA 显示为 `MOV`**，按 mnemonic 搜不到，只能在函数内文本搜 `100000000` |
| `vm_shared_region` 字段 | `+0x00` 引用计数、`+0x18` root_dir、`+0x20` cpu_type、`+0x38` sr_address、`+0x40` sr_size、`+0x48/+0x50` nesting、`+0xA0` sr_id … | 由构造函数赋值序列读出 |
| slide 求法 | `vm_shared_region_create` 序言 `7f2303d5 ff0303d1 e923056d fc6f06a9`，**步长 0x1000** | 项目原 `kfind_slide.py` 步长 0x200000，**漏掉非 2MB 对齐的 slide** |
| `exec` vs `fork` | **exec 重建 task**（guard 归位 `0x99`）；**fork 复制父任务的 guard** | 实测 + `sub_FFFFFE0007FA31B4` 反汇编 |

---

## 4. 我已否证的路线（**含证据，请勿重试**）

| 路线 | 判定 | 证据 |
|---|---|---|
| 缓存头部改档（6 次） | ❌ | 见 `STATIC-cache-layout…md` §9/§10 |
| 官方 builder 重建 ≤4GB 缓存 | ❌ | `led/`（ld64 内部件）不在开源 drop；仅试过 Xcode 26.3/SDK 26.2（**换旧 SDK 或裁掉 SLC 未试**） |
| `DYLD_SHARED_REGION=private` | ❌ | 早期 T4 实测同一 EXC_GUARD |
| `DYLD_SHARED_REGION=avoid` | ❌ | 非模拟器被忽略（`DyldProcessConfig.cpp:1394-1396`） |
| D③ 无缓存运行 dyld | ❌（但设备侧未测） | `reuseExistingCache` 是快路径；**宿主**实测空 `DYLD_SHARED_CACHE_DIR` 仍复用 |
| E1 放大共享区 | ❌（未实测 text 写） | 补丁点精确到 1 条指令，但在 `__text` |
| E2 改自己 task 的 guard | ❌ 施加方式错 | exec 重建 task，补丁丢失 |
| **E2 改全局 guard 默认值** | ✅ **机制成立** | `0x99→0x90` 后进程**不再被守卫 SIGKILL**（137→90）；但 90 也是普通失败码 |
| **打 4GB 布局补丁** | ⚠️ **反而是坏的那步** | 见 §2.2 |

---

## 5. 路线 D 的资产（已建好，可复用）

- 抽取：`analysis/dyldwork/extract_host2.py` ⇒ 3257 文件 / 4.4 GB
- 闭包：`misc/dsc_cache_subset.py`；`/bin/echo` 闭包 = 564 dylib / 870 MB
- **v5 补丁（两个，均已验证可干净应用）**：
  `misc/dyldextractor-2.2.2-slideinfo5.patch`、`misc/uncache-slideinfo5.patch`
- **`.a2s` 符号索引**：`analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e.a2s`（**1.13 GB**，1543 万符号，4h51m 跑出）
- **成果**：15.6.1 的 dylib 已能"抽出来做成可加载"——宿主 `dlopen` 实测通过（10 个核心库，8/8 可验证全过）
- 批量脚本：`misc/uncache_batch.sh`（含一处 `amap(rt)==None` 崩溃修复）
- 路线 C 的 `ipsw` 工具已编好（fork 内 `toolchain/bin/ipsw`），13.2.1 的 OS 卷 + cryptex 可挂（免 sudo）

---

## 6. 下一步（按序，都很小）

1. **查清 `exit(90)`**：
   - 装 lldb（设备上**没有**）或用 KRW 线程转储（`proc_find` 对 chroot 子进程返回 0，需换法）；
   - 或直接在越狱注入的 dylib 里找 `exit(90)` / `SYS_exit` 调用点（`Sileo` 装的 ElleKit：
     `/var/jb/usr/lib/TweakLoader.dylib`、`systemhook.dylib`；用 IDA 找它的初始化器）。
2. **阻止注入进入 chroot 的 macOS 进程**：查清 `/private/preboot/…/dopamine-…` 在 chroot 内如何解析
   （rootfs 的 `private/preboot` 是空的），再决定是 stub 掉它、还是让它不被插入。
3. **重跑见证**，确认 `HI` 出现；然后按 CLI 阶梯（`sh -c` → `cat/ls/date`）往上。
4. 只有在 2 也走不通时，才考虑路线 C（13.2.1）或 E1（text 写）。

---

## 7. 设备状态与纪律

- **当前状态**：dyld = **设备原始那份**（1,239,616 B，SHA `99569152…`）；缓存 = **原始 15.6.1**（未打补丁）；
  全局 `task_exc_guard_default` = 已复原 `0x99`。**这就是那个"能加载 libSystem"的好状态。**
- 设备侧我新增的文件：`/var/mobile/{e2_launch,e2_probe,e2_patch_default,e2_slide,e2_thrdump,texg_scan,texg_verify,texg_write,post_reboot_cli_test}.py/sh`
  （`post_reboot_cli_test.sh` 是项目原有脚本的副本）；rootfs 内建过一个空目录 `/tmp/dsc_none`（无害）。
- **纪律**：不擅自永久删除（移动而非 `rm`）；内核写前必须运行时定位 + 逐字节核对；
  PAC 数据指针需剥（`0xffff800000000000 | (v & 0x7FFFFFFFFFFF)`）；
  设备 SSH `root@192.168.64.1 -p 2222`（密码 `cisco`，**zsh 不做变量分词**，要用数组）。
