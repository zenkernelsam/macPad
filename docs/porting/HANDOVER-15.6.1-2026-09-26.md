# HANDOVER — macOS 15.6.1 dyld shared cache on iPadOS 16.3 (2026-09-26)

**写给一个没有任何记忆的 AI。** 目标：让另一个 agent 不重新推导就能继续干活。
本文档 = 全部已知事实（每条都带证据等级）+ 全部 patch + 完整复现步骤 + 当前未决问题。
同样内容的实时状态在 `docs/porting/dyld-15.6.1-state.md`（更长、按时间序）。

---

## ⭐⭐⭐ 2026-09-27 00:1x 更新（最新，先读）：exec 门已通 + 536 真因锁定 + 536=0 已验证

### A) exec EACCES 真根因 = 部署文件**缺可执行位**（整场最大坑，已解决）
`kern_exec.c:6242`：非 authopaque 挂载（/private/var）+ 文件无执行位（0644）→ 直接 EACCES（静默）。部署脚本 `rm;cp` 把目标建成 0644。**修复：部署后 `chmod 755`**。之后 `true/ls/echo` 均 exec 成功，macOS dyld 真正运行。
（附：launcher `0x46FC` 曾被写成非法指令 `0x001c0012` → 任意 posix_spawn 失败即 SIGILL(132)，已修；`perror("posix_spawn")` 是残留 errno，真实码用 `analysis/dyldwork/exec_probe.py`。）

### B) dyld 内探针 SIGILL(132) 根因 = `build_dyld.py` 的 cave hex **字节序写反**（已修，新增 `_le()`）

### C) ★★★★★ syscall 536 = EINVAL(22) 的真因：**缓存 slide-info version=5，内核只支持 1–4**
- IDA `sub_8062CA8` @ `0xfffffe0008063024`：`if ((version-1)>3) → KERN_FAILURE(5)`（源码 `osfmk/vm/vm_shared_region.c:2934` switch default）→ `bsd/vm/vm_unix.c:2725` 映射为 **EINVAL(22)**。
- 物证：`xxd -s 0x7ad4c000 /Users/ciscohe/Desktop/dyld-cache-15.6.1/dyld_shared_cache_arm64e` = `05 00 00 00 00 40 00 00`（version=5, page_size=16K）。
- 触发：`sms_max_prot & VM_PROT_SLIDE(0x20)`（与 slide 数值无关）；次生障碍 page_size=16K≠内核 4K。
- 已排除：region 占用（实测空 12）、setup 门6/10/11（KRW 实测 ubc+blob 存在、覆盖 [0,0xa160c000]）、dynregion、slide。

### D) ★★★★★ 验证：掩掉 `VM_PROT_SLIDE` → **536 返回 0，缓存真正映射**
空 region 上探针掩 9 条 mapping 的 `0x20` → `ret536=0`（try2=22 = region 已填）。
**用户态修复**：`analysis/dyldwork/dyld_noslide`（在 dyld `0x35690` 注入 cave 掩 0x20）。已部署 `/var/mobile/dyld_noslide.bin` + 测脚本 `/var/mobile/post_reboot_noslide.sh`。

### E) 待解（下一步）
1. **[需冷启动]** 空 region 上用 `dyld_noslide` 首交，验证 536=0 + 进程真跑（libSystem 从缓存加载）。
2. **region 持久性**：一旦映射就持续到重启 → 需「首交成功 + 其余进程 reuse」或常驻 keeper。
3. **跳过 slide 的 rebase**：__DATA 的 rebase 指针可能留错（待验）。

---

## ⛔ 2026-09-26 20:1x 更新（**先读，推翻下面旧结论**）

### exec 137 的真根因 = 非 `ARM64/ALL`（已解决）

以前“全 exec 137、疑内核态损坏→需重启”的结论**已被推翻**。真根因：
**iPadOS 内核只 exec `cpusubtype=ARM64/ALL` 的 macOS 主可执行体**；macOS 15.x 系统二进制
只有 `x86_64+arm64e` → 直接 137。证据：`misc/arm64ify_macho.py` 注释 +
把 rootfs 的 dyld/true/ls/bash/... 用 `python3 arm64ify_macho.py <file>` relabel 成 ARM64/ALL
（代码字节不动）+ `ldid` 重签 + `jbctl trustcache add` 后，**137 全部消失**；
未 arm64ify 的 `echo` 仍 137（完美对照）。

### ✅ exec 准入**已打通**（2026-09-26 20:2x）——macOS dyld 已能真正运行

**两个门的解法（纯用户态，无需 patch 内核/amfid）：**

```
# 对每个要跑的 macOS 二进制（含 /usr/lib/dyld）：
python3 misc/arm64ify_macho.py <file>                 # 门①: relabel 成 ARM64/ALL（否则 137）
ldid -Hsha256 -Cadhoc -S<entitlements.plist> <file>   # 门②: sha256 cdhash + CS_ADHOC
jbctl trustcache add <sha256 cdhash>                  # 入越狱 trustcache
rm <dest>; cp <file> <dest>                           # fresh inode
```

**关键：`-Hsha256` 必需**——trustcache 条目带 `hash_type` 字段（`osfmk/kern/trustcache.h`）；
只有 **sha256 cdhash** 才匹配 → AMFI 认 → 置 `CS_SIGNED` → 跳过 taskgated upcall（MIG-27001）。
之前所有 `-Cadhoc`（sha1）仍 EACCES 就是因为 hash_type 对不上。

**实测**：部署后 `true`/`echo`/`ls`/`bash` **不再 137/13**，macOS dyld 真正执行：
```
dyld: dyld cache '(null)' not loaded: syscall to map cache into shared region failed
dyld: Library not loaded: /usr/lib/libutil.dylib ...
```
⇒ **工作重心回到原任务核心：syscall 536 映射缓存**（对应下面 §4 门禁链 + `dyld_cleanB_err`）。
复现脚本：`analysis/dyldwork/remote_apply_form.sh` / `remote_deploy_dyld.sh`。

### 越过 137 后的下一道门 = `EACCES(13)`（**已于 20:2x 解决，见上**）

设备端探针（`posix_spawn` 直调，拿真实返回码）：
- `analysis/dyldwork/ceprobe.py` → `/usr/bin/true` **rc=13**（所有 arm64 macOS 动态二进制皆 13）
- ⚠ `launchdchrootexec` 会丢弃 `posix_spawn` 返回值、无条件 `perror` → 它打印的
  “No such file or directory” 是**残留 errno（假象）**，不是真实码。

**内核 + 官方源码双重确证（`analysis/xnu-xnu-8792.81.2/` + `analysis/dyld-dyld-1286.10/`）：**
- EACCES = `kern_exec.c` 的 `process_signature` → **taskgated/amfid upcall（MIG 27001 = `find_code_signature`）**
  返回非 0（subsystem 27000@`osfmk/mach/task_access.defs:55`；参数 `proc_getpid(p)`）。
- `kern_exec.c:7430`：**有 `CS_SIGNED` 就跳过 upcall**；`7506`：`CS_SIGNED` 是 upcall 成功后才置。
- spawn 场景（`imgp` flags bit 0x10）同一拒绝改走 `SIGKILL` → **137 与 13 同源**。
- `analysis/dyldwork/csops_probe.py`（`csops(CS_OPS_STATUS)`）实测：**能跑的进程
  （cachereg/jbctl/python3）全是 `CS_PLATFORM_BINARY(0x04000000)+CS_SIGNED`**；
  我们的二进制非 platform → 走 upcall 被拒。⇒ **`CS_PLATFORM_BINARY` 是判别因子**。

已排除（均仍 13）：CD flags=0x2/CS_ADHOC、CS_HARD|KILL|RUNTIME、CD platform 字节 1/2/5、
identifier 重签、loader 闭包入 tc、launchd 作业 launch type、非 chroot、任意路径、最小静态二进制。

**下一步（未做）**：让 arm64ified macOS 二进制被 AMFI 认定为**有效签名/platform**
（纯用户态途径候选：以“platform”形态入 jailbreak trustcache；或复现 cachereg 的签名形态）。
工具就绪：`ceprobe.py` / `csops_probe.py` / `arm64ify_macho.py` / `build_dyld.py`。

---

## ⛔ 2026-09-26 18:2x（**旧结论，部分已被上面推翻**）

设备当时**任何 chroot 内 macOS 二进制 exec 都失败（SIGKILL/137）**。已排除：trustcache
（已在）、单二进制重签（`platform-application` 已在）、amfid 有无、预读 vnode、
完整重跑 `postinst.sh`（重签 1156 镜像）、boot-args（空）。

**两个已确证的关键事实（仍有效）：**
1. **陷阱**：若把 Apple 原版 `dyld.orig` 部署为 `/usr/lib/dyld` 而没先入 trustcache，
   会让**所有** macOS exec 被内核杀（137）。部署任何 dyld 后必须确认它已受信。
2. **一次成功**（rc=134）：杀 amfid + 换受信 dyld 后 `true` 跑起来了，dyld 打印
   `syscall to map cache into shared region failed` → 说明 exec 一旦放行，回到预期的
   **syscall 536 失败层**。

> 注：当时“内核 exec/CS 态损坏 → 需重启”的结论**已被推翻**——真因是 arm64e 分支门（见上）。

**另：`dyld_pi` 注入 blob 的真实用途已解码** —— 它只为绕开 `preflightCacheFile`
在 chroot 里必然 EPERM 的 `fcntl(fd,97/F_ADDFILESIGS_RETURN)`。现用 2 条小补丁
（thin `0x35d70`→nop、`0x35d80`→`b 0x35d9c`）替代整块 blob，且**重新启用 `.01` 子缓存**。
构建器 `analysis/dyldwork/build_dyld.py`（编码全经汇编器验证）。


---

## 0. 一句话现状

macOS 15.6.1 dyld 能在 iPadOS 16.3 chroot 里执行到 `start()` 深处的应用入口调用点
（`0x6b94 blraaz x8`），syscall 536 (`shared_region_map_and_slide_2_np`) 本身已能在
空 shared region 上返回 0 并把主缓存 8 条映射真实落地。**未决的三件事**：
(1) 每次 exec 的 dynregion 生命周期；(2) `.01` 子缓存尾部 ~0.7GB 超出 iOS 4GB
shared region 的混合映射；(3) 若干二进制仍被 exec 阶段 SIGKILL(137) 的分类归因。

---

## 1. 环境 / 访问

| 项 | 值 |
|---|---|
| 设备 | M1 iPad Pro `iPad13,11`（SoC `0x8103`，代号 T8112 = M2 系） |
| iPadOS | 16.3 (20D47)，XNU `xnu-8792.82.2`（源码参考用 8792.81.2 亦可） |
| 越狱 | Dopamine **rootless**，`JBROOT=/var/jb` |
| SSH | `sshpass -p cisco ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no -p 2222 root@192.168.64.1` |
| macOS rootfs | 设备上 `/var/mnt/rootfs` = **数据卷上的目录树**（不是独立卷），内含 bindfs 子挂载 `mc/`、`ios/` |
| 缓存真实路径（host 视角） | `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e[.01]`；`/var/mnt/rootfs/System/Library/dyld/…` 是指向它的符号链接 |
| 仓库 | `/Users/ciscohe/Desktop/macPad` |
| 本地工作目录 | `/tmp/dyldwork`（会被清，重要产物放 `analysis/`） |

**持久化的分析文件（`analysis/` 目录——不要再放 /tmp）：**

| 文件 | 说明 |
|---|---|
| `analysis/dyld_15.6.1_arm64e` | 15.6.1 fat dyld（1257136 B） |
| `analysis/dyld_15.6.1_arm64e_thin` | arm64e thin slice（1240752 B），**所有 patch 偏移都以此为准** |
| `analysis/dyld_15.6.1_arm64e_thin.i64` (+.id0/.id1/.nam/.til) | IDA IDB，当前挂在 **ida-pro-mcp-Instance1** |
| `analysis/kernelcache_16.3_T8112.img4` | 设备原始 IMG4（21845088 B） |
| `analysis/kc_raw_16.3_T8112.bin` | 解压后的内核 Mach-O（**80052224 B**, arm64e），imagebase `0xfffffe0007004000`；应加载到 **ida-pro-mcp-Instance2** |

重新提取内核的命令（若需要重做）：
```bash
scp -P 2222 root@IP:/private/preboot/*/System/Library/Caches/com.apple.kernelcaches/kernelcache kc.img4
python3:  import pyimg4; im=pyimg4.IMG4(open('kc.img4','rb').read()); p=im.im4p.payload; p.decompress(); write(p.data)
# payload 是 bvx2/LZVN，解压后 cffaedfe Mach-O
```

## 2. 签名 / trustcache 运维（重启后必须重做）

```bash
JB=/var/jb/basebin/jbctl                      # Dopamine 自带
H=$(ldid -arch arm64e -h <file> | grep CDHash= | cut -c8-)   # 取 cdhash
$JB trustcache info | tr '[:upper:]' '[:lower:]' | grep -qi "$H" || $JB trustcache add "$H"
```

- `ldid -S<ent.plist> -M <file>` 重签（项目 entitlements 在 `/var/jb/usr/macOS/bin/entitlements.plist`，含 `com.apple.private.security.no-sandbox`——**dyld 必须带它**，否则 syscall 536 被 sandbox 挡 errno=40）。
- trustcache 每次重启清空；vnode 上的 cs_blob 也在 panic/reboot 后丢失。
- **重启后完整恢复清单**：①重签+重加所有测试二进制 cdhash；②重跑 cachereg holder（下述）；③确认 rootfs 内 dyld 是预期构建（hash 对得上）。

## 3. 缓存签名注册（syscall 536 前置条件，runtime-confirmed）

内核要求提交文件的 vnode 上挂有 cs_blob。办法：

```c
fcntl(fd, F_ADDFILESIGS=61, &user_fsignatures{fs_file_start:0,
        fs_blob_start:hdr[0x28](cs offset), fs_blob_size:hdr[0x30](cs size)})
```

工具已写好：**`/tmp/dyldwork/cachereg_ios`**（源码 `cachereg.c`），host 侧跑
（`mac_vnode_check_signature` 只允许 host 上下文成功），`pause()` 常驻保活 fd/vnode。
设备上已部署 `/var/mobile/cachereg`。注意它要一直活着——进程死了 blob 可能回收。

两个缓存的 cdhash（已验证加入 trustcache）：
`2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e`（主）、`8c7ba7e588b0edd43f7334e2de11688cd4732192`（.01）。

## 4. syscall 536 的完整门禁链（xnu-8792.81.2 `bsd/vm/vm_unix.c` `shared_region_map_and_slide_setup` + IDA 内核反编译双确认）

按源码顺序：

| # | 检查 | 失败返回 | 我们的状态 |
|---|---|---|---|
| 1 | `files_count==0` | EINVAL | 提交≥1 |
| 2 | `task->shared_region==NULL` | EINVAL | exec 时 vm_map_exec 无条件建——**chroot 进程有 region（check_np=12=存在但空）** |
| 3 | `sr_vnode` ≠ 进程 chroot root | EPERM | 文件必须在 region root 树下 → 用 `/var/mnt/rootfs` 内的数据卷路径 |
| 4 | fd=-1 伪条目：count≥2 且 sms_address/size 页对齐 | EINVAL | dynregion 条目遵守即可 |
| 5 | fd→vnode / FREAD / VREG | 各自 errno | OK |
| 6 | `mac_file_check_mmap`（sandbox） | 40 | **no-sandbox entitlement 已过** |
| 7 | `uid!=0` | EPERM | 全程 root |
| 8 | `v_mount` ∈ {根卷, preboot cryptex 卷} | EPERM | 数据卷文件过（见§6矩阵） |
| 9 | `scdir_enforce` 父目录校验 | EPERM | `System/Volumes/Preboot/Cryptexes/OS/...` 路径过 |
| 10 | `ubc_getobject` 空 | EINVAL | OK |
| 11 | **`ubc_cs_is_range_codesigned(vp,off,size)`** | EINVAL | **需 F_ADDFILESIGS——已通过 cachereg 解决** |

内核 IDA 要点（Instance2 可复核）：sysent @ `0xfffffe0007999680`（24B 表项，
+0x10=sy_call）；[536]=`0xfffffe0008459134`；`sub_8459570`=setup；`sub_8063590`=
task+0x3E8 shared_region getter；`sub_8063720`=vm_shared_region_enter（exec 无条件建）。
详见 `kernel-syscall536-re-handover.md` / `kernel-syscall536-finding.md`。

**errno-40 的精确来源（隔壁 AI RE 确认）**：Sandbox `mpo_file_check_mmap`
@ `0xfffffe000a659664` → `cred_sb_evaluate(op=16, file-map-executable)` →
对外来缓存 vnode（无 VSHARED_DYLD flag）deny 40。AMFI hook 只能返 {0,1}。

**AMFI MACF 已知点（隔壁 Instance2 反编译）**：`_vnode_check_signature`
@ `0xfffffe00092a45e4`；`AMFIIsCodeDirectoryInTrustCache`→
`pmap_lookup_in_static_trust_cache`；`codeDirectoryHashIsInLoadedTrustCache`
→`pmap_lookup_in_loaded_trust_caches`。L1124：`!(cs_flags & CS_PLATFORM_BINARY
/*0x4000000*/)` 才查 devMode；设备 `developer_mode_resolved=1` 不触发。
cdhash∈(static∪loaded)tc → 'trust-cache' 类别放行——理论该过但 exec 仍被杀，
说明触发点在更下游（exec 页映射/CS_KILL/或更早检查）——**这正是
proof2-137 待定位的点**。

## 4.5 关键运行语义（易踩坑）

- **syscall 294 `check_np`**：`check_np(&base)` → region 存在但未填充返 **12**，
  已填充返 0 且 copyout base；`task->shared_region==NULL` 返 22；
  **`check_np(NULL)` = detach/unmap 该 task 的 shared region——永远不要调**，
  它杀调用者（137）并污染后续 exec 的 pmap 状态。
- **kernel 对每条 mapping 施加 slide**（`slide = read_random % files[0].sf_slide`）——
  dyld 提交的是**未滑动**的 header 地址；我们 slide=0 原样用。
- **fd=-1 dynregion 伪条目**：`sms_file_offset` 不是文件偏移，而是**指向 dyld
  用户态 DynamicRegion 缓冲区的指针——内核 copyin 内容**填充该映射。prots=R/R。
  ⇒ dynregion 内容完全由提交进程内存提供，天然 per-mapper。
- **错误路径吞错**：`0x356dc cbz w23→ok` / `tbnz w0,#0→reuse ok` / 否则
  `errorMessage` 非 NULL 时**原生错误路径被跳过、函数返回 0**——
  "dyld cache not loaded" 走这里，可能导致静默-0 而非 halt。
- **probe 放置铁律**：在 syscall 536 **之前** exit（0x352bc 入口/0x3533c/0x35680）
  必被 137 杀掉——内核杀死"shared-region attach 半途退出"的进程。
  只有 ≥0x35698（syscall 返回后）的探针有效。早期大量"137"其实是这条。

## 5. Shared region 结构墙（runtime+源码双确认）

- iOS 16.3: base `0x180000000`，size `0x100000000` = **4GB**（`[0x180000000,0x280000000)`）
- macOS 15.x: size `0x180000000` = 6GB；15.6.1 缓存实际跨 `0x180000000→0x2ac75c000` ≈ **4.69GB**
- 主缓存 8 条映射 `0x180000000-0x22560c000`：全在界内，已真实映射成功（崩溃报告 `180000000-1e7f5c000 __TEXT SM=COW` 为证）
- `.01` 溢出表：
  - m0 `0x22560c000+0x54808000→0x279e14000` ✓；m1 `→0x27bfd8000` ✓
  - m2 `0x27dfd8000+0x38b4000→0x28188c000` ✗越界；m3-m6 全越界
  - m1/m2 间天然 gap `[0x27bfd8000,0x27dfd8000)`
  - 主缓存 m5/m6 间天然 gap 含 `0x1f8000000`（dynregion 候选位）

**混合映射方案（未实现）**：界内走 536；界外尾巴私有 `mmap` 到自然 VA
（借鉴 dyld `mapSplitCachePrivate`）；或改 .01 头部 `mappingCount`+重签只提交界内部分。

## 6. dyld patch 全表（thin-slice 文件偏移，全部字节级验证过）

| 偏移 | 原始 | patch | 语义 |
|---|---|---|---|
| `0x76270` | `011000d4`(svc) | `e0031faa c0035fd6` = `mov x0,xzr; ret` | crossarch trap stub；**缺它=所有 exec 140(SIGSYS)** |
| `0x30140` | `7f2303d5 f44fbea9` | `00008052 c0035fd6` = `movz w0,#0; ret` | `hasExistingDyldCache`→0，强制走 map 路径 |
| `0x34298` | `c4030094`(bl) | `00008052` = `movz w0,#0` | loadDyldCache 内 reuse 判定→0 |
| `0x3538c` | `7cca51b9` | `3c008052` = `movz w28,#1` | mapSplit 内 flag 强制 1 |
| `0x35fc8` | `e9634ff9`(ldr x9,[sp,#0x1ec0]) | `0900afd2` = `movz x9,#0x7800,lsl#16` | dynregion 提交地址 offset（+base=**0x1f8000000**）；`0x35fd8` 处 `add x9,x9,x11` **必须保留** |
| `0x50dfc` | `f840f9..`(ldr x8,[x0,#0x1f0]) | `00afd2` = `movz x8,#0x7800,lsl#16` | `dynamicRegion()` accessor 同步改 |
| `0x3576c-0x35afe` | preflightMainCacheFile+preflightSubCacheFile 死区 | 自研 blob | 见 §7 ⚠ |

**立即数陷阱**：`movz` 只有 16 位立即数——`0x27c00` 会被静默截断成 `0x7c00`
（曾经因此提交到 0x7c000000 越界失败而 accessor 指 0x27c000000 → SEGV）。
编码前永远用汇编器验证。

**dynregion 偏移演进**（防串）：最早 `0xfc00`→VA 0x27c000000（m1/m2 gap）→
中间档 `0x7a00`→VA 0x1fa000000（state.md 旧表里还留着，**过时**）→
**当前定稿 `0x7800`→VA 0x1f8000000**（主缓存 m5/m6 天然 gap，16K 映射表
`[0x1f7070000,0x1f9070000)` 空缺，永不冲突）。

## 7. dyld_pi 构建里有什么（重要——别再把死区当空地）

`/tmp/dyldwork/dyld_pi` = pristine + 上表全部 patch + **注入 blob @0x3576c-0x35afe**
（替换了 `preflightMainCacheFile` 函数体；`mapSplitCacheSystemWide` 在 `0x3537c` 调它）。
blob 做的事：open `/System/Library/dyld/dyld_shared_cache_arm64e` → F_ADDFILESIGS →
读 header 填 CacheInfo/mappings（含 fd=-1 dynregion @0x1f8000000）→ dump 到
`/tmp/MTOUT5.txt` → 返回让原代码继续走 syscall。

⚠️ `0x3588c`（preflightSubCacheFile 起点）在 dyld_pi 里**属于 blob 内部，不是死区**。
曾把寄存器探针打在这里（dyld_pm）→ 探针在 blob 半途中触发，dump 的 x8=1/x9=0x228
是 blob 内部状态而非 glue 现场，exit 103 ≠ 到达应用入口。**这是本次 session 确认的
坑：`true`/`ls` 的 RC=103 不能当"dyld 到了 glue call"的证据。**

## 8. 已确认的入口/退出语义

- `start`(0x53dc) 早段调 `hasExistingDyldCache`（@0x5a8c 处 BL）→ check_np →
  **deref dynamicRegion()**。若 region 已填缓存而 dynregion 未映射 → SEGV@0x1f8000000。
- 应用入口调用：`0x6b7c ldr x8,[sp,#0x38]`(glue)；`0x6b80/6b84` x9=[[sp+0x1d0]+8]；
  `0x6b88-90` 取 argc/argv/envp/apple；`0x6b94 blraaz x8`。
- **`dyld_file=<fsid>,<fileid>`** apple 参数：`start` 用它取 dyld 自身路径，
  缺省回落 `/usr/lib/dyld`——不是"可执行文件是 dyld"判定。
- `restartWithDyldInCache` @0x6844：`handleDyldInCache` 分支命中时重启进缓存内 dyld。

## 9. 未决问题（按优先级）

0. **exec admission vs dylib admission 是两个门（关键判别证据）**：
   `misc/sprobe_dylib.c` 编译的注入 dylib（raw svc、零依赖）经
   `DYLD_INSERT_LIBRARIES` 进 chroot 进程，**ctor 真实跑通并写了文件**——
   我们 ldid 签名的自制代码可以在 chroot 里执行。但**新 exec 的自制 exe
   （proof2）被 137**。→ proof2 死在内核 exec/AMFI exec-hook/launch-constraint
   层，不是 dyld、不是 vnode cs_blob、不是 trustcache 缺失。变招备选：
   若 exec 放行无解，用已信任系统二进制做宿主 + dylib 注入跑自制代码。
   （上游 README 明示：Apple 签名自身不够——platform-binary/library-validation
   flag 会招致 SIGKILL，须 ad-hoc 重签+trustcache；autosignd+libmachook
   exec-hook 是项目的自动签名链。）

1. **proof2 的 137**：本地构建的 arm64e/platform=1 测试 exe（`/tmp/dyldwork/proof2`，
   ctor 写 `/tmp/ctor_ran`、main 写 `/tmp/main_ran`+stdout+ret7），cdhash 已在
   trustcache 仍被 SIGKILL；而重签+注册的 `date2` 曾 rc=0。**假设**：自制二进制缺
   某个 AMFI launch constraint 要素（platform、CODE_DIRECTORY 形态、或
   `CS_KILL`/`CS_HARD` flag）。验证法：逐步二分（最小 Mach-O→加 LC_BUILD_VERSION→
   加 ctor），或对每次 kill 抓 amfid `log show --predicate 'process=="amfid"'`。
   **数据点**：纯静态 arm64e 无-dyld 二进制（`hw`，raw svc）也 137——exec 门
   与 dyld 无关；`dsctest`（weak_import 链 libSystem stub）却是静默-0——
   "admission 过了但 main 没跑"的第三类症状，与 glue 之谜同源嫌疑。
2. **dynregion 持久化**：536 成功后 fd=-1 条目随 mapper 进程退出消失（runtime
   confirmed），文件映射存活。待测：在已填 region 上重交 536 是否重建 dynregion；
   或常驻 keeper（有先有蛋：keeper 自己 exec 时就撞上 §8 的 deref——除非 keeper
   是 mapper 本体且别的进程全走 reuse）；或把 dynregion 改成文件映射。
3. **glue-call 真相未取证**：上一段的 reg dump 被证明是 blob 内部值。要真量
   `0x6b94` 处的 x8/x9，需挑**真正死的**代码洞放探针（如 `__text` 尾部 padding
   或用 `verifyChecksums`/未调用函数），并把 `blraaz` 换成 `b <probe>`。
4. **.01 尾部混合映射**（§5）——设计已定，实现未动。
5. 远景：AGX `0xe00002c2` 结构墙（GUI 的独立 mega-blocker，见 AGENTS.md）——
   与 dyld 线正交，暂不碰。

## 10. 失败史速查（别重复）

| 现象 | 真相 |
|---|---|
| errno 40 | sandbox `file-map-executable` → dyld 签 no-sandbox entitlement |
| errno 22 | UBC CS range 校验 → F_ADDFILESIGS（cachereg） |
| errno 1 (EPERM) | 文件卷/scdir 不对（数据卷↔preboot 与 region root 的 XOR 关系） |
| 140 SIGSYS | crossarch 未打（0x76270）或探针忘了 x16=1 |
| 139 @0x1f8000000 | dynregion 未映射（accessor 已指向那里但映射不存在） |
| 137 SIGKILL | **不是单一原因**：exec 前 AMFI / stale cdhash / launchdchrootexec 注入 libmachook / launch constraint 都可能；每个二进制单独归因 |
| 134 | dyld 磁盘 fallback 死在 libdyld.dylib 缺失 |
| 首跑 0 后续 139 | dynregion ephemerality（§9.2） |
| RC=103 | 本次证明是 blob 内探针 exit(0x67)，**不是** glue 到达证据 |
| 探针写到 /tmp/glueptrr | movk 立即数错位（path byte12 写成 'r'）——手写字符串每字节核对 |
| `check_np`=12 vs 170 | 12=region 存在但空（真实）；170 是 copyin 失败读栈垃圾的假阳性 |

## 10.6 部署/运维陷阱（每条都是血泪换来的）

1. **`rm` 再 `cp`，绝不覆盖写**：cp 覆盖同 inode → CS vnode 缓存陈旧 →
   静默 SIGKILL(137)。换 inode 才生效。
2. **`ldid -S` 会重排 fat slice**：签名后 arm64e slice 偏移会变（曾挪到
   `0xfc000`）。patch 偏移是 **thin-slice** 偏移；改 fat 前先读 fat header
   （`d[8+i*20+8..12]` BE=slice offset，arm64e: cputype 0x100000c subtype
   0x80000002）定位 slice。
3. **`jbctl trustcache add` 会静默失败**——必须 `jbctl trustcache info` 回查，
   且输出是**大写 hex**，grep 记得 `-i`。`for h in $(ldid -h)` 循环可能产出
   空变量——逐个验证。
4. **探针 svc 编码**：`svc #0x80` 本身不是 exit——必须 `movz x16,#1`。
   `movz w0,#N` 的 imm 在 bits[20:5]：`exit(N)` byte0=(N&7)<<5, byte1=N>>3。
   位置无关探针别用 `adr`（fixup 报错），用栈上构造字符串；路径每字节核对
   （曾因 movk 错位把 `/tmp/glue` 写成 `/tmp/glueptrr`）。
5. **间歇性 137 是环境噪音**（iOS-cache prebind/amfid 时序），同文件会
   137/0 交替——先连测几次再下结论。
6. **panic 签名**：`pmap_trim_internal: grand addr wraps around … vstart=
   0xffffffffffffffff` —— malformed 536 提交或 check_np(NULL) 的后遗症，
   panic 后 trustcache/cs_blob/jailbreak 全丢。
7. **IDB 污染警告**：`dyld_15.6.1_arm64e_thin.i64` 里 0x35754 显示的是我旧
   patch（`B 0x38d08`）而非原生字节（`MOV W0,#0`）。**字节真值以设备
   pristine 副本为准**（`/var/mnt/rootfs/usr/lib/dyld.orig` = fat 2289328B）。
8. **真死代码洞**：thin `0x38d08-0x38d3c`（56B，0x38d40 起是真函数）——
   放探针用这里，别再用 0x3576c blob 区。
9. **有用地址**：errno 全局 `0xa9b10`（cerror_nocancel @0x2d64 写）；
   `dyld4::console` @ `0xa2f4`（printf，可从 tramp 调，保存/恢复 LR+pacibsp）。

## 10.7 设备/仓库工具清单

| 工具 | 位置 | 作用 |
|---|---|---|
| `jbctl` | `/var/jb/basebin/jbctl` | `trustcache info/add <cdhash>` |
| `launchdchrootexec` | `/var/jb/usr/macOS/bin/` | `usage: launchdchrootexec uid gid <rootfs> <exec> args`；chroot+exec，**会注入 `DYLD_INSERT_LIBRARIES=/usr/local/lib/libmachook.dylib`**（stderr 打 `[launchdchrootexec] target=…`）——用它测 ≠ 纯 chroot |
| `libmachook.dylib` | `/var/mnt/rootfs/usr/local/lib/` | 注入 dylib：exec 钩子→autosignd 自动签名；曾有 137 嫌疑 |
| `autosignd` | iOS 侧 daemon（postinst 起） | 收 `/tmp/autosignd.sock` 路径→ldid+jbctl 自动签名 |
| `cachereg` | `/var/mobile/cachereg`（源码 `/tmp/dyldwork/cachereg.c`） | F_ADDFILESIGS+pause 保活 vnode cs_blob |
| `mountdevfs` | `/var/jb/usr/macOS/bin/` | chroot 内挂 devfs |
| `mount_bindfs` | `/var/jb/usr/local/bin/` | bindfs 挂载 |
| `loadtc` | repo `misc/loadtc/main.m`（**设备上未装**） | 经 `jbclient_root_trustcache_add_cdhash` 整文件加载 macOS .trustcache（v1/v2 格式已解析）。15.6.1 rootfs 里还没找到 .trustcache 文件（可能在 OS cryptex dmg 内） |
| `sprobe*.c` | repo `misc/` | raw-svc 探针源码，`shared_file_np`(12B)/`sfm_slide`(48B) ABI 结构在里面 |
| 上游 debug 137 | `sudo oslog \| grep "AMFI\|launchd\|WindowSer"` | 抓 AMFI kill 原因 |
| 设备备份 | `/var/mnt/rootfs/usr/lib/dyld.orig`（fat pristine 2289328B）、`dyld.func*` | dyld 回滚用 |
| 仓库设备路径 | `/var/jb/var/mobile/MacWSBootingGuide` 等 | **已不存在**（换过越狱）；staging 用 File Provider 目录 |

## 10.5 重启/安装问答（隔壁 AI 问过，已核实）

- **上游 "Setting up macOS full installation" 步骤**（README 链接 DCMMC/MacWSBootingGuide）：
  一次性工作，重启后**无需重做**——rootfs 是数据卷上的目录树，持久。
- **每次重启必做**（易失态）：Dopamine 重激活 → `postinst.sh`（全量 ldid+jbctl trustcache）
  → `cachereg` holder（F_ADDFILESIGS，保活 vnode blob）。`com.macwsguide.*`
  LaunchDaemons 在越狱恢复时自动拉起部分服务（实测旧 cachereg 自动复活）。
- **没有用 KRW 写内核**：全仓只有 `kread64/kread32` 只读探针（`misc/agx_iogpu_probe.c`
  dlsym 自越狱库）；exec 放行 = `jbctl trustcache` + ldid，纯用户态。
- 上游自述重要事实（README "Debug kill: 9"）：**Apple 签名本身也不够**——
  platform-binary / library-validation flag 会让进程被 SIGKILL，即使 cdhash 进了
  trustcache；必须 ad-hoc 重签。这很可能就是 proof2 137 的同质问题。

## 10.9 官方源码（已落盘 analysis/，给隔壁 AI）

| 源码 | 路径 | 对应关系 |
|---|---|---|
| **XNU** | `analysis/xnu-xnu-8792.81.2/` | 设备内核 8792.82.2 差一个 patchlevel；`bsd/vm/vm_unix.c:2189` = `shared_region_map_and_slide_setup`（536 门禁链已逐条对过 IDA），`bsd/kern/kern_cs.c` = F_ADDFILESIGS/`ubc_cs_blob_*` |
| **dyld** | `analysis/dyld-dyld-1286.10/` | **精确匹配** 15.6.1（二进制自报 `PROJECT:dyld-1286.10`）。`dyld/DyldMain.cpp` = 完整 start()/prepare()/handleDyldInCache() 源码 |

**源码级确认的关键事实（直接解了三个悬案）**：

- `DyldMain.cpp:start()` 尾部（~L1400）：`appMain = prepare(state, dyldMA)` →
  `result = appMain(argc, argv, envp, apple)` → `libSystemHelpers.exit(result)`。
  **`0x6b94 blraaz x8` 的 x8 = appMain**，x9 对象 = `state->config.process`
  （`+0x98` argc、`+0xa0` argv、`+0xb0` apple）。
- `prepare()` @ `DyldMain.cpp:536`；`getEntry` @ ~L1043：无 LC_MAIN 且
  非 LC_UNIXTHREAD → `halt("main executable is missing LC_MAIN")`；
  LC_UNIXTHREAD → `gotoAppStart`（旧式）。
- `handleDyldInCache` @ L1084：每次 start 必跑；L1095 先调
  `hasExistingDyldCache`（→ check_np → dynamicRegion deref——**139 死点在这**），
  再 `dyldMH->inDyldCache()` 判定；命中缓存内 dyld → `restartWithDyldInCache`。
- `getDyldPath` @ L1062：`dyld_file` apple 参数 = dyld 自己的 fsID/objID，
  缺省 `/usr/lib/dyld`——**与"可执行文件是谁"无关**，之前的猜想作废。
- `libSystemHelpers.exit`（`LibSystemHelpersWrapper`）依赖 libSystem 初始化——
  **若 libSystem 根本没加载（缓存不完整），exit 链本身可能就是
  "静默-0/异常退出"的来源之一**，排查 silent-0 时先查这条。

## 10.8 资源与源码索引

- **15.6.1 缓存完整副本（host）**：`/Users/ciscohe/Desktop/dyld-cache-15.6.1/`
  ——主 2712764416B + `.01` 2203500544B，与设备上文件逐字节一致。
  分析 header/mapping 表用它，不用碰设备。`dsc_extractor`/`misc/extract_dyld_cache.py` 可用。
- **rootfs staging**：`/Users/ciscohe/Desktop/macos-15.6.1-rootfs` +
  `/Users/ciscohe/Desktop/build-rootfs-15.6.1.sh`（产出）→
  `misc/install_rootfs_15.sh`（设备安装）。
- **xnu 源码**：`/tmp/dyldwork/xnu-xnu-8792.81.2/`（被清则需重下：
  `apple-oss-distributions/xnu` tag `xnu-8792.81.2`；设备是 8792.82.2，
  差一个 patchlevel，536 的 setup 逻辑已逐一与 IDA 反编译对过）。
- **chroot 交互 shell**：`/var/jb/usr/macOS/bin/run_bash.sh`。
- **已知死路（勿再试）**：`check_np(NULL)`/`deallocateExistingSharedCache`
  detach——杀调用者 137 + 污染 pmap 致 panic；detach 后 region **无法重建**
  （只在 exec 时建一次）→ 536 永远 EINVAL。`hw` 纯静态 arm64e 137 →
  exec 门与 dyld 无关已证。
- **dyld 磁盘 fallback 行为**：536 失败 → dyld 回退逐文件 mmap →
  `libdyld.dylib not found` → abort(134)；errorMessage 非 NULL 时原生
  错误路径被跳过直接返回 0（0x356dc/0x35710）——**静默-0 的可能来源**。

## 11. 复现最短路径（从头到一个能跑的测试）

```bash
# A. 设备端（重启后）
ldid -S/var/jb/usr/macOS/bin/entitlements.plist -M /var/mnt/rootfs/usr/lib/dyld   # 若 dyld 重打过
for f in /var/mnt/rootfs/usr/lib/dyld /var/mnt/rootfs/tmp/proof2 /var/mobile/cachereg; do
  H=$(ldid -arch arm64e -h $f | grep CDHash= | cut -c8-); /var/jb/basebin/jbctl trustcache add $H
done
/var/jb/basebin/jbctl trustcache add 2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e   # 主缓存
/var/jb/basebin/jbctl trustcache add 8c7ba7e588b0edd43f7334e2de11688cd4732192   # .01
nohup /var/mobile/cachereg /var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e \
       /var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.01 >/tmp/cachereg.log &
# B. 测试
chroot /var/mnt/rootfs /usr/bin/true; echo $?     # 纯 chroot（无 libmachook 变量）
# launcher 路径单独测（注入了 libmachook，结果不可与纯 chroot 混用）
```

## 12. IDA 约定（用户硬性要求）

- 逆向一律走 ida-pro-mcp；Instance1=dyld IDB，Instance2=内核 IDB（`analysis/kc_raw_16.3_T8112.bin`，
  imagebase `0xfffffe0007004000`，若用户处 IDB 丢失需重载）。
- 需要新文件加载 IDA 时**先报文件绝对路径**等用户加载。
- 不用 python 猜指令编码/结构——编码用 `clang -c`+objdump，结构用 IDA py_eval。

## 13. 相关文档

- `dyld-15.6.1-state.md` — 时间序全状态（本文件的超集）
- `kernel-syscall536-re-handover.md` — 536 wrapper/setup 完整反编译笔记
- `kernel-syscall536-finding.md` — sysent 布局
- `dyld-15.6.1-deep-re-handover.md` / `dyld-15.6.1-full-analysis.md` — dyld 全貌
- `patch-ledger.tsv` — 补丁台账（老 13.4 对照 + 新 15.6.1）
- `AGENTS.md` — 项目纪律（symptom-suppress ≠ fix；每条结论要 IDA/runtime 证据）
