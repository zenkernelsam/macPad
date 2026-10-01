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

- **当前状态（我离开时）**：
  - `/usr/lib/dyld` = **设备原始那份**（**1,239,616 B**，SHA `9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`）
  - 缓存 = **原始 15.6.1**（主 `2,712,764,416` / `.01` `2,203,500,544`，**未打 4GB 布局补丁**）
  - 全局 `task_exc_guard_default` = **已复原 `0x99`**
  - F1 的 dyld 我**没有删**，移到 `/var/mobile/f1_leftovers_20261001/f1_active_dyld`（1,239,648）
- **复现"好状态"（能加载 libSystem 的那次）的精确命令**：
  ```bash
  R=/var/mnt/rootfs
  cp /var/mobile/dyld_f1_34790.bin "$R/usr/lib/.f1s_x" && chmod 755 "$R/usr/lib/.f1s_x"
  mv "$R/usr/lib/dyld" "$R/usr/lib/.f1o_x" && mv "$R/usr/lib/.f1s_x" "$R/usr/lib/dyld"
  DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_INITIALIZERS=1 \
    timeout 300 /var/mobile/run_dbg_hold_v2 /var/jb/usr/bin/chroot "$R" /bin/echo HI
  # 还原：mv "$R/usr/lib/dyld" "$R/usr/lib/.f1t_x" && mv "$R/usr/lib/.f1o_x" "$R/usr/lib/dyld"
  ```
  ⚠️ `timeout` 要 **> 90 s**，否则看不到结尾的 `[*] child exited rc=90`（会误判成"卡死"）。
- 设备侧我新增/留下的文件：`/var/mobile/{e2_launch,e2_probe,e2_patch_default,e2_slide,e2_thrdump,texg_scan,texg_verify,texg_write}.py`、
  `post_reboot_cli_test.sh`（项目原脚本副本）、`f1_leftovers_20261001/`（我移开的 F1 备份）；
  rootfs 内建过一个空目录 `/tmp/dsc_none`（无害，可删）。
- **设备上没有 lldb**（`/var/jb/usr/bin/lldb` 不存在，`/usr/bin/lldb*` 也不存在）——要取线程栈得先装。
- **纪律**：不擅自永久删除（**移动**而非 `rm`）；内核写前必须运行时定位 + 逐字节核对；
  PAC 数据指针需剥（`0xffff800000000000 | (v & 0x7FFFFFFFFFFF)`）；
  设备 SSH `root@192.168.64.1 -p 2222`（密码 `cisco`，**zsh 不做变量分词**，命令要放进数组 `S=(sshpass …); "${S[@]}" '…'`）；
  设备 python 是 procursus 的，**没有 `os.chroot`**，要用 `ctypes.CDLL(None).chroot()`。

---

## 8. 上游新提交的合并与"有没有暗示"评估（2026-10-01）

### 8.1 合并：**干净，已并入**

- `upstream = DCMMC/macPad`：**11 个新提交**（`bda24de..062c2ca`），相对 merge-base 的真实改动
  = **38 文件 / +3253 / −282**。
- `git merge --no-commit --no-ff upstream/main` → **rc=0、零 CONFLICT、"Automatic merge went well"**。
- 已合并并推送：`8d0d15c STATIC: merge upstream (GUI/windowing/input: …)`。

### 8.2 内容评估：**全是 GUI/窗口/输入/性能，对当前的 dyld/缓存/初始化器阻塞没有直接帮助**

改动集中在：`libmachook/AppInputBridge.m`、`macwsdisplayd/main.m`、`libmachook/Metal_hooks.x`、
`include/macws_{text_input,window_configuration,viewport_math,stream_protocol,host_protocol}.h`、
一堆 `misc/test_*` 与 4 份 `docs/evidence/*-20261001.md`。
主题：iOS IME→AppKit 焦点、Retina 预设与无界窗口、Electron 滚动卡顿、Chromium 全屏节奏、120 Hz 呈现目标、
resize 时的直接合成/drawable 归属。

`libmachook/mac_hooks.m` 的 +31 行是**渲染/交互活动文件的 fd 失效 inode 修复**（`macws_shared_descriptor_names_path`），
也与我们的阻塞无关。

### 8.3 唯一有价值的**暗示**（来自 `khanhduytran0/MacWSBootingGuide`，非 macPad）

`3770f6f` 在 `libmachook/objc_hooks.c` 里新增：
```objc
// workaround strange SIGTRAPs
uintptr_t objc_addExceptionHandler_new(void *fn, void *context) { return 0; }
void objc_removeExceptionHandler_new(uintptr_t token) { }
DYLD_INTERPOSE(objc_addExceptionHandler_new, objc_addExceptionHandler);
DYLD_INTERPOSE(objc_removeExceptionHandler_new, objc_removeExceptionHandler);
```
⇒ **作者遇到过"奇怪 SIGTRAP"，靠 interpose 老式 `objc_addExceptionHandler` 绕过**。
这提示：**旧式 libSystem/libobjc 的异常/初始化机制在这个 chroot 环境里确实会出问题**——
与我们"初始化器阶段出事"的现象属**同一类**。

**已实测的一个推论（被否定）**：作者的所有这类 workaround 都住在 **libmachook** 里，
而我此前的见证运行**没有插入 libmachook**。于是补做了带 libmachook 的运行：
```
DYLD_INSERT_LIBRARIES=/usr/local/lib/libmachook_arm64.dylib  + F1 + 原始缓存
结果：466 个镜像加载（比不插入时的 409 更多），
      最后一条初始化器仍是 /private/preboot/…/dopamine-…（ElleKit），
      仍然没有 HI ⇒ 插入 libmachook 不改变结局。
```

其它 khanhduytran0 提交（`3436ab8` 加 `com.apple.private.domain-extension` entitlement、
`loadtc`；`14e1850` 把 WS plist 的 `CA_DISABLE_SWAP_ICC` 删掉、把 Metal XPC 注册改回 dict 方式）
**都与我们的阻塞无关**。

### 8.4 结论

上游新代码**不能**直接解决我们的问题；但 §8.3 那个 `objc_addExceptionHandler` 暗示值得在
"查 `exit(90)` 成因"时一并考虑（例如：让 ElleKit 的初始化器不走老式异常注册路径，或看它是否因此 trap 后自尽）。

---

## 9. 给复核方（隔壁 AI）的逐条复核清单

> 目的：让独立复核者能用**最少的步骤**验证（或推翻）我的每条观察。
> 每条格式：**结论 / 复现 / 期望 / 什么会推翻它**。
> 设备访问：`sshpass -p cisco ssh -o StrictHostKeyChecking=no -p 2222 root@192.168.64.1`
> ⚠️ 设备上是 **zsh 且不做变量分词**：命令必须写成数组形式，例如
> `S=(sshpass -p cisco ssh … root@192.168.64.1); "${S[@]}" '…'`
> ⚠️ 设备上**没有** `shasum` / `strings` / `xxd` / `pgrep` / `lldb`；用 `wc -c`、`cat -v`、`od`。
> ⚠️ 设备 python 是 procursus 的，**没有 `os.chroot`**，需 `ctypes.CDLL(None).chroot()`。

### V0（前置）确认设备处于"好状态"
- **复现**：`wc -c /var/mnt/rootfs/usr/lib/dyld`
- **期望**：`1239616`（= 设备原始 dyld，SHA `9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`）；
  缓存主/`.01` = `2712764416` / `2203500544`（**未打 4GB 布局补丁**）。
- **推翻**：若尺寸不同 ⇒ 有人跑过 `apply_4gb_layout_patch.sh` 或部署了 F1。

### V1 ⭐【最重要】原始缓存 + F1 ⇒ 131 镜像加载、零 EXC_GUARD
- **复现**：
  ```bash
  R=/var/mnt/rootfs
  cp /var/mobile/dyld_f1_34790.bin "$R/usr/lib/.f1s_v" && chmod 755 "$R/usr/lib/.f1s_v"
  mv "$R/usr/lib/dyld" "$R/usr/lib/.f1o_v" && mv "$R/usr/lib/.f1s_v" "$R/usr/lib/dyld"
  DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_INITIALIZERS=1 timeout 300 \
    /var/mobile/run_dbg_hold_v2 /var/jb/usr/bin/chroot "$R" /bin/echo HI > /tmp/v1.out 2> /tmp/v1.raw
  # 还原：mv "$R/usr/lib/dyld" "$R/usr/lib/.f1t_v" && mv "$R/usr/lib/.f1o_v" "$R/usr/lib/dyld"
  ```
- **期望**：`/tmp/v1.raw` 有 **400+ 行 `dyld[PID]: <UUID> <path>`**，唯一路径数 ≈ **131**，
  含 `/usr/lib/libSystem.B.dylib`、`libsystem_kernel/platform/pthread/malloc`、`libobjc.A.dylib`、
  `libc++.1`、`CoreFoundation`、`Network`、`IOKit`、`IOMobileFramebuffer`、`IOSurface`、swift 全套；
  **`grep -c '\[exc\]' /tmp/v1.raw` = 0**。
- **推翻**：若出现 `[exc] type=12 code0=0xa000000100000000`（= EXC_GUARD）⇒ 那条"原始缓存不出守卫"不成立。
- **注意（易误判）**：`timeout` 必须 **> 90 s**；否则看不到结尾的 `[*] child exited rc=90`，会误以为"卡死"。

### V2 "打 4GB 布局补丁反而坏"（对照）
- **复现**：看 `misc/post_reboot_cli_test.sh` 的输出/日志 `post_reboot_cli_test.log` 里**更早那次**运行。
- **期望**：那次报 `GUARD_2ac75c000_HITS=1` / `VERDICT=STILL-BLOCKED-same-guard`，
  且 `[vm] 0x2ac75c000..0x2ac760000 prot=1/3`（**写只读页**）。
- **推翻**：若能在**打补丁**的状态下稳定加载 libSystem ⇒ 我的"补丁是坏的那步"就不成立。

### V3 `HI` 不打印，且进程在初始化器阶段 `exit(90)`
- **复现**：同 V1，看 `/tmp/v1.out`（只有 `Successfully marked proc …`，**无 `HI`**）与 `/tmp/v1.raw` 的**最后一条**
  `running initializer`。
- **期望**：最后一条是 `… in /private/preboot/CFD92CED…/dopamine-…`（**越狱注入**，路径被 dyld 截断在 96 字符左右）。
- **推翻**：若最后一条初始化器是某个 **macOS 系统库**（而非 `/private/preboot/…`）⇒ 我的"卡在越狱注入"归因错。
- **另一条可查性检查**：`ls /var/mnt/rootfs/private/preboot/` **是空的** ⇒ 那批注入 dylib 在 chroot 内如何解析**我未查清**，
  这本身是个待解的疑点（供你复核）。

### V4 那个 stderr blob 不是 libmachook、也不是失败标记
- **复现**：与 V1 同，但**不设** `DYLD_INSERT_LIBRARIES`；再跑一次项目自己的
  `run_bash.sh -c "echo hi"`。
- **期望**：两种情况下**同一个 7 条记录的 blob**（`44 46 …`/`41 4e …`/`46 4c …`/`54 44 …`/`46 32 …`/`48 47 …`/`48 41 …`，
  每条 8B tag + 8B 值；`HG`/`HA` 的值 = `0x2ac75c000` / `0x2ac760000`）。
- **推翻**：若某个运行里 blob 消失 ⇒ "通用诊断"的说法错，它可能与某条具体失败绑定。

### V5 ⭐ 内核侧地址（需要 KRW；`kcall` 不可用但 `kread/kwrite` 可用）
- **V5a** `task_exc_guard` = **`task + 0x5C4`**
  - **复现**：`proc_self()` → `+0x18` = `ro` → `ro+0x8` = `task`（**剥 PAC**：`0xffff800000000000 | (v & 0x7FFFFFFFFFFF)`），
    读 `task+0x5C4`。
  - **期望**：普通进程 `0x99`；`proc_find(1)`（launchd）`0x53`；`proc_find(0)`（kernel_task）`0x00`。
  - **推翻**：值不随任务变化、或三个进程读不出上述形态。
- **V5b** `task_exc_guard_default` = **IDB `0xFFFFFE000A9FABE0`**
  - **复现**：先求 slide——用 `vm_shared_region_create`（IDB `0xFFFFFE0008060FD0`）的**序言字节**
    `7f2303d5 ff0303d1 e923056d fc6f06a9` 做 needle，**步长 0x1000** 扫；
    本 boot 得 `SLIDE = 0x1a874000`，于是运行时地址 `0xfffffe002526ebe0`，读回应为 **`0x00000099`**。
  - **推翻**：若值不是 `0x99`（平台字节）或第三方字节不是 `0x00`。
  - **注意**：项目原 `kfind_slide.py` **步长 0x200000**，会漏掉非 2MB 对齐的 slide——这是它报 "no hit" 的原因。
- **V5c** `size = SHARED_REGION_SIZE_ARM64` 的物化点 = **`0xfffffe0008061160`** = `MOVZ X20,#1,LSL#32`（字节 `34 00 C0 D2`）
  - **注意**：IDA 把它显示为 **`MOV`**，按 `movz`/`LDR`/immediate 搜**都搜不到**；
    只能在函数内**文本搜索** `100000000`。
- **V5d** `exec` **重建 task**（同一 pid 下 task 指针变化、guard 归位 `0x99`）；`fork` **复制父任务的 guard**
  （`sub_FFFFFE0007FA31B4`：`LDR W8,[X22,#0x5C4]`→`STR W8,[X19,#0x5C4]`，`kernel_task` 特判置 0）。

### V6 E2：守卫可被"非致命化"（机制成立，但不解决根因）
- **复现**：`kwrite32(task_exc_guard_default_runtime, 0x90)`（先确认原值 `0x99`），再跑见证。
- **期望**：从 `EXIT=137`（守卫 SIGKILL）变成 **`EXIT=90`**。
- **推翻**：若仍 137 ⇒ 该位不是致命性的来源。
- ⚠️ **务必还原 `0x99`**（我离开时已还原）。

### V7 路线 D 的工具链（可独立验证）
- **复现**：`misc/dyldextractor-2.2.2-slideinfo5.patch` + `misc/uncache-slideinfo5.patch` 应用到
  dyldextractor 2.2.2 / uncache.py 后，对 `analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e`
  抽 `/usr/lib/libxml2.2.dylib` → uncache → **宿主 `dlopen`**（需把 install name 唯一化以排除 dyld 去重）。
- **期望**：`dlopen` 成功（libxml2：3621 rebases / 134 binds）。
- **推翻**：若 `mmap errno=22`（= 打补丁前的状态）。

### V8 上游合并
- **复现**：`git merge --no-commit --no-ff upstream/main`（`upstream = DCMMC/macPad`）。
- **期望**：rc=0、零冲突；改动 38 文件 / +3253 / −282，全为 GUI/窗口/输入/性能。

---

## 10. commit / merge 总结（给复核方与记录）

| commit | 内容 |
|---|---|
| `8d0d15c` | **merge upstream/main**（11 个提交：Retina 预设、Electron 滚动、drawable 归属、120 Hz、IME→AppKit 焦点）— 干净合并 |
| `9910b6d` | 上游合并评估：内容对阻塞**无直接帮助**；唯一暗示 = `objc_addExceptionHandler` interpose；实测"插入 libmachook 不改变结局" |
| `87049ad` | 最终交接：修正设备状态描述 + 精确复现命令 |
| `0cb4297` | **正式交接回复**（本文档 §0-§7） |
| `b1aeced` | **重大发现**：原始缓存 + F1 ⇒ 131 镜像加载、零守卫；卡点转到初始化器 |
| `198700c` | S1/S2 结果 + **对我自己两条结论的更正** |
| `49424d2` | 完整交接简报（544 行） |
| `802009e` | E2 机制成立 + "撞上布局根因"（**后已撤回**，见 `198700c`/`b1aeced`） |
| `dfd1d4b`/`512491d` | 第三方 dsc 工具评估：全都不支持 slide info v5 |
| `150a3f1` | D③ 研究："条件可行"（**后由 `2a01bce` 判定不可行**） |
| `2a01bce` | **D③ 判定不可行**（`reuseExistingCache` 是快路径） |
| `80ccdff`/`543ac62` | 路线 C 资产发现（13.2.1 缓存 3.21 GB）与 rootfs 来源修正 |
| `ad3b785`/`f4fc7ce` | **v5 补丁两个**（dyldextractor + uncache.py），v5 支持落地 |
| `6dd7640` | 过夜 `.a2s` 索引（1.13 GB）的运行与判定方法 |
| `2df12be`/`ea7e6cc` | 路线 D 里程碑（**dylib 可加载**）+ 批量冒烟（8/8） |
| `c69d32b` | Phase 2 内核 IDB 字节级验证 |

**核心资产位置**：
- `.a2s` 符号索引：`analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e.a2s`（1.13 GB）
- 两个 v5 补丁：`misc/dyldextractor-2.2.2-slideinfo5.patch`、`misc/uncache-slideinfo5.patch`
- E2 与复核脚本：`misc/e2_launch.py`、`e2_probe.py`、`e2_patch_default.py`、`e2_slide.py`、`e2_thrdump.py`、`uncache_batch.sh`
- F1 dyld：设备 `/var/mobile/dyld_f1_34790.bin`（SHA `14e2751b…`，CDHash `cd023af61cd044d269a6bb89380c80f5b15979dc`）
