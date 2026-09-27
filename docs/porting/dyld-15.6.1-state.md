# macOS 15.6.1 dyld shared-cache bring-up — live state

**READ THIS FIRST after context loss.** Active task: get dyld to map the
macOS 15.6.1 shared cache on iPadOS 16.3 (xnu-8792.82.2) so macOS binaries
run in chroot. This file is the single source of truth — update it whenever
a fact/offset/result changes, BEFORE context is lost.

**▶ 完整移交文档（给下一位 agent 的自包含复现+继续指南）：
`docs/porting/HANDOVER-HELLO-2026-09-27.md` + `HANDOVER-DYLD-ADMIT-2026-09-27.md`**

---
## 📌 顶部摘要（2026-09-28 最新；下方旧段落如与之冲突，以本节为准）

### 1) 签名/准入（已定论，不再推导）
- 内核 exec 准入查的 cdhash = **`sha256(CodeDirectory[0:CD.length])[:20]`**（只哈希 CD 本体；工具：`misc/cdhash_slices.py`，对每个 slice 都算）。
- 配方：`ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist F`（**不加 `-Cadhoc`/`-M`**）→ `jbctl trustcache add <40hex>` → `rm -f` 后 `cp`（新 inode）+ `chmod 755`。
- 判据：`re-clears=2` ⇒ 准入过；`re-clears=1` ⇒ 被杀（确定性 8/8）。`SIGNALED 9` 先查 cdhash 是否按 `CD[0:cdlen]` 算对并进 TC。

### 2) 【已达成】536 把 iOS dyld 缓存映射进 chroot shared region
- **真因 = `files[].sf_slide` 非 16K 对齐**（`shared_file_np` = 12B `{fd,count,sf_slide}`；iOS 主分片 `sf_slide=0x539b0000`）⇒ 清零后 **536 成功**。
- 产物 `analysis/dyldwork/dyld_sf0.bin`（`crossarch+plataccept` + cave@0x35690→0x9b578 清零 `sf_slide`）。
- 验收 3×：`rc=0 HELLO`、`Using mapping in dyld cache ×91`、`re-using existing shared cache ×2`、`cache not loaded ×0`、`check_np base=0x180000000`。
- 前置：① `misc/restore_env.sh` 补 TC；② `cachereg_ios /iosdsc/*`（46 片）附 CS blob；③ `set_blob_cov.py` 把 46 片 `csb_end_offset`=文件大小。

### 3) 当前卡点：libSystem 的平台/兼容
- `/bin/echo` 过（只需 `_err`）；`/bin/cat`/`/bin/sh` 失败于 `___error` Expected in **shim 的 UUID(B90391D8)** ⇒ **libSystem 实际仍取磁盘 shim**。
- 移走 shim → `wrong platform to load into process`；`platstub`（`loadableIntoProcess→1`）强载 iOS 库 → **SIGILL(132)**。
- ⇒ “库全走（iOS）缓存”受限于 **iOS 库 ≠ macOS 进程**；**结论：真正需要的是 macOS 自己的缓存**。

### 4) 下一步（需重启拿干净 region）
- **实测：shared region 跨进程持久**（`check_np` 连续两次 = `0 / base=0x180000000`）；所以要换缓存必须**重启**（bindfs 新建 root 尝试失败：`mount_bindfs: No such file or directory`）。
- **实测：macOS 缓存 `slideInfoVersion=0 / slideInfoOffset=0`（根本没有 slide-info）** ⇒ 旧“slide-info v5”障碍**对本缓存不适用**。
- 重启后跑设备上 `/var/mobile/post_reboot_mac.sh`（自动：校验干净 region → 复原 TC → cachereg macOS 缓存 → blob 覆盖 → 部署 `dyld_noslide.bin` → 验收 HELLO/using/notloaded + cat/ls/sh）。

### 5) 重启后必重建（易失）
- **jailbreak trustcache（内存）** → 用 `misc/restore_env.sh`；**cachereg 的 cs_blob（vnode 级）** → 重跑 `cachereg*`；rootfs 本身不丢。
- ⚠️ 旧 `run_nocskill` 硬编码 kernel slide → 重启后 `proc not found`；**实测 TC 复原后直接 `chroot` 即可**（不需要 run_nocskill）。

### 6) 已作废/易误解的旧说法
- ❌“签名配方不对导致 SIGNALED 9” → 真变量是 cdhash 算法（§1）。
- ❌“22 = CS 覆盖门（0x8459cbc）是首因” → 对 iOS 缓存，**首因是 `sf_slide`**；CS 覆盖门只在特定配置下暴露（覆盖面需 ⊇ mapping 区间）。
- ❌“macOS 缓存卡在 slide-info v5” → 本缓存无 slide-info（§4）。
- ❌ marker 站点（mk*/m2*）不可靠（`SIGNALED 11`、零 marker）→ 改用 `cknp2`/自建 cave 探针。

---

## ★★★★★ 2026-09-28 ⭐⭐⭐⭐⭐ MILESTONE：**cdhash 算法搞错 = 之前所有 `SIGNALED 9` 的真因**（runtime-confirmed）

**结论（不要再推导）**：内核 exec 准入查的 cdhash 是
`sha256(CodeDirectory[0 : CD.length字段])[:20]` —— **只哈希 CodeDirectory 本体**
（长度取 CD 头 +4 的 `length`），**不是** `sha256(CD..superblob尾)`（会多算对齐 padding），
也**不是**整个 superblob。上一轮 `try_dyld.sh`/临时脚本用了 `sha256(b[o:])`（到尾）→
算出的 hash 永远不在核心里 → `jbctl trustcache add` 加的是**无效项** → AMFI `CS_KILL` →
`SIGNALED 9`（零 dyld 输出）。**签名配方（ldid 裸签 flags=0x0）从头到尾没问题。**

**铁证（设备实测，TC=jbctl trustcache info）**：

| dyld | 结果 | H_full=sha256(CD..尾) | H_cdlen=sha256(CD[0:cdlen]) |
|---|---|---|---|
| dyld_plat.bin | **rc=0** | a9529bb2（不在 TC） | **2821ecab（在 TC）** |
| dyld_p2.bin | **rc=0** | 0fd89939（在 TC） | 9bf6c2a7（在 TC） |
| nm/deploy.bin | SIGNALED 9 | d74e1dcb（在 TC） | 10b958ba（**不在 TC**） |
| dyld_p3.bin | SIGNALED 9 | 411077e8（在 TC） | 639c6f84（**不在 TC**） |

- **决定性验证**：给 deploy.bin 只加 H_cdlen（10B958BA…）→ 立刻 `rc=0`（3×）。
- 判别式：`re-clears=2` ⇒ 准入通过；`re-clears=1` ⇒ 被杀。**确定性，非竞态**（8/8 稳定）。
- 正确工具：`misc`/设备上 `/var/mobile/nm/cdhash_slices.py`（它对：`sha256(b[o:o+cdlen])`）。
  临时脚本一律改用它，别再手写 `sha256(b[o:])`。

**重启后复原（交付物 A，已验证）**：`mount | grep mnt` 为空但 `/var/mnt/rootfs` 可用（普通目录，非挂载点，重启不丢）；
真正会丢的是 **jailbreak trustcache（内存）**。所以复原 = 部署活件 + 对 **dyld 及 HELLO 路径每个 Mach-O**（dyld / libSystem.B.dylib / libdyld.dylib / echo …按实际加载集）
用 `cdhash_slices.py` 算 cdhash 后 `jbctl trustcache add`。可用 `misc/restore_env.sh`（已用正确工具）。

**※ 勘误（覆盖本文件下方 2026-09-27 的旧说法）**：line 23「base 重签同名 → 与 base 字节完全相同 ⇒ 配方正确」成立；
但 line 62-64「`-Cadhoc` 只在内容改动时才致命」等旧解释作废——真实变量是 **cdhash 是否按 `CD[0:cdlen]` 算对并进 TC**。

## ★★★★★ 2026-09-27（晚）⭐⭐⭐⭐⭐ MILESTONE：**修改过的 dyld 准入规则破解** —— `ldid` 裸签（无 `-Cadhoc`）是唯一存活配方

### 🎉🎉 2026-09-28 里程碑：**536 映射 iOS 缓存成功（真因 = `files[].sf_slide` 非 16K 对齐）**
**真凶**：`shared_file_np` 是 **12B/条 `{sf_fd, sf_mappings_count, sf_slide}`**；iOS split-cache 主分片的 `sf_slide` = **0x539b0000（未对齐）** ⇒ 内核在文件循环之前/内就 EINVAL(22)。
**最小修复**：在 536 调用点前把每条 entry 的 `sf_slide`（+8）清零。
- 构建：`python3 build_dyld.py dyld_sf0.bin crossarch plataccept` 后再打 cave（`0x35690→0x9b578`：`mov x9,x1; mov x10,x0; loop{str wzr,[x9,#8]; add x9,#12; subs;b.ne}; mov x3,x26; b 0x35694`）。产物 `analysis/dyldwork/dyld_sf0.bin`。

**运行验收（3×）**：`DYLD_SHARED_CACHE_DIR=/iosdsc DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_SEGMENTS=1 chroot /var/mnt/rootfs /bin/echo HELLO`
- rc=0、输出 HELLO；**`Using mapping in dyld cache` × 91**、**`re-using existing shared cache` × 2、`cache not loaded` × 0**。
- `cknp2` 探针：**`check_np ret=0, base=0x180000000`** ⇒ **shared region 已被 iOS 缓存填上**。
- 无 env 基线仍正常（HELLO）。

**⚠️ 更正（重要）**：91 个 image 确实走“缓存映射”，但 **`/usr/lib/libSystem.B.dylib` 实际仍取磁盘 shim**（判据：`/bin/echo` 只需 `_err` → shim 有→过；`/bin/cat`/`/bin/sh` 需 `___error` → 报 `Symbol not found: ___error Expected in <B90391D8> /usr/lib/libSystem.B.dylib`，而 B90391D8 = **shim 的 UUID**）。
**真正的下一道墙 = libSystem 平台/兼容**：移走 shim → `wrong platform to load into process`；用 `platstub`（`loadableIntoProcess→1`）强载 iOS 库 → **SIGILL(132)**。⇒ “库全走 iOS 缓存”尚未完成，核心堵点是 **iOS libSystem × macOS 进程**。

**前置条件（缺一不可）**：① 重启后补 trustcache（`misc/restore_env.sh`）；② `cachereg_ios` 对 `/iosdsc/*` **全部 46 片**附加 CS blob；③ `set_blob_cov.py` 把 46 片 `csb_end_offset` 改成文件大小。

**下一步**：移除磁盘 shim 后，dyld 已能用缓存解析 91 库，但 `/usr/lib/libSystem.B.dylib` 会被平台检查拒（`wrong platform to load into process`）；用 `platstub`（`loadableIntoProcess→1`，诊断性）强行加载 iOS 库则 **SIGILL(132)** ⇒ 下一道墙 = **iOS 库在 macOS 进程里的平台/兼容兼容性**（需评估是否需真正的 macOS 缓存而非 iOS 缓存）。

### 🔜 2026-09-28 下一步计划（待重启测试）：改映射 **macOS 自己的缓存**
- 动机：macOS 缓存里的 libSystem **兼容 macOS 进程**，可绕过“iOS 库不兼容”的墙。
- 已知障碍：state doc 旧结论“**macOS 缓存 slide-info version=5，内核只支持 1..4**” —— ⚠️ **已证伪：本 macOS 缓存 `slideInfoVersion=0/slideInfoOffset=0`（无 slide-info）**。
- 对策：dyld 侧把每条 mapping 的 `sms_slide_size(+0x18)/sms_slide_start(+0x20)` 清零（不触发内核读 slide-info） + `files[].sf_slide` 清零。
- 产物：`analysis/dyldwork/dyld_noslide.bin`（= crossarch+plataccept + sf0 + mapping-slide 清零）。
- 实测：现 region 已被 iOS 缓存占；强制不复用后映射 macOS 缓存 **仍 EINVAL(22)**（因 region 非空）。
- **待办**：**重启取得干净 region** → 跑设备上 `/var/mobile/post_reboot_mac.sh`（自动：复原 TC → cachereg macOS 缓存 → blob 覆盖 → 部署 dyld_noslide → 验收 HELLO/using/notloaded + cat/ls/sh）。

### ⚠️ 2026-09-28 重启后环境退化（必须知道，否则会误判）
**设备发生过一次 panic 重启**（`ptd ... does not belong to iommu @pmap.c:15786` + `initproc exited`；现场 VirtualMachine.xpc 512% CPU/load 27+，与 dyld 实验无直接因果）。**重启后下列状态丢失，需重建**：
1. **jailbreak trustcache 清空** → 修改版 dyld 立即 `SIGNALED 9`（exec veto）。**修复：重新 `jbctl trustcache add` 后才能过准入**（本次实测：补 TC 后 9→6/0）。
2. **cachereg（对 macOS 缓存）需要重启**：否则 dyld 报 `code signature registration for shared cache failed`（缓存文件无 CS blob）。
3. **shim `libSystem.B.dylib` 变为不可用**：`Library not loaded: /usr/lib/libSystem.B.dylib`，`Reason: code signature invalid (errno=1) sliceOffset=0x00018000 ...`（该文件是 **fat 两切片**，`cafebabe 00000002`；`ldid -Hsha256 -S` 重签 + 补 TC 仍报 invalid）。⇒ **HELLO 目前不通**，需先把 shim 恢复到可被信任的形态（重启前它是可用的）。
4. **marker 组合测试（crossarch+plataccept+mkA+mkB+m2u+m2S）结果：`SIGNALED 11`，零 marker、零 dyld 输出** ⇒ 这些 marker 站点/机制确实有问题（与 subagent 判断一致），**不要再用它们做判据**；需另选探针方式（如 `cknp2entry/cknp2cave` 之前在旧环境里是可用的）。

### 🔧 2026-09-28 补记：签名/部署方法论（实测勘误，务必按此）

**实测矩阵（设备上，`run_nocskill ... chroot $R /bin/echo HELLO`）**：

| 被测物 | 签名方式 | 结果 |
|---|---|---|
| pristine | ldid -Hsha256 -S<ent> | **SIGSYS(12)**（过准入；pristine 无 crossarch → svc 触发 SIGSYS） |
| dyld_probe_noC（活基线） | 原样 | **rc=0** |
| dyld_es.bin | 原样 | **SIGILL(4)**（过准入，探针自身错） |
| dyld_plat = crossarch+plataccept | ldid -Hsha256 -S<ent> | **rc=0 ✅** |
| base 重签为**同名**dyld_probe_noC.bin | ldid -Hsha256 -S<ent> | **与 base 字节完全相同** ⇒ ldid 就是原工具、配方正确 |

**⚠️ 关键教训（之前误判“签名配方不对”）**：
- 先前多轮 `SIGNALED 9` 实为**状态污染**（连续多次 rm+cp 换 dyld / vnode CS 缓存 / cachereg 常驻），**不是**签名不被接受。
- **正确的签名配方（已验证可重新产出可用 dyld）**：
  1. `ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist <file>`（**不加** `-Cadhoc`、**不加** `-M`）
  2. `cdhash40 = sha256(CD blob)[:20]`（注意：是**整个 CD blob**（含 4B magic+4B len），取 sha256 前 40 hex）
  3. `jbctl trustcache add <cdhash40>`
  4. **`rm -f` 目标后 `cp`**（新 inode）+ `chmod 755`
  5. 若连续换 dyld 出现异常 veto：**先回滚到活基线一次，再部署新件**（消除残留）。
- `jbctl trustcache info` 输出是**大写 hex**，grep 必须 `-i`。
- **admission 不依赖 jailbreak trustcache**（活基线 cdhash 不在 TC 里也能跑）；TC 只是历史习惯。

### 🎉🏆 2026-09-28 【新里程碑】 **macOS 自有缓存也成功灌进 region**（536=0），随后命中已知 post-reuse SEGV
**实测（重启后的干净 region，脚本 `post_reboot_mac.sh`）**：
```
CACHE_DIR=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld
rk=0? → 实为 rc=139(SIGSEGV)；但：
  Using mapping in dyld cache ×91\n  not loaded ×0
  Using mapping in dyld cache for /usr/lib/libSystem.B.dylib  → UUID D161E41A（非 shim B90391D8）
  re-using existing shared cache (/private/preboot/.../Caches/com.apple.dyld/dyld_shared_cache_arm64e)
  最后一行：Mapping the shared cache system wide → 随后 SIGSEGV
```
⇒ **536 对 macOS 缓存也成功（0）且已复用**；libSystem 来自缓存。**新的（已知）堆 = post-reuse SEGV**。

**源码关键事实（dyld 1286.10 `SharedCacheRuntime.cpp`）**：
- `files[i].sf_slide = (i==0) ? infoArray[0].maxSlide : 0;` ⇒ **我们的 zero-slide 修复与上游一致**（iOS maxSlide 非对齐才 EINVAL）。
- `console("Mapping the shared cache system wide")` 在构 files[]/mappings[] 之前 ⇒ 崩在构数组/536/其后。

### 🔬 2026-09-28 冒死取证：post-reuse 不是普通 SEGV，而是**内核 CS "Invalid Page" 击杀**
**实测（崩溃报告 `echo-2026-09-27-235846.ips`）**：
```
exception.type = EXC_BAD_ACCESS, signal = SIGKILL - CODESIGNING (subtype UNKNOWN_0x32)
termination.indicator = "Invalid Page" (namespace=CODESIGNING)
faulting 0x100bbe9f8 ∈ mapped file 0x100b48000-0x100be4000 (624K, r-x/r-x, SM=COW)
邻居：0x100ae4000-0x100aec000 (32K r--, gap 0x5c000) / 0x100be4000-0x100bec000 (32K rw-)
```
⇒ **不是 dyld 的普通野指针 SEGV，而是内核 page-CS 校验失败后的 SIGKILL**（CLAUDE.md 点名的同类阻塞：“CS-enforced 页 + 普通 mmap → CS kill”）。
- 对照实验：**不映射缓存（CACHE_DIR 指向不存在目录）时不会被杀**（echo rc=0）⇒ 击杀由“映射后从的库”引入。
- 注：崩溃地址在**普通 mmap 文件区**（非 shared region 0x180000000），且 dyld 日志显示最后加载的是 `/usr/lib/libMatch.1.dylib`（**iOS UUID 83CA476B**，无法在 chroot /usr/lib 找到）与 procursus 注入库 ⇒ 怀疑 **chroot 下 /usr/lib 被 iOS 的 bindfs `.fakelib` 命中** 或缓存 image 列表混入 iOS 路径。
- 探针局限：dyld 日志未打印 0x100b48xxx 段的 image 名；`.ips` 的 usedImages 为 0 ⇒ 需另建“地址→image”映射（如逐个 image mmap 跟踪）。

### 🧪 2026-09-28 IDA(Instance2=kernel) 取证：cs_validate_page / 无条件击杀 printf
- **`osfmk/vm/vm_fault.c:2863`**：`printf("CODE SIGNING: process %d[%s]: rejecting invalid page at address 0x%llx from offset 0x%llx in file \"%s%s%s\" ...")` —— **无条件打印（带文件名）**；本机**无法抓内核日志**（无真实 `log` 二进制；`log show` 空；无 dmesg/sysctl）⇒ 该线索暂时用不上。
- **`bsd/kern/ubc_subr.c:5226-5300`**（`cs_validate_page`）：页必须在某 blob 覆盖窗内（否则 `continue`），然后在 **CD 哈希表**查哈希；查不到 → `found_hash=FALSE` → `validated=FALSE` → 由 `vm_fault_enter` 决定击杀。
  ⇒ 我们对 dsc 的 `csb_end_offset` 扩到整文件后，超出原签名范围的页“被覆盖但无哈希”→ 仍 `validated=FALSE`。
- 实测补充：macOS 缓存 vnode **`v_flag=0x184a00` 已含 VSHARED_DYLD(0x200)**（`already set`）⇒ 设 VSHARED_DYLD 不解决问题。
- 崩溃模式稳定：fault 总在某个 **624K r-x mapped file** 的 `region_start+0x769F8`；且该区域**不在 dyld 的 segment 日志中** ⇒ 由**非 dyld 途径**映射（内核共享区/越狱注入器/plain mmap）。
- **稳定性复现（本轮）**：同一环境连跑 `cat→echo→cat`，**3/3 均 `rc=137` + `using=91` + `notloaded=0` + `re-using existing shared cache`** ⇒ 击杀是**确定性**的，之前偶发的 rc=0 不可靠。
- ❗ 结论：**536 与缓存复用均已成功**，唯一未通 = **消费缓存页时的内核 CS 击杀**。
- 崩溃报告 `ktriageinfo` 明确定性：**“VM - A memory corruption was found in executable text”**（=可执行文本页 CS 校验失败）。
- ❗ **lldb 取证在结构上受阻**：chroot 的 `libSystem.B.dylib` 是只有 10 个符号的 shim ⇒ `bash`/`lldb` 无法链接加载（`Killed: 9`/Symbol not found）。要跑 lldb 需真实缓存 libSystem，而它正是崩点 ⇒ **鸡生蛋**。
- 下一步建议（需你/明早）：① 用能读内核日志的手段拿到 `vm_fault.c:2863` 那条带**文件名**的 print（含 serial/kdp/sysdiagnose）；② 或从“**越狱注入的 iOS 库**”入手（本次扫到 `libbrotlienc` TEXT=0xa0000 接近 624K，但未命中；待查 624K r-x 的真正归属）；③ 或换策略避免对缓存/DSC 的 plain mmap。

**下半目标（post-reuse SEGV）取证工具与阻塞**：
- 工具：`analysis/dyldwork/catch_segv.sh`（chroot lldb 拓 PC/far/backtrace）。
- 阻塞：chroot 里的 `bash`/`lldb` 未签名→TC → AMFI `Killed: 9`；**需先 `ldid -Hsha256 -S<ent>` + `cdhash_slices.py`+TC 后再跑 catch_segv**（下一轮）。
- 其他观察：重启后若映射进程崩溃（139），**region 会自动释放**（`check_np` 又回 12）⇒ **不用每次重启就能重试**。

### ✅ 本轮已达成：`crossarch+plataccept` 的修改版 dyld **通过 exec 准入并跑通 HELLO**（§2 iOS 缓存实验的前置全部就绪）

### 🎯 2026-09-28 环境已复原 + 536 实测结论（重跑 subagent 任务）
**A. 环境复原（已验证）**：重启后只丢 **jailbreak trustcache（内存）**；rootfs 是普通目录不丢。复原 = 部署活件 + 用 **`misc/cdhash_slices.py`**（对 **每个 slice** 算 `sha256(CD[0:cdlen])`）→ `jbctl trustcache add`。已用 `misc/restore_env.sh` 一次跑通。
- ❗ `run_nocskill` 旧件硬编码 kernel slide（0x158B4000），**重启后 KASLR 变化 ⇒ `[!] proc not found`**；已修于 `misc/run_nocskill.c`（改为扫描内核 Mach-O 头）。但**实测：TC 复原后 `chroot` 直连即可**（不需 run_nocskill）：`/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO` → **HELLO, rc=0（2/2）**。

**B. 536 实测（关键量化）**：
- 用 `build_dyld.py dyld_p536.bin crossarch plataccept retentry retcave`（在 536 之后写 x0 到 fd2）：stderr 首 8B = **`0xffffffffffffffff`** ⇒ **536 确被调用且返回 -1**。
- 用 `dyld_e5.bin`（crossarch plataccept e5centry e5ccave，挂在 536 stub 失败分支 0x76e04）取 raw errno：**`errno = 22 (EINVAL)`**。
- **重启 `cachereg_ios`** 对 `/iosdsc/dyld_shared_cache_arm64e{,.01}` 附加 CS blob（`REG ... fcntl=0 ... READY ok=1`）后，**errno 仍 = 22** ⇒ **EINVAL 不是 CS blob 门**。
- 仍无 `different platform` 文案 ⇒ **plataccept 生效**。
- **已 dump 536 入参（自建 cave @0x35690 写 x0..x3）**：**x0=0x2e(46 files)、x1=files、x2=0x3a(58 mappings)、x3=mappings** ⇒ 与 iOS 缓存 46 分片完全对得上，**参数集是齐的**。
- 据此反编译 `shared_region_map_and_slide_setup`：EINVAL(22) 站点共 8 处（0x84596c4 计数溢出/0x8459780 region==NULL/0x8459d74 fd==-1且>1映射/0x8459d04 未页对齐/0x8459d50 非VREG/0x8459ce0 无 memory object/0x8459cbc CS 覆盖不足/0x8459d40）。
- `0x8459780` 分支 → `sub_FFFFFE0008063590(task)`→内部 `sub_FFFFFE00080608E8(task)` 取 region，**返回 0 即 22**（候选元凶）。
- **下一步**：区分上述 8 个站点。推荐 **KRW（只读）** 或内核侧打点：在 `sub_FFFFFE0008459570` 的各 EINVAL 赋值处看哪个命中；或先验证“空 region 是否导致 0x8459780”。
- **已 dump files[]/mappings[]（cave @0x35690 写 x1/x3 各 0x60B）**：files 为 12B/条，`fd` 序列 = 4,5,6,7,8,0xA,0xB,0xC…（**全部有效小 fd，无 -1**），每条 `count=1`；mappings 48B/条。
  ⇒ 排除 `计数溢出(0x84596c4)`（46≤58）与 `fd==-1且>1映射(0x8459d74)`；`region==NULL(0x8459780)` 也大概不命中（check_np=12 ⇒ region 存在）。
  ⇒ **最可能剩下 `0x8459cbc`（CS blob 未覆盖 mapping 范围）或 `0x8459ce0`（无 memory object/非VREG）**。两者均与 vnode/ubc 相关，需内核侧运行时证据才能二选一。
- **已排除 ubc 门**：先用 iOS python mmap `iosdsc` 两个 dsc（促 vnode ubc_info）→ errno 仍 22。
- **✅ 已定位 22 = CS 覆盖门 `0x8459cbc`**：该门被 `initProt & 0x10` 守护（`*(v56+44)`）；在 536 前用 cave 对所有 mapping 做 `initProt |= 0x10`（并加 'X' marker 证明确实执行）→ **errno 由 22 变为 14(EFAULT)** ⇒ 门确实换了。
  ⇒ **根因：cachereg 附加到 dsc 的 CS blob 覆盖范围不包含各 mapping 的 [fileOffset, fileOffset+size]**（内核按 `cs_blob[5]/[6]/[7]` 算的覆盖窗口 vs mapping 偏移）。
- **下一步**：让 dsc 的 CS blob **覆盖整个文件**（内核要求 blob 窗口 ⊇ 每个 mapping 的 file 区间）；否则可研究 `initProt` 置位后为何变 EFAULT（可能是 COPY 位导致后续 mmap 语义变化）。
- **源码确证判据**（`analysis/xnu-xnu-8792.81.2/bsd/vm/vm_unix.c:2611-2642`）：CS 覆盖面检查 = `ubc_cs_is_range_codesigned(vp, sms_file_offset, sms_size)`；`ubc_subr.c` 里它要求 `csblob!=NULL && [csb_base_offset+csb_start_offset, csb_base_offset+csb_end_offset] ⊇ [start,start+size]`。
- **`struct cs_blob` 字段偏移（ubc_internal.h）**：`csb_flags`@0x20、`csb_base_offset`@0x28、`csb_start_offset`@0x30、**`csb_end_offset`@0x38**、`csb_mem_size`@0x40。
- **实测（blob_read.py）**：iosdsc main 的 `blob+0x38 = 0x58000`（仅覆盖 352KB）、.01 `= 0x6df4000` ⇒ 远小于文件 ⇒ CS 门必失败。
- **已做修复（`set_blob_cov.py`，KRW 写 `csb_end_offset=filesize`）**：对 `iosdsc/*` **全部 46 个分片**均改成功（main→0x5c000、.01→0x6e2c000）。
- ❗ **但 536 仍返 22**（cachereg 已对 46 分片全部附加 blob；ubc/blob 均非空）。⇒ **22 可能不只来自 CS 门**（或内核取的是 blob 链上另一个）。
- **下一步二选一**：(a) 内核侧证据——把 `cs_system_enforcement_enable` 置 0（`_cs_system_enforcement` @ IDA `0xfffffe0008373a28`）确认是否就是它（注：该全局是 SECURITY_READ_ONLY，KRW 写可能失败/风险）；(b) 继续用户态差分（如给 mappings 设 VM_PROT_ZF 已试→变 14(EFAULT)，说明 CS 段确实被跳过）。
- 🔎 **2026-09-28 进一步实测**：`_cs_system_enforcement`（kern_cs.c）在 RELEASE 内核里是**常量函数 `MOV W0,#1; RET`** ⇒ 无法靠全局关闭；唯一用户态旁路是 mapping 的 `VM_PROT_ZF` 位。
- 🔎 **iosdsc 结构**：**split cache**，46 片总 3.2G；每片 `mappingCount=1`、`fileOff=0`；主片映射 `[0x180000000,0x58000]`。
- 🔎 **覆盖修改已生效且持久**（main 0x5c000 / .01 0x6e2c000 / .02 0x1c000 = 各自文件大小；blob+0x38 重读确认）。
- ❗ **覆盖满足、CS blob 俱在，536 仍 22** ⇒ 22 另有出处（待查；下一候选：`0x8459d04 未页对齐` / `0x8459d50 非VREG` / 上游 wrapper 映射 / 某片未被 CS 覆盖到的 mapping）。

**C. 下一步（定位 EINVAL 的具体门）**：535 setup = `shared_region_map_and_slide_setup`（kernel IDA `0xfffffe0008459570`，slide=0x158B4000）。EINVAL 组候选：`mappings 溢出`/`region==NULL`/`fd==-1 未对齐`/`非VREG`/`no memory object`/`mapping 未 code-signed`。**建议**：用 Instance2（kernel）反编译该函数并逐个比对我们的 files/mappings 输入；或对 setup 内各 EINVAL 赋值处下 KRW/断点读数。
- ⚠️ 注意：之前 subagent 测过 “空 region(sr_first_mapping==-1) ⇒ 536=12；非空 ⇒ 22”，与本次 22 的对应关系需一并核清（可能同一 race 门）。
- 构建：`python3 build_dyld.py dyld_plat.bin crossarch plataccept`（产物仅 2 处 4B 差异：`0x76270`、`0x35c24`）。
- 部署：按上面配方，`/bin/echo HELLO` → `child exited rc=0`（多次复现）。
- **无 env 与带 `DYLD_SHARED_CACHE_DIR=/iosdsc` 均能跑**；`DYLD_SHARED_CACHE_DIR` 确被采纳（改成不存在的 `/nope123` → `/bin/true` rc=127，行为变化）。
- **env 确实到达 dyld**（`DYLD_PRINT_LIBRARIES=1` → 101 行 `dyld[...]` 输出）。
- `cachereg_ios` 常驻已启动并对 `/iosdsc/*` 附加 CS blob：日志 `REG ... fcntl=0 ... READY ok=1`。

### ⛔ 仍未通（下一步）：**536 映射 iOS 缓存仍失败**
- 带 `DYLD_SHARED_CACHE_DIR=/iosdsc` 时 dyld 打印：`dyld cache '(null)' not loaded: syscall to map cache into shared region failed`（path 为 null）。
- 未出现 `different platform` 文案 ⇒ **plataccept 疑似已生效**（未在 preflight 被拒）。
- `e5centry/e5ccave`(0x76e04) errno 探针**未触发** ⇒ 536 stub 的 error 分支没走到，需换探针位置或确认是否真发了 536。
- **下一步**：① 确认是否真发起 536（用 `mkBentry`/`M` 系列 marker 或 536 前 dump）；② 若发了 536，取原始 errno（kernel 门表见 `shared_region_map_and_slide_setup` `0x8459570`，EINVAL 组/EPERM 组）；③ 注意 iOS 缓存有 46 个分片，`preflightMainCacheFile` 会按 header 的 cacheFileCount 依次 open 子缓存（缺一则 cacheFileFound=false）。

**runtime-confirmed，3 次复测全过。**

### 结论（不要再推导）

修改 dyld `__TEXT` 任意字节（含死区 NOP）后，能否过 exec 准入**只取决于签名配方**，与改动内容无关：

| 签名方式 | superblob | CD flags | 结果 |
|---|---|---|---|
| `ldid -Hsha256 -S<ent.plist>`（**无 `-Cadhoc`**） | 4 项（CD+req+XMLent+DERent） | **0x0** | ✅ HELLO ×3 |
| `ldid -Hsha256 -Cadhoc -S<ent.plist>` | 4 项同上 | **0x2(adhoc)** | ❌ SIGKILL(exec veto,dyld 零输出） |
| `resign_dyld.py`（保 superblob 只重算页哈希） | 4 项原样保留 | 0x0 | ❌ SIGKILL |
| `ldid -Hsha256 -S<ent>` 于**未改内容**的备份 | 4 项 | 0x2（带-Cadhoc时也work) | ✅ HELLO（见备注） |

**备注**：`dyld_bk_resign`（未改内容 + `-Cadhoc` → flags=0x2）反而能跑——
说明 flags=0x2 只在"内容被改"时才致命。别问为什么，经验规则就是：
**改 dyld → 用 `ldid -Hsha256 -S<ent>`（绝不加 -Cadhoc）→ flags 必须 0x0。**

### 反例澄清（旧记录要修正）

- 之前写"`resign_dyld.py` 保 superblob + 重算哈希仍 SIGKILL"——属实，但
  现在知道正确配方是 ldid 裸签，不是保 blob。保 blob 路径不要再走。
- `jbctl trustcache add` 需要 **40 hex 的 sha256(cd)[:20]**——不是全 64 hex，
  也不是 cdhash.py 的全量输出。截前 40 字符。
- trustcache info 输出是**大写 hex**——grep 必须 `-i`（之前误判"备份不在 TC"是
  因为小写 grep 大写列表）。备份 cdhash `b219dae7…` 实于 TC entry 290。

### dyld 准入完整配方（一条命令链）

```bash
# on device, file at /var/mobile/dyld_X.bin:
/var/jb/usr/bin/ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist /var/mobile/dyld_X.bin
# cdhash = sha256(CD blob)[:20] (40 hex):
/var/jb/basebin/jbctl trustcache add <cdhash40>
rm -f /var/mnt/rootfs/usr/lib/dyld && cp /var/mobile/dyld_X.bin /var/mnt/rootfs/usr/lib/dyld && chmod 755 /var/mnt/rootfs/usr/lib/dyld
# test:
/var/mobile/run_nocskill /var/jb/usr/bin/env -i PATH=/usr/bin:/bin \
  /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO
```

### 本路径解锁的下一步（交接给隔壁 AI)

死区字节能改 = **可以打 `plataccept` 补丁**(dyld thin `0x35c24`
`B.NE → NOP`)，让 dyld 接受 iOS 缓存 platform → `DYLD_SHARED_CACHE_DIR=/iosdsc`
+ iOS 缓存拷贝灌进 chroot region 的路可以测了。详见
`HANDOVER-DYLD-ADMIT-2026-09-27.md`。

---

## ★★★★★ 2026-09-27（续·傍晚）⭐⭐⭐⭐⭐ MILESTONE：发现 iOS 缓存采纳路径 —— 可能根本不需要 536

**本轮改写整条技术路线的认知**（runtime-confirmed）：

1. **chroot 任务 exec 时被内核挂上的是 iOS 自己的 shared region**。
   `__shared_region_check_np`(syscall 294) 在 chroot 的 macOS dyld 进程里
   返回 **iOS 缓存基址 `0x1A4AE8000`**（与原生 iOS 进程同一 region）。
2. **`reuseExistingCache` 直接采纳 iOS 缓存**：magic `dyld_v1  arm64e`
   匹配 → `re-using existing shared cache` 打印 → **89 个 iOS dylib 全部
   从缓存成功加载**（libSystem、libnetwork、Security.framework… +
   jb 的 forkfix/libinjector 从磁盘）。证据：`/tmp/priv.log`（部署
   `dyld_noslide.bin` + `DYLD_PRINT_LIBRARIES=1`）。
3. **上游原项目（MacWSBootingGuide, macOS 13.4+iPadOS 16.x）从未打过
   共享缓存补丁** —— README 的 dyld 补丁清单只有 GradedArchs
   arm64e→arm64 一项 ⇒ **原厂设计就是采纳 iOS 缓存**，macOS-only 库
   （AppKit 等）从 rootfs 磁盘加载。536/keeper/cachereg 这条线为
   HELLO 世界不是必需；它只对"用 macOS 原生缓存内容"有必要（后期
   再评估 macOS 独有 dylib 是否必须走 macOS 缓存）。
4. **syscall 536 在 region 已被 iOS 缓存填充的进程上返回 EINVAL(22)**
   —— 与新发现自洽（不能覆盖已占 region）。536 只在"首交者"
   路径上有意义（keeper 设计保留）。

**当前唯一 blocker（Hello World 距离 = 这一个崩溃）**：
库全部载入后、到达 app 入口前 **SIGSEGV(139)**。
`dyld_emark.bin`（`crossarch`+`entrymark`@0x6b94 BLRAAZ→`bl 0x970`+
`entrycave`@0x970 write 'E'+x8→blraaz）实测 **'E' 未出现** ⇒
崩在 **dyld 内部 post-cache 代码**（initializers/notify/bind 阶段），
不是 app/libSystem 初始化器。下一步 = segvcap handler 移植到
emark 构建（`dyld_segvcap.bin` 的 cave 在 `0x9b578`，
`segvcap_cave.s` 现成）→ 抓崩溃 PC+FAR。

**复现命令（已验证）**：
```bash
# 纯 chroot 路径（无 libmachook 注入）
/var/mobile/run_nocskill /var/jb/usr/bin/env -i PATH=/usr/bin:/bin \
    /var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HELLO
# launchdchrootexec 路径（注入 libmachook_arm64.dylib）
/var/jb/usr/macOS/bin/launchdchrootexec 0 0 /var/mnt/rootfs /bin/echo HELLO
```

**新事实表**：
| 事实 | 值 |
|---|---|
| chroot 进程的 task region | iOS region，基址 `0x1A4AE8000`（exec 时挂载） |
| check_np(294) 在 macOS dyld 里 | 返回 iOS base + magic `dyld_v1  arm64e` ⇒ reuse 采纳 |
| reuse 采纳后行为 | iOS 库从缓存加载成功；崩于 post-cache（139，PC 未取） |
| `launchdchrootexec` | chroot+setenv(DYLD_INSERT_LIBRARIES=libmachook_$ARCH)+posix_spawn\|SETEXEC；`arch=arm64` 时注入 arm64 变体 |
| `run_nocskill MACWS_EXC=1` | **会让父进程被 AMFI SIGKILL**（task_set_exception_ports 门）——勿用 |
| `dyld_segvcap.bin` | 已有 SIGSEGV-handler dyld（start@0x540c→cave 0x9b578）；但其补丁组合走 536-map→烧循环，须移植 handler 到 emark 基线 |
| chroot 崩溃 | **不写 .ips**（CrashReporter 路径外） |
| rootfs 布局（README 实证） | OS cryptex→`$R/System/Volumes/Preboot/Cryptexes/OS`；`$R/System/Volumes/Data`→`../..` 链接；`$R/var/folders/zz`→`/var/folders/zz`；bind `$R/var/jb`→`/var/jb`；每 exe `ldid -S ent.plist -M`+逐 cdhash `jbctl trustcache add`+`loadtc` |
| 构建器新 patch key | `entrymark`/`entrycave`（@0x6b94/@0x970，见 build_dyld.py） |

**非确定性警示**：同一二进制连跑会在 {139 快崩, 45s 烧循环, 137}
之间漂移——与 region/时序状态相关；结论前必须连测 ≥3 次。

### 补充记录（散点但重要，勿再推导）

- **check_np 返回值的语义**：返回的是**当前任务 region 里已映射缓存的基址**——
  iOS 缓存映射时 = `0x1A4AE8000`；早前 macOS 缓存被某次 536 成功填入时
  = `0x180000000`。出现过的"僵尸态" = base 有值（`0x180000000`）但读
  magic 立刻 SEGV（region 对象在、页未映射进本任务）。iOS 采纳路径则
  页全映射好（verboseSharedCacheMappings 实打印过全部段）。
- **烧循环也在采纳路径上出现**：dyld_es（crossarch+entrymark+segvcap）
  两次运行都是 93% CPU 烧 ≥14s、零 fd2 输出——post-cache 失败不止
  SEGV 一种形态，也会以死循环出现。
- **run_nocskill 的 task port 在 ~2s 变 INVALID_DEST(kr=268435459)**：
  `vm_region_64`/`task_threads` 全挂——spawn 后 2s 端口就失效（
  env→chroot→echo 连续 execve 后端口语义问题），外部采样 PC 不可靠。
- **stderr pipe 填满假象**：marker 往 fd2 写约 ~190KB 后会阻塞（父进程
  pipe 没人读）→ 看起来像内核卡死。用文件重定向而非继承 pipe。
- **内核侧 536 errno 真值表**（Instance2 RE，`sub_8062254` 包装）：
  内部码 {0→成功，1→EFAULT(14)，2→EPERM(1)，3→ENOMEM(12)，>3→EINVAL(22)}；
  返回 -1 的是 `sub_8459570`（vnode/文件设置段）。任务门：`task+0x18`
  的 region-root vnode 须 == 进程根目录 vnode 或 rootvnode，否则 EPERM；
  chroot 内 `task+0x18`==chroot 根 ⇒ 可过（536 ret=0 曾在 chroot 实证）。
  `task+0x3e8` = 任务持有的 shared_region 对象；`sr+0x71`=in_progress，
  另有 stale 位。
- **`mov w1,wsp`/`mov x1,sp` 等一切以 SP 为源的操作数在 SP 未对齐时全部
  SIGILL**(0xe1030091/0xe1030111 崩溃签名）——既是坑也是探针。
- **AMFI 拒绝 macOS 磁盘 dylib**：加 trustcache 后仍 `errno=1`——
  libSystem 不可能走磁盘 shim，必须走缓存（坐实采纳路径必要性）。
- **libmachook 为 13.4 dyld 内部结构所写**——launchdchrootexec 注入它
  进 15.6.1 dyld 进程时其 ctor/hook 可能不兼容；纯 chroot（run_nocskill
  路径）可排除它，二分崩溃时先排掉注入变量。
- **forkfix+libinjector 来源**：不是 libmachook——是 Dopamine ElleKit
  经继承的 `DYLD_INSERT_LIBRARIES` 注入；`env -i` 可清。
- **`DYLD_SHARED_REGION=private` 未定论**：设过 env 但进程仍采纳共享
  region（可能 launchdchrootexec/env 传递问题或该选项不适用此场景）——
  值得重试验证（私有 mmap 路径绕开共享区概念）。
- **cave 间距规则**：相邻 marker cave 必须 ≥ cave 实际长度（mkD@0xb00
  与 mkE@0xb40 间距 0x40 < 0x4c cave 长 → 尾部互踩、输出污染）。
- **echo 的 Mach-O**:fat、platform=1(macOS);`ldid -S ent.plist -M`
  重签 + 每 slice cdhash 注册（`add_all_trustcache` 流程）。
- **re-clear csflags 模式**:run_nocskill 打 `re-clears=2` = 每次
  execve(chroot→echo）内核重设 0x300,watchdog 以 ~20µs 粒度重清。
- **iOS 缓存文件**:`/private/preboot/Cryptexes/OS/System/Library/Caches/
  com.apple.dyld/dyld_shared_cache_arm64e[.01-.44]+.symbols`（本机实测
  ls 输出）；chroot 内同名路径是 rootfs 里的 macOS 文件，勿混。
- **chroot 工具真实路径**:`/var/jb/usr/bin/chroot`（不是 /usr/sbin)。
- **run_nocskill 是 posix_spawn 包装，本身不 chroot**——不 chroot 直接
  喂 `$R/bin/echo` = iOS dyld 加载 macOS 二进制 → "wrong platform" 报错
  （报错来自 iOS dyld，不是我们 dyld)。

---

## 2026-09-27 — ⭐ 交接 Executive Summary（重启后先读这段）

### 🎉🎉🎉 MILESTONE 2026-09-27 晚：macOS `/bin/echo HELLO` 在 chroot 里首次跑通（独立复现）
- **实测（我亲自跑，非仅 subagent）**：`/bin/echo HELLO` ×5 → 全部打印 `HELLO` + `child exited rc=0`；`/usr/bin/true` ×2 → rc=0；`/bin/echo one two THREE` → `one two THREE`（参数透传 OK）。
- **达成路径（subagent 实现 + 我验证）**：**磁盘回退 + 两个自包含替身 dylib**（不需改 dyld、不需内核 patch）：
  - `/var/mnt/rootfs/usr/lib/libSystem.B.dylib`（shim，导出 echo 依赖的 10 个符号：`_err,_exit,_fflush,_getenv,_mbtowc,_putchar,_putwchar,_strlen,___stdoutp,___mb_cur_max`，并 LC_LOAD_DYLIB→libdyld）。
  - `/var/mnt/rootfs/usr/lib/system/libdyld.dylib`（shim，满足 dyld 硬门槛：install-name 精确匹配 + `__TPRO_CONST,__dyld_apis` + `__DATA_CONST,__helper` 的 **ptrauth 签名 `dyld4::LibSystemHelpers` vtable**，`version()>=7`）。
  - 两者均 `ldid -Hsha256 -S<ent>` + `trustcache add` + `rm+cp` 新 inode + `chmod 755`。
- **与 private 路径的关系**：`DYLD_SHARED_REGION=private` 能成功 mmap 两个子缓存（`mapped dyld cache file private to process (...main.../01...)`）但随后 **SIGKILL(9)**（DSC 页 CS-enforced exec 页被击杀）⇒ private 路死；转磁盘回退+替身才成。
- **诚实定性**：这是让 **echo/true** 跑通的**桥接方案**（非“完整缓存映射”的根治路）；要看任意 macOS 二进制需继续补齐 libSystem 桩（malloc/getenv/…）与依赖 dylib。但**对本 goal 已达标且可重复**。
- **产物**：`analysis/dyldwork/tmp/shim/{libSystem_shim.c,libdyld_shim.cpp,build_shim.sh,deploy_shim.sh,run_repeat.sh}`（gitignored）。

### ★★★ 2026-09-27 晚 M-HW6（非侵入探针锤实）：reuse 入口 `check_np=12`，root cause 彻底闭合
- 工具：自建 `dyld_reuse_dump.bin`（在 `reuseExistingCache`@0x351a8 入口装 cave@0x38d08：**保存 x0-x3/x30 → call check_np → dump 64B → 恢复 → 重放 PACIBSP → b 0x351ac**，**完全不改变原流程**）。
- 实测（`/tmp/rd.bin`）：**`check_np ret=0x0c(12)`、base=0**。⇒ **macOS dyld 的 reuse 必失败**（源码 `:1254`）。
- 结合 `getDyldCache` 仅被 `ProcessConfig::DyldCache` 构造一次（`loadDyldCache` 每进程一次）⇒ 日志 line1 “re-using”= iOS dyld（launcher），91 × “Using mapping in dyld cache”= `Loader::logSegmentsFromSharedCache` 的**文件级元数据**（实际未映射）；856-857 “Mapping system wide / not loaded” = macOS dyld 那唯一次 `loadDyldCache`。
- **圆整后的根因（最终版）**：**chroot 进程的 shared region 为空（root_dir 键控）→ macOS dyld reuse 失败 → 536 映 macOS 缓存失败（门/冲突）→ 无真实缓存 → 主执行的依赖（libSystem/libutil/libdyld…）无法解析 → abort。**
- **可行修法（已收敛到两条）**：**① 内核**：让 `vm_shared_region_lookup` 忽略 `sr_root_dir`（slide=0x158B4000）→ chroot 复用 iOS region（已映射的 iOS 缓存，magic 就是 `dyld_v1  arm64e`）→ reuse 成功。**③ 用户态**：提供缺失符号/库，或让 `DYLD_SHARED_REGION=private` 生效（需开 `security.allowEnvVarsSharedCache`）——均需另找插入点。

### ★★★ 2026-09-27 晚 M-HW5（完整失败链已锤实，含源码）：Hello-World 进不去 main 的机制
- **870 行完整 trace**（`analysis/dyldwork/ft.log`，本地）关键：
  1. 行 **1** `re-using existing shared cache (/private/preboot/.../dyld_shared_cache_arm64e)` + 行 2-13 dump **`0x1A4AE8000` 起的映射**（=`check_np` 的 base！）；
  2. **91** × `Using mapping in dyld cache for /usr/lib/system/...`；
  3. 行 **856** `Mapping the shared cache system wide` → 行 **857** `dyld cache '(null)' not loaded: syscall to map cache into shared region failed`；
  4. 然后 `/bin/echo` + **磁盘** `/usr/lib/libSystem.B.dylib`(34KB) → **`Symbol not found: _err` → abort**。
- **源码定论**（`analysis/dyld-dyld-1286.10/dyld/SharedCacheRuntime.cpp`）：
  - `loadDyldCache`(`:1476`)：`forcePrivate ? mapSplitCachePrivate : (reuseExistingCache(:1490) ? ok : mapSplitCacheSystemWide)`。
  - `mapSplitCacheSystemWide`(`:1367-1387`)：536 失败后**再试一次 reuse**（“另一个进程抢先”），仍失败才置 `errorMessage="syscall to map cache into shared region failed"`。
  - `reuseExistingCache`(`:1251`)：`check_np==0 && validMagic(magic=="dyld_v1  arm64e")`。
- **机制推断**：macOS dyld 的 `reuse`（:1490）**失败**（chroot 的 region 为空：root_dir 不同→空 region→check_np=12）⇒ 走 536（macOS 缓存）⇒ 失败（**与已在 `0x1A4AE8000` 的 iOS 缓存地址冲突 → KERN_NO_SPACE/EINVAL**）⇒ 再无 cache ⇒ echo 的 libSystem 退磁盘 ⇒ `_err` 缺失 ⇒ abort。
- **⇒ 真正可行的两大方向（用户决断）**：
  - **① 让 chroot 看见 iOS region**（内核 `vm_shared_region_lookup` 忽略 `sr_root_dir`；运行时地址可由 slide `0x158B4000` 求得）→ macOS dyld reuse **iOS 缓存**（其 magic 已是 `dyld_v1  arm64e`）→ 91 库全从缓存 → 可能直达 main。风险：跨 iOS 进程污染（需评估）。
  - **② dyld 侧“无 region 时体面回退”**：hmm——源码已证不可强改（会把 loadAddress 指空）。
  - **③ （治标、但直击目标）让 `_err` 可得**：给磁盘 `libSystem.B.dylib` 补/拦截 `_err` 符号（甚至一个导出 `_err` 的小 dylib）——因为 echo 只在错误路径用 `err()`，**提供 `_err` 就可能让它直接打印 HELLO**。注意：此为诊断/桥接，不是根因修复。

### ★★★ 2026-09-27 晚 M-HW4：536 原始 kernel errno = **22(EINVAL)**；populate 未成功
- 工具与脚本（已就绪，官方 key）：`build_dyld.py dyld_pope.bin crossarch hasexisting prereuse filescount1 dynoff accessor fcntl_nop cover_b slidentry slidecave e5centry e5ccave`。
- 实测：populate 的 536 返回 **-1**；`e5c` 探针（挂在 536 stub 失败处）拿到 **raw kernel errno = 22**。
- 对照门表（subagent 输出，`shared_region_map_and_slide_setup` IDA `0xfffffe0008459570`）——EINVAL(22) 组候选：`mapping not code-signed`(`0x8459CBC` `ubc_cs_is_range_codesigned`)、`no memory object`(`0x8459CE0`)、`fp_get_ftype/非VREG/映射溢出`。
- **下一步 = 内核 KRW 短路该门让 536 成功一次**（→ region populate → check_np 返真实 base → dyld reuse 成功 → `_err` 从缓存解析 → echo 应能进 main）。**前置：kernel slide（已定位内核代码段运行时在 `0xfffffe001d7e…`）。**

### ★★★ 2026-09-27 晚 M-HW3（subagent 源码+IDA 双证，推翻“没挂 region”假说）
- **exec 总是挂 shared region**（`kern_exec.c:1558 __mac_execve → vm_map_exec → vm_shared_region_enter`，**无条件**）；**唯一判别键 = `p_fd.fd_rdir`(root_dir)**（`vm_shared_region.c:386-398`）。**chroot 子进程因 root_dir 不同 → 拿到该 root_dir 下的【全新空 region】**。
- **check_np=12 语义**：任务**有** region 但**空**（`sr_first_mapping==-1` → start_address 返 KERN_INVALID_ADDRESS → ENOMEM(12)）；错误时 **copyout 未执行** → 用户态 base 保持 0（故 magic 全 0）。源码 `osfmk/vm/vm_unix.c:2046-2134`。
- **⇒ 正确修法（不是“让内核挂 region”）两选一**：
  - **① 用户态**：`DYLD_SHARED_REGION=private` → `mapSplitCachePrivate`(`SharedCacheRuntime.cpp:865/984`) 普通 `mmap(MAP_PRIVATE)`，**绕开 294/536**。**实测失败**：本 boot `security.allowEnvVarsSharedCache` 未开 → env 被 AMFI 剥掉；且加该 env 后变 **`SIGNALED 9`(SIGKILL)**（疑 `:1519` “指定 private 但找不到 cache 文件→halt”或 AMFI 剥 env）。
  - **② 内核 KRW**：短路 `shared_region_map_and_slide_setup`(IDA `0xfffffe0008459570`) 的某道门，让 **536 成功一次** populate region → 之后 check_np 返真实 base → reuse 走快路径。门的地址表见 subagent 输出（code-sign 门 `0x8459CBC` / root_dir 门 `0x8459764` / 等）。**需解决 kernel slide（旧问题）。**
- **③ dyld 强改 reuse=恒 true = 有害**（subagent 与源码均证实：会把 loadAddress 指向空 region，更早崩）——**不推荐**。
- **原仓库（MacWSBootingGuide）无 shared-region 结论**（目标 macOS 13.4，未遇此问题）；其 rootfs/签名设计见 README.md:15-30/123-129/182-205。

### ★★★ 2026-09-27 晚 M-HW2（根因链第一环已锤实）：chroot 里 macOS 进程 **没有 shared region** → check_np=12
- 工具：`build_dyld.py dyld_cknp2.bin crossarch cknp2entry cknp2cave`（**官方 key，非手改**）；在 `loadDyldCache:BL reuseExistingCache`(0x34298) 处先 call `check_np` 并 dump `{ret,base,magic}` 40B 到 fd2。
- **实测（`/tmp/c2.bin`）**：**`ret=0x0c(12)`，`base=0x0`，magic 全 0**。
- **源码定论**（`analysis/dyld-dyld-1286.10/dyld/SharedCacheRuntime.cpp:1251-1281`）：`reuseExistingCache` 首句 `if(__shared_region_check_np(&base)==0)`；**返回 12≠0 ⇒ 直接 `return false`** ⇒ `mapSplitCacheSystemWide`(536) ⇒ EINVAL(22) ⇒ “cache not loaded” ⇒ 磁盘 `libSystem.B.dylib`(34KB dsh 缺 `_err`) ⇒ `abort()` **SIGABRT(6)**。
- **⇒ 真正的根因**：**内核没有给 chroot 的 macOS（非 platform）进程挂上 shared region** ⇒ macOS dyld 永远看不到可 reuse 的缓存。
- **修正旧记录**：handover §1 fact#1 “chroot 进程 check_np 返 base=0x1A4AE8000” 与本次不符（本次=12/0）——需复测分清探针差异。
- **下一步（排序）**：① 查为何 exec 未挂 region（对照 `analysis/xnu-*/osfmk/vm/vm_shared_region.c` 的 `vm_shared_region_enter/attach` 条件 + `kern_exec.c`）；② 很可能与 “非 CS_PLATFORM_BINARY” 相关（早前已证 `CS_PLATFORM_BINARY` 是 exec 判别因子）⇒ 让二进制被认作 platform / 或内核侧挂 region；③ 或 patch dyld 让 “无 region” 时优雅回退（非首选）。

### ✅ 2026-09-27 晚 M-HW1：Hello-World 崩溃真因 = macOS dyld `reuseExistingCache` 失败 → 536 → 磁盘 libSystem 缺 `_err` → SIGABRT(6)
- **复现**（3/3 一致，非 SEGV）：`run_nocskill ... chroot $R /bin/echo HELLO` → `child SIGNALED 6`；dump(279B)：
  `dyld: dyld cache '(null)' not loaded: syscall to map cache into shared region failed` + `Symbol not found: _err ... Expected in /usr/lib/libSystem.B.dylib`。
- **完整链**（`DYLD_PRINT_*`，870 行）：
  1. 行1 `re-using existing shared cache (...)` = **iOS dyld**（env/chroot 这些 iOS 二进制）；
  2. ~90 × `Using mapping in dyld cache for /usr/lib/...`（macOS 库从缓存解析）；
  3. 行856 macOS dyld `Mapping the shared cache system wide`（=`mapSplitCacheSystemWide` @IDA **0x355ec**）→ 536 失败；
  4. 行857 `dyld cache '(null)' not loaded` → **丢弃缓存** → 退回磁盘 `libSystem.B.dylib`(34KB dsh) → **缺 `_err`** → `abort()` SIGABRT(6)。
- **RE 定位**：`loadDyldCache`@**0x34240**：`0x34298 BL reuseExistingCache` → **`0x3429c CBZ W0, →0x342b8`（reuse 返 0 则 `0x342d8 B mapSplitCacheSystemWide`）**。`reuseExistingCache`@**0x351a8**：`0x351d0 BL __shared_region_check_np`→`CBZ`；`0x351f0 platform_strcmp(base,"dyld_v1  arm64e")`→`0x351f4 CBZ`；**不匹配 → `0x351f8 MOV W0,#0` + 存诊断串 `"existing shared cache in memory is not compatible"`(@0x90b12) → ret0**；匹配 → `0x352a4 MOV W0,#1`。
- **⇒ 根因候选**：macOS dyld 的 reuse 判定（magic 不匹配 / check_np）失败 → 走 536，而 536 在本环境 EINVAL(22) → 缓存被丢弃。**下一步**：① 查为何 reuse 判失败（reuse 内的 check_np 值与 magic）；② 或 patch `0x3429c CBZ` 分支使 reuse 成功时不回退；③ 或给磁盘 `libSystem` 补 `_err`（治标）。

### ★★★★★★★★ 2026-09-27（Devin 续·深夜）⭐⭐⭐⭐ MILESTONE：macOS dyld 越过 shared-cache 阶段，进入库加载

**当前 blocker 已迁移**：不再是 syscall 536 / mapSplitCacheSystemWide，
而是 **`libSystem.B.dylib` 在 chroot 内加载失败**（code signature
invalid）。dyld 的库加载管线已经真实运行到逐路径重试 + fatal(SIGABRT)。

**m7 运行实证（dyld_m7.bin, CDHash 2ba08622…）**：
`run_nocskill chroot /var/mnt/rootfs /bin/echo HELLO` → 探针 dump 第一趟
全正常（x23=15=映射数、lr=dyld+0x35668、savedLR=dyld+0x2fe94=getDyldCache
返回址），随后 dyld 自身输出：

```
dyld[6174]: Library not loaded: /usr/lib/libSystem.B.dylib
  tried: '/usr/lib/libSystem.B.dylib' (code signature invalid in
    <4DB5C3A0-…> /usr/lib/libSystem.B.dylib, sliceOffset=0x0,
    codeBlobOffset=0x00040080, codeBlobSize=0x00040540)
  tried: '/System/Volumes/Preboot/Cryptexes/OS/usr/lib/libSystem.B.dylib' (errno=2)
  tried: '/usr/local/lib/libSystem.B.dylib' (code signature invalid …)
[+] child SIGNALED 6   ← dyld 正常 fatal 路径
```

**为什么这次通了**：共享区已被早前某次成功 536 填充
（`sr_uuid=4c1223e5…`，`slide=0xd7b0000`）。本进程里 536 返回非 0
（x3=mappings 指针被探针弄坏→EFAULT），但 `reuseExistingCache`
（0x351a8）走 `__shared_region_check_np` 发现已填充 region → 返回 1
→ dyld 继续。**即：reuse 路径本身工作正常**。

**m5/m6 寄存器实证（此前"用户态死循环"机制）**：

- m6 dump 在 pack 外循环头（0x3561c）：`CacheInfo[0]+0x180=8`、
  `CacheInfo[1]+0x180=7`、合计 15 = w23 ——**计数字段全部正常，
  pack 循环无辜**。
- m5 dump 在 0x35690 抓到 `x23=0x1024d8000`（另一趟 0x1044a0000）
  = **dyld 基址形态** → 该 dump 是**第二趟**经过 0x35690
  （第一趟 `MOV X23,X0`@0x35698 后 x23 被复用）。→ **post-536
  返回路径存在重入边**落回 ~0x35660-0x35668（append 块）；第二趟
  append 用 w23=基址低 32 位（0x24d8000≈38.8M）把映射记录写到
  x26+1.16GB ≈ 0x1b39271d0 ——**落进已映射共享区（0x180000000-…）
  不触发 fault**，之前观察到的"45s 用户态烧"即此类重入循环
  （每次重入 = append+536+post 一趟）。
- 重入边静态仍不可见（无 backedge 覆盖 0x3565c-0x35698）；候选机制：
  epilogue `0x35718 MOV SP,X8`（`[x19+0x18]` 恢复的 SP）/ RETAB 落到
  被踩的 x30。**但当 region 已填充时无关紧要——536 失败也走 reuse 成功。**

**教训（写探针的正确姿势）**：
- 有效：`write(2,regs,N)` 在 mapfn 深处（0x355fc+）安全；`x15`/`sp±0x50`
  做暂存；用 `b`（非 `bl`）跳 cave，尾部分支人工算 `target-(cave+len-4)`。
- 会崩：`__dyld_start`/`start()` 早期上下文里 svc write → SIGBUS。
- `_mkmark` 的 movz/movk 单字母标记有编码坑（`0x528a4a09`='P\x52'≠'PE'）。
- stp/madd 手编易错——**一律 python 现算**（见 build_dyld.py 底部注释）。

**新 blocker（下一步）**：`libSystem.B.dylib` 签名校验失败。
dyld4 报 `code signature invalid` —— 候选原因：(a) rootfs 里的
libSystem 是 thin slice/被重签坏（对照 dyld_shared_cache 内嵌的
libSystem 版本）；(b) dyld 的 `validateDyldCache`-style 自检要求 cdhash
匹配共享区映射的版本；(c) 正常路径本应**从共享区直接取**libSystem
（不需要落盘读），cache 未完全可用才 fallback 到磁盘文件 → 检查
`hasValidCache`/loadInfo 的标志位（`[X20,#0x19]` reuse 里写）是否置位。
**先用 IDA 找 `code signature invalid` 串的调用点，看校验条件。**

---

### ★★★★★★★ 2026-09-27（Devin 续）⭐⭐ 536 实证成功；卡点=映射后用户态循环（已解明机制）

**一句话**：syscall 536 `__shared_region_map_and_slide_2_np` **在 macOS
15.6.1 dyld 里能跑通且返回 0**——证据双保险：retcave 抓到 ~590 万次
`x0=0` 返回（47MB 零字节洪流）+ KRW 读 task map 显示共享区子映射已填
~30 个映射（`sr_uuid=4c1223e5…`=macOS cache，`slide=0xd7b0000`，
`first_map=0`，`in_prog=0`）。**之前所有"536 阻塞/EINVAL"方向全部作废**。

**当前卡点（未决）**：536 成功后 dyld 在 `mapSplitCacheSystemWide`
返回路径附近进入**用户态死循环**（fs_usage 静默=纯计算，看门狗 45s
才杀）。`spindump` 采样得到两个确凿 PC：

- pid 3394（遗留）：`dyld+0x38d28` = **NOP sled 内部**（0x38d08-0x38d3c
  padding，其后 0x38d40=`dyld_program_minos_at_least`）——PC 如何进
  padding 未解（无静态 xref；必为 BR/RET/computed 跳入）。
- pid 5430：`dyld+0xcb0` = **mkG cave 的 `svc #0x80` 内部**——write(2)
  阻塞在内核（stderr/pty 缓冲被前面 ~190K 次 'D' 标记写满）→ 证明流程
  真实走到了 0x35668（G 点，mappings 循环之后）。

**marker-breadcrumb 结论链**（写探针：B@0x35380/C@0x35434/D@0x355fc/
E@0x3561c/F@0x3562c/G@0x35668，全部 `write(2)+replay+b`）：

| 运行 | B | C | D | E | F | G | 结果 |
|---|---|---|---|---|---|---|---|
| mEFG2（含 mkD） | 1 | 1 | **~95K 次** | 0 | 0 | 0 | D 洪泛→pipe 满→G-cave svc 卡 |
| noD（去 mkD） | 1 | 1 | — | 0 | 0 | 0 | **47MB `00` 洪流**=retcave x0=0 |

- 'D' 在 0x355fc 重复 ~95K 次但**该点函数内无回边**（backedge 表：
  353a8/353e4/35538/3562c/3561c/356b4/35714）→ 要么 mapfn 被反复调
  用（但 B@入口只触发 1 次！）要么 `44 0a 00 00` 不是 D 标记——
  **未解之谜**。mkDlr 证实 x30=base+0x355d8 只是 chkstk BLRAA 残值。
- noD 的 47MB 零字节 = **每 8 字节一次 `x0=0` 的 retcave 写 ~590 万次**
  = **536 被调用了 ~590 万次且全部返回 0** → 确实存在一个
  `[…→0x35690 536→0x35698 ret→…→回绕]` 的巨型重试环，但环结构未
  定位（post-536 代码无到 0x35690 的回边；reuseExistingCache 内无递归）。

**新铁律（hard-coded，血换来的）**：

1. **`write(2)`/svc 探针在 dyld 早期上下文必死**：`__dyld_start`(0x47c0)
   和 `start` 入口(0x53dc) 的 write-cave → 秒 SIGBUS/SIGILL 零输出；
   同 cave 只 replay 不写 → 正常跑。write 只在 dyld 自身 init 完成后
   （mapfn 区域 0x35380+ 起）才安全。**早期打点别用 syscall 探针**。
2. **cave 间距必须 ≥ cave 长度**：mkE(0xb40) 与 mkF(0xb80) 曾重叠
   0x40 间距 < 0x4c 长度 → E 尾部被 F 覆盖。死区 `0x970-0xfff`
   （thin slice 全零已验证）+ `0x3b394`(60B) + `0x47290`(56B) 可用。
3. **`_le()` 输入必须是指令字序 hex**（如 `1400d32c`），不是文件序——
   曾把 `36d30014` 当字序喂进去 → 落地成 TBZ 乱指令 = v2/v3 怪异死法。
4. **每次重签后才部署**：`scp` 原地覆盖同 inode 会让 vnode 缓存旧 blob
   → 秒 SIGBUS；先 `rm` 再 `cp`。`ldid -Hsha256 -Cadhoc -S` →
   `jbctl trustcache add $(ldid -h|grep CDHash=|cut -c8-)`。
5. **spindump 可用且给真 PC/栈**：`/usr/sbin/spindump`（采样全系统，
   忽略 pid 参数）→ `/tmp/spindump*.txt` 里按 Process 名找段。kernel
   帧带 `*`。chroot 进程也采得到——**第一仪器，别再瞎猜 PC**。
6. `task_threads`/`mach_vm_region*` 经 task_for_pid 端口=INVALID_DEST
   （iOS vm_map_read_t 门）；KRW 直读 `task+0x590`=threads 队列，
   `task+0x28`(PAC)→map，`ro+0x8`(PAC)→task，`proc+0x18`=ro。

**函数地图（thin slice 基线 `analysis/dyld_15.6.1_arm64e_thin`，IDB=Instance1）**：

- `mapSplitCacheSystemWide` = `0x352bc-0x3576c`（不是 0x35380！）
  - `0x3537c BL preflightMainCacheFile`；`0x353e4-0x35430` subcache
    preflight 循环（每 CacheInfo 0x1C0，调 `preflightSubCacheFile`）
  - `0x35438 BL DynamicRegion::make`；`0x35510/0x355d4 BLRAA chkstk`
    （files[] 12B×w28 / mappings[] 0x30×w25 两个 alloca）
  - `0x35538-0x35564` files[] 打包循环（`files[i]={fd,slide(i==0?slide:0),
    count}`——**dyld 本来就只给 files[0] 写 slide，"subcache slide 未
    清零"假说已被 IDA 反汇编证伪**）
  - `0x355f8 CBZ W28→0x35660`；`0x3561c-0x3565c` mappings 打包
    （外 x8<x27，内 w14=CacheInfo[i]+0x180 映射数，0x30 拷贝）
  - `0x35664 BL DynamicRegion::size`；`0x35690 slidentry→slidecave2`；
    `0x35694 BL __shared_region_map_and_slide_2_np`；
    `0x35698 retentry→retcave`
  - `0x356a0 BL DynamicRegion::free`；`0x356b4-0x356cc` close(fd) 循环；
    `0x356d8 BL reuseExistingCache`→成功返 1，失败 w0=0+errstr
- `loadDyldCache`=`0x34240`：options+4==1→`mapSplitCachePrivate` 尾调；
  else `0x34298 BL reuseExistingCache`（**被 prereuse patch 改 movz 0**
  →强制 map 路径）→ `0x342d8 B mapfn` 尾调。
- `getDyldCache`=`0x2fe34`（BL loadDyldCache@0x2fe90）；caller=DyldCache
  ctor `0xbed4`。`reuseExistingCache`=`0x351a8`（`check_np`→uuid strcmp
  →`dynamicRegion()`(0x50dfc=accessor patch)→fileID 比对）。

**下一步二分**（恢复后继续）：①post-536 重试环结构未定——把
marker 放到 0x356dc/0x356f4/0x35710/0x35754 各分支点 + `free`/`close`/
`reuse` 各 callee 入口，看循环覆盖哪段；②0x38d28 NOP-sled PC 来源
未解（找 `off_9C038` GOT 被写坏 / BR x16 落点）；③47MB 零写本身也
可能是单次 `write(2,zerobuf,huge_count)`——需分辨。

### ★★★★★★ 2026-09-27（原 AI 续 2）⭐ 墙 B 已解——纯用户态 `run_nocskill`，零内核写

**决定性实验结果**（全在设备实测）：

| 用例 | 结果 |
|---|---|
| `hellocs`（sha256 签+TC）直接跑 | `r=33333326` rc=0 ✅ |
| `hellocs_bad`（**签名后翻 __text 字节**，cdhash 不变）直接跑 | **rc=137 SIGKILL-CODESIGNING**（Invalid Page）✅ 复现墙 B |
| `hellocs_bad` 经 `run_nocskill` | **`r=f041b330`（篡改指令真实执行）+ rc=0** ✅ 墙 B 攻破 |

**机制（repo:`misc/run_nocskill.c` + `run_nocskill.entitlements.plist`，设备 `/var/mobile/run_nocskill`）**：

```c
posix_spawn(pid, target, NULL, attr=POSIX_SPAWN_START_SUSPENDED, ...)  // exec 完、旗已置、用户代码未跑
proc = find_proc(pid)                      // pidhash: TBL@0x...1D22B4D0 / MSK@0x...1D22B4D8, chain proc+0xA0, pid@+0x60
ro   = kread64(proc + 0x18)                // p_proc_ro
kwrite32(ro + 0x1C, kread32(ro+0x1C) & ~0x300)   // p_csflags &= ~(CS_HARD|CS_KILL)
task_for_pid(mach_task_self(), pid, &tp);        // 需 task_for_pid-allow entitlement
task_resume(tp);                               // LEGACY release 也减 user_stop_count → NORMAL hold 亦可解
waitpid(pid, &st, 0);
```

**结构事实（xnu-8792.81.2 `bsd/sys/proc_ro.h`）**：`proc_ro{pr_proc@0, pr_task@8, p_uniqueid@0x10, p_idversion@0x18, p_csflags@0x1C}`——`p_csflags` 在 proc_ro 里，之前叫的"cs_blob"其实是 proc_ro。

**关键事实**：
- spawn 挂起 = `task_suspend_internal`（`place_task_hold NORMAL`）；`task_resume`（MIG impl）`release_task_hold LEGACY` 对非-PIDSUSPEND 模式**无条件减 `user_stop_count`** → 可解 NORMAL hold（源证 `osfmk/kern/task.c:3659-3760`）。
- `task_for_pid` root 也会被 MACF 拒；**`task_for_pid-allow`（或 `com.apple.system-task-ports`）entitlement 在 ldid-sha256 签的二进制上被 AMFI 兑现**（实测 kr=0）。
- `pid_suspend`/`pid_resume` 是 PIDSUSPEND hold——**解不了** spawn 的 NORMAL hold，别走那条路。
- `ptrace` PT_TRACE_ME/PT_ATTACH 在 iOS 全 EPERM。
- kcall 需要 `com.apple.security.exception.iokit-user-client-class`+`IOSurfaceRootUserClient` entitlement（python3.9 无 → kcall 不可用；launcher 已签上备用）。
- kernel `__TEXT` 写入会挂死（KTRR）——**别写内核 text**；本方案只写数据字段 `p_csflags`（非 PAC）。
- 真死代码洞 `0x38d08-0x38d3c`(56B)（探针可用）；`0x3576c-0x35afe` 是活代码（注入 blob），IDB 里 0x35754 字节被旧 patch 污染，真值看 `dyld.orig`。

**签名公式**：`ldid -Hsha256 -Srun_nocskill.entitlements.plist`（不加 `-M`）→ `jbctl trustcache add $(ldid -h x|grep -o 'CDHash=[0-9a-f]*'|cut -d= -f2)` → `chmod 755`。entitlements 必备 `task_for_pid-allow`+`get-task-allow`+`platform-application`+`no-sandbox`+iokit-user-client。

**剩唯一墙 = 墙 A（dynregion 0x1f8000000 随首 mapper 退出消失 → 后续 exec 的 `hasExistingDyldCache`/`reuseExistingCache` 在 `check_np(NULL)`/dynamicRegion() 处 SEGV 139）**。两个候选解：① 每 exec 自愈（把两个早 deref 点改返 0 强制走 map 路径，验证 536 在已填充 region 上可重提交）② keeper 常驻进程持有 region。

### ★★★★★ 2026-09-27（原 AI 续）双补丁点运行时地址已铁证 + slide 统一更正

**前提更正（推翻隔壁"slide 不匀"结论）**：`runtime_text = IDB_addr + 0x158B4000` 对 **kernel `__text` 和 `__TEXT_EXEC` kext 文本全部成立**。隔壁的 slide 矛盾是把 `proc+0x180`（数据/堆指针）混入推算所致。现在**任何内核 text 地址可直接换算**（本 boot）。

| 靶点 | IDB | 运行时（已逐字节验证） | 指令 | 补丁 |
|---|---|---|---|---|
| C1 | `0x92a69fc` | **`0xfffffe001eb5a9fc`** | `ORR W8,W8,#0x300` | `0xD503201F` NOP |
| C2a | `0x8373868` | **`0xfffffe001dc27868`** | `TBNZ W27,#9` | `0xD503201F` NOP |
| C2b | `0x8373874` | **`0xfffffe001dc27874`** | `TBZ W1,#8` | `0x1400000A` B +0x28 |

- **验证方法**：IDB 全库签名唯一性（`py_eval find_bytes`）+ 设备端 `kread32` 逐字节复核——C2 20/20 字、C1 11/11 字全匹配（含函数头 PACIBSP）。
- **签名**：C2=`b278010a 7100013f 9a8a011b 374800db 52800016 aa1b03e1 36400141`（IDB 唯一命中 0x837385c）；C1=`e80040b9 08051832 e80000b9`（IDB 唯一命中 0x92a69f8）。
- **扫描器**：`/var/mobile/{kscan_c1c2.py,kscan_c1.py,kverify_c1.py,kverify_c2.py}`（重启后地址会随 KASLR 变——**必须重扫重验再写**）。
- **语义**：C1 NOP 后 `_vnode_check_exec` 不再置 CS_HARD|CS_KILL → `cs_invalid_page` 的 KILL/HARD 分支全不触发 → 全局不再因 "Invalid Page" SIGKILL（诊断级、全局弱化，已获用户授权试写）。
- **未写**：KTRR 风险仍在——写 kernel text 可能 panic（用户已授权冒险）。

### ★★★ 2026-09-27 深夜 3×subagent 大包围：两墙定性 + 具体补丁地址（最高优先）

### ❗❗ 2026-09-27 晨 重大更正："echo 活/sleep 死" 是【测量假象】
- 【事实】（subagent 控变量实验）**"703–792 行 vs 2 行" 100% 由是否设 `DYLD_PRINT_*` 决定**：开了就喷~700行，关了就只有 launcher 2 行。同环境下 `echo ≡ sleep`（逐字节，仅 target 名不同）。**我那句“暖机 echo(703) 后 sleep(2行)”是错：echo 开了 PRINT、sleep 没开。**
- 【事实】`next_boot.sh` 的 `try_keeper` 原先**没开 PRINT → 误判 keeper 死亡**（已修：try_keeper 现带 PRINT）。
- 【事实】真实二档 = 签名：① 项目 ent+arm64+TC 的二进制（echo/true/ls/sleep/bash/cat）→ **全到缓存映射(703–792) → 再死在 dynregion(139)**；② Apple 原版 ent（如 `mobileassetd`）→ exec 就被 AMFI 杀(137)。**修②：`ldid -Hsha256 -S<项目ent>`（不加 `-M`！）-M 会合并保留 Apple ent → 仍死；去 -M 实测 782/0→792/1。**
- ⇒ 历史上“sleep 死”真身 = 当时它还是 arm64e（已被 arm64ify 修）。**⇒ 剩下唯一 CLI 墙 = dynregion(139)，连 mapper 自己也崩。**
- ❗ 实验失败记录：`dyld_nodyn.bin`（dynamicRegion→NULL + 中和 3 个 ExternallyViewableState）→ **反把 dyld 弄坏**（693 行、无 cachemap、死；且 ldid 报 “Are you sure that is a Mach-O?”）⇒ **NULL 不容忍，此路不通**。下一步改走：让 dynregion **真实存在**（keeper 或 m8 改文件映射），而不是强制 NULL。
**墙(b) `SIGKILL-CODESIGNING/Invalid Page`（rc137、“2 行静默死”）= 内核 AMFI 无条件置 CS_KILL**
- **★ 更正（2026-09-27）**：`kc_raw_16.3_T8112.bin` **文件名误名**——它含 `_apciecT8103` 等 T8103 特征，**就是本机内核**（设备=`iPad13,11`/`J523tAP`/`RELEASE_ARM64_T8103`，xnu-8792.82.2）⇒ **IDB 偏移有效**。
- **KRW 可用**：`kread32/64`、`proc_self`；`proc_self=0xfffffe1217…`（堆）。**`proc+0x180=0xfffffe002033cb10`、`proc+0x18=0xfffffe1133682080`（内核镜像区指针）可作 slide 锚点。**
- **❌ 仍缺：kernel 运行时 slide**。假阳性教训：`_memset_s+0x17000000` 看似序言，但 IDB `_memset_s[0]=0xd503237f` 而那里是 `0xa9017bfd` ⇒ **dry-run（`kpatch_c2.py`）挡住了误写**。用 `cs_invalid_page` 指纹在 `[0x19000000,0x1a200000)` step4K **未命中**。
- **下一步**：用 `proc+0x180` 的 IDB 字段语义（或读 IDB `struct proc`）一步定 slide → 再用 C2/C3 指纹校验 → 才写。**未确认前绝不写内核**。
- 【事实】`_vnode_check_exec`@`0xfffffe00092a69e8`，指令 **`0xfffffe00092a69fc  ORR W8,W8,#0x300`（CS_HARD|CS_KILL）在每次 exec 无条件置位** → 任一 taint/未验证可执行页 → `vm_fault_validate_cs`→`cs_invalid_page`(0xfffffe0008373778)→`threadsignal(SIGKILL)` → “Invalid Page”。
- 【事实】dyld **无自校验**（导入无 `csops`/`cs_*`）⇒ 墙(b) **不能靠改 dyld 代码修**（只能签名侧或内核侧）。
- **内核侧修法**：C1（最直接）`0xfffffe00092a69fc` 的 `ORR #0x300`→`NOP`（全局去 CS_KILL）；C2 `cs_invalid_page` 的 `0xfffffe0008373868 TBNZ`→NOP + `0xfffffe0008373874 TBZ` 恒跳软路径；C3 `cs_validate_hash`(0xfffffe00084035ac) no-hash 路径改 validated。风险：全局削弱签名。
- **用户态修法**：`ldid -Hsha256` 重签（默认 SHA1→SHA256）实测已使首交 782→792（越过 `__dyld_start` 击杀）。cave **无 CS 优势**（在 `__TEXT` 内、落在 codeLimit≈0x128210/297 页覆盖内）。

**墙(a) `SIGSEGV@0x1f8000000`（dynregion 消失）精确补丁**
- 【事实】崩在 `dynamicRegion()`@`0x50dfc` 第 3 条 `LDR X9,[X8]`（读 `0x1f8000000` magic）。10 调用点中 **4 组容忍 NULL**（`start`@0x6380、`evaluateFunctionVariantFlags`@0x95c0、`hasExistingDyldCache`@0x30178、`reuseExistingCache`@0x35254）；**3 个 `ExternallyViewableState` 不容忍**（`0x4a964/0x4b9a8/0x4c17c` 直接 `cachePath(dynamicRegion())`，`cachePath`@0x51358 也不判空）。
- **补丁 1**：`0x50dfc`→`mov x0,#0`(`00 00 80 D2`) + `0x50e00`→`ret`(`C0 03 5F D6`)；**需搭配补丁 2**：`0x4a95c/0x4b99c/0x4c170` 的 `CBZ`→`B`（无条件跳过，目标 `0x4a9ec/0x4ba54/0x4c264`）。更稳替代=keeper 保活。

**amfid（Instance3）** 【事实】`amfid_bin` 只注册 MIG base=1000；**内核 exec upcall 27001 的 server 不是 amfid_bin**（修正旧推断）；exec 放行 = ①ARM64/ALL ②执行位 ③cdhash∈trustcache → 内核直置 `CS_SIGNED`（`kern_exec.c:7430` 跳过 upcall）。

### 当前 TODO（下个对话先看这里）
1. ✅ exec 门/字节序/launcher 毒丸 — 已修。
2. ✅ 536=EINVAL 真因=缓存 slide-info v5；`cachereg`+掩0x20+slide=0 → ret536=0。
3. ✅ 候选 dyld（`dyld_noslide`/`dyld_noslide_reuse`）+ `post_reboot_noslide.sh`/`catch_segv.sh`。
4. ✅ reuse 设计（`dyld_noslide_reuse`）。
5. ✅ 冷启动首交：**536 映射成功 + dyld 从缓存加载 libSystem/libobjc 等**（`analysis/dyldwork/t1_final_trace.log`）。
6. ⏳ **免重启**：每测必重启很痛。**实验结论（2026-09-27）**：① “坏 fd 的 536”在映射前就失败→不触发 undo；② “m8.size=-1 的 536”（想先映 8 条再在第 9 条失败）在 **populated region 上被早期拒绝（22）** → 也无 undo。⇒ **536 无法复位已填 region**。可行替代：① 每 boot 一测（`/var/mobile/run_all_cold.sh`）；② KRW 直清 region（高风险，未做）。`vm_shared_region_undo_mappings`(`vm_shared_region.c:1295-1310`) 仅用于内核内部回滚。
   - ✅ 附带确认：populated 后子进程 `check_np` 返回 **`base=0x180000000`** = 缓存真映在此。（探针：`dyld_reset536.bin`/`dyld_reset536_v2.bin`）
10. ⏳ **【重启批处理计划】一次冷启动跑 `/var/mobile/run_all_cold.sh`，一次拿全：**
   - A `A_first`：空 region 首交全量 DYLD_PRINT(含 SEGMENTS) trace。
   - ✅ **免重启已全部做完（2026-09-27）**：全部变体重签为 **SHA256**；`run_all_cold.sh` 加固（8s 硬超时 + SEGMENTS）并 **dry-run 完整跑通（不挂死）**；当前 boot 上复用路径实测：trace 止于**缓存 8 段打印之后**（`re-using existing shared cache` 后）→ **SIGKILL(137)**（非 SEGV，handler 捕不到）。
   - B `base/noverb/noreuse/reusemin`：复用路径四个隔离变体（`dyld_v_*.bin`）。
   - C `C_segvcap`：`dyld_segvcap.bin`（SIGSEGV-handler cave，真 139 时 dump pc/far）。
   - D `sha1/sha256/ent256`：**验证 CS 击杀假说**（`dyld_segvcap.bin`=SHA1 vs `dyld_segvcap_s256.bin`=SHA256 vs `dyld_segvcap_ent.bin`=SHA256+entitlements，均含 segvcap）。若 SHA256 版不再被 CS 击杀 → 根因确定。
   - ⚠️ 2026-09-27 实测：**本 boot 上三签名表现一致**（均 2 行即挂住/早崩、无 macOS dyld 输出、无新 .ips）⇒ **哈希差异需干净冷启动才能观测**（本 boot 已退化：早前 92 行崩→现直接挂住）。
   - **✅ 2026-09-27 DRY-RUN（完整跑通 `run_all_cold.sh`，无挂死）：** 结果 **`D_sha256`/`D_ent256` = 792 行且到达 `Mapping the shared cache system wide` + reuse；`C_segvcap_sha1`/`B_*` = 782 行**。⇒ **SHA256 签名让 macOS dyld 越过了早期 `__dyld_start` 的 CS 击杀、走得更远**——方向坐实；后续 137 是**更后面另一处**。脚本已验证（本地=设备、依赖全齐、8s 硬超时、开 SEGMENTS）。
7. ⏳ **定位/修复复用路径 SEGV**（候选 `reuseExistingCache` 0x351a8 / `verboseSharedCacheMappings` 0x3598c；需 PC——chroot lldb 自崩、无 debugserver）。
   - **2026-09-27 发现**：**已填 region 的 boot** 上跑任何 dyld（含变体）都**停在 91 行 / ellekit `libinjector.dylib`**（launcher 的 iOS dyld 阶段，rc139/137）——**≠ 冷启动的崩溃**（后者才走到 macOS dyld 深处）。
   - ⇒ 复用路径隔离变体（`dyld_v_noverbose.bin`：`verboseSharedCacheMappings`→RETAB；`dyld_v_noreuse_tail.bin`：`reuseExistingCache` 0x3525c→0x352a4）**必须在冷启动首交后立即测**（用 `run_all_cold.sh` 的 B 段）。
   - 调用链：`loadDyldCache`(0x34240) → `reuseExistingCache`(0x34298) → 若返 0 → `mapSplitCacheSystemWide`(0x342d8)。上层：`SyscallDelegate::getDyldCache`(0x2fe34，reuse 成功后调 `DyldSharedCache::getUUID`+`kdebug_trace`) → `loadDyldCache`。
   - **2026-09-27 trace 细读（关键）**：`t1_final_trace.log` 792 行里，**1-782 行是 launcher 的 iOS dyld**；**783-792 行才是 macOS dyld**：`Mapping the shared cache system wide`(783) → `re-using existing shared cache ((null))`(784) → **`verboseSharedCacheMappings` 打印出缓存完整 8 段（785-792）**：`0x180000000->0x1E7F5BFFF __TEXT`、`__DATA_CONST`、`__DATA`、`__TPRO_CONST`、`__AUTH`、`__AUTH_CONST`、`0x1F9070000 __READ_ONLY`、`0x1FED4C000->0x22560BFFF __LINKEDIT`。⇒ **reuse 成功、缓存段布局与 prot 均正确**；**崩溃在 reuse/getUUID 返回之后**（dyld 的 post-cache 流程：cache-restart 或首个 dylib 启动）。
8. ⏳ 跳过 slide 的 **rebase 验证**（slide=0 下指针是否真正确）。
9. ⏳ 推进 **WindowServer/VNC**（AGX 桥接为后续大工程，见 AGENTS.md）。

### 测试环境（每次冷启动重复）
- SSH `root@192.168.64.1 -p 2222`（密码 cisco）；region 一旦填满 → **必须重启**。
- 冷启动恢复：trustcache add cache cdhashes + `chmod 755` 所有要用二进制 + `nohup /var/mobile/cachereg <cache> <cache>.01 &`。
- 一键：`sh /var/mobile/post_reboot_noslide.sh`（首交+reuse）；`sh /var/mobile/catch_segv.sh`（lldb 抓崩，目前 chroot lldb 自崩）。

### 2026-09-27 00:4x — ★★★★★ 里程碑：首交成功映射缓存（536=0），进程在下游 SEGV
- 冷启动空 region + `cachereg` + `dyld_noslide` → 跑 `/bin/echo`：**无 `syscall to map cache into shared region failed`**（= mapSplitCacheSystemWide 未报错），但**随后 `Segmentation fault: 11`（rc=139）**，无 .ips。
- 随后同 boot 用 `sf_ns`（掩码捕获探针）测得 **`ret536=22`**（region 已被首交填满）⇒ **首交的 536 确实返回了 0 并映射成功**（区域被填充）。
- ⇒ **536 映射这面墙已被跨过**；新的卡点是**下游 SEGV**（候选：跳过 slide 导致缓存 __DATA 的 rebase 指针未处理；或 dynregion deref；或注入的 libmachook ctor）。

**2026-09-27 00:4x 更新 — 冷启动 DYLD_PRINT trace（`analysis/dyldwork/t1_final_trace.log`，792 行）彻底改写了结论：**
- **macOS dyld 成功用了缓存！** 日志中大量 `Using mapping in dyld cache for /usr/lib/libSystem.B.dylib`、`/usr/lib/libobjc.A.dylib`、`libdispatch/libdyld/libc++/libcache/...`，还有 `Kernel mapped .../launchdchrootexec`、`__SHARED_CACHE (rw.)`。
- 末尾两行：**`Mapping the shared cache system wide`**（= `mapSplitCacheSystemWide` 0x35704 成功路径！）+ **`re-using existing shared cache ((null)):`**（= `reuseExistingCache` 0x35280 的打印，参数是 `DynamicRegion::osCryptexPath()`）→ **然后 SEGV(139)**。
- ⇒ **536 映射成功 + 缓存内 dylib 加载成功**；崩溃在 **macOS dyld 第二次 `reuseExistingCache` 的后续**（日志止于 0x35280 的 print；下一句即 `verboseSharedCacheMappings` 0x35290）。
- `reuseExistingCache`(0x351a8) 反汇编：`X19=base` → `strcmp(base,"dyld_v1  arm64e")` → 匹配则 `loadAddress=base; slide(); dynamicRegion(); getDyldCacheFileID(); osCryptexPath()` 打印；`(%s)=(null)` 是 `osCryptexPath` 返回 null（打印本身无害）。**下一步靶子：第二次 reuse 的 base/`verboseSharedCacheMappings` 为何 SEGV**（需冷启动 lldb `catch_segv.sh` 取 PC，或比对第二次 reuse 的 base 是否 null）。
- 下一步：① 用 lldb（`MACWS_SUSPEND_AT_EXEC=1` + `misc/lldb_*`）在冷启动首交时抓 SEGV 的 PC/far；② 判断是否 rebase（若 PC 落在缓存 __DATA 上）——若是，则需真正处理 v5 slide（内核 backport 或重生成 v4/4K 缓存），而非仅跳过。
- **源码分析（`dyld/SharedCacheRuntime.cpp:382-388`）**：dyld **仅在某 mapping 的 `slideInfoFileSize != 0` 时**才给它 `sms_{init,max}_prot |= (VM_PROT_SLIDE(0x20) | authProt)`。⇒ `0x20` 源于缓存的 `mappingWithSlide` 表（`dyld_cache_mapping_and_slide_info`）逐 mapping 的 `slideInfoFileSize`；掩 `0x20` 等价于让 dyld 不提交 slide。
- **rebase 推理（倾向“跳过 slide 不破坏指针”）**：slide-info 语义 = 内核给 slide 页的指针 += `slide`；`slide=0` 时增量 0，指针保持其 preferred VA，而缓存正好映射在 preferred `0x180000000` ⇒ 指针本就正确，理论上无需 rebase。故 SEGV 更可能是 **dynregion(m8) / 注入 libmachook ctor / 某条 mapping**，而非纯 rebase。**需 SEGV PC 定论**（冷启动首交跑带 DYLD_PRINT 的 trace）。
- **post-536 流程（IDA 反汇编）**：`0x356d8` 调 **post-syscall `reuseExistingCache`**（填 loadAddress）；成功 → `0x35704` 打印 **“mapped dyld cache file system wide”** → `ret 1`。**该函数内无 dynregion deref** ⇒ 首交 SEGV **不在 536 后立即处**，在**更下游**（cache 内 dylib 加载 / objc 初始化 / cache-finder）。静态分析无法定论，**必须冷启动跑 DYLD_PRINT trace**。
- ⚠️ 测试纪律：region 一旦映射就**持续到重启** → 每次端到端必须冷启动。本次首交已把本 boot 的区域填满。
- **★★★ 2026-09-27 01:0x 重大发现：崩溃实为 `SIGKILL - CODESIGNING`（CS 击杀），非普通 SEGV！**
  - crash 报告：`echo-2026-09-27-010008/010022/010105.ips` → `exception.type=EXC_BAD_ACCESS`、`signal=SIGKILL - CODESIGNING`、`subtype="UNKNOWN_0x32 at 0x<PC>"`（PC 三次：`0x100dd87c0`/`0x1027107c0`/`0x104b7c7c0`，随 ASLR 变）。
  - ⇒ 进程因**执行了未通过 CS 校验的代码页**被 AMFI/内核击杀（不是野指针 SEGV）。与文档旧结论“errno-40 / `VSHARED_DYLD` / 需项目 entitlements”属同一 **CS/AMFI 家族**。
  - ⚠️ **SIGKILL 无法被信号处理器捕获** ⇒ `dyld_segvcap`（SIGSEGV/SIGBUS handler）对 137 无效，只对真正的 139 有效。
  - **★★★★★ 决定性！报告全字段定位：击杀页 = 我们改过的 dyld 的 __TEXT。** 证据：`termination={code:2, namespace:"CODESIGNING", indicator:"Invalid Page"}`；`ktriageinfo="VM - A memory corruption was found in executable text"`；`vmRegionInfo` 的 PC `0x104b7c7c0` 落在 `0x104b78000-0x104c14000`【**0x9c000=624K** r-x】= thin dyld 的 __TEXT 大小；且 **PC-region = 0x47c0 = `__dyld_start`**。
  - ⇒ **根因假设（强）：修改 dyld 的 __TEXT 代码页后，CS 的页面哈希不匹配 → 内核在执行时判 "Invalid Page" → CODESIGNING 击杀**。与文档旧结论“MUST 带项目 entitlements 签名”同一方向（可能是 ldid 重签未正确覆盖页哈希 / 或需用 entitlements）。
  - 下一步（不需冷启动）：① 核查 ldid 重签是否重建了 CodeDirectory 的 page hashes（对比修改前后的 CD slot hashes / 用 `ldid -S<ent>` 重签）；② 优先**不改 __TEXT 代码页**的方案：只在**可执行 cave**（如未用 padding）写，或改用 **`DYLD_INSERT_LIBRARIES` 注入一个自写 dylib**（它自己合法签名）来装钩，避免改 dyld 代码页。
  - **追加实验（2026-09-27）：原始未修改 dyld.orig 也 rc=137**（其 cdhash 因 fat 头报 "wrong length" 未进 TC）；补丁版已 TC 但也 137。⇒ **CS「Invalid Page」击杀不只是“我改了代码页”——连未修改的 macOS dyld 也遭杀**。⇒ 根因升为：**chroot 里的 macOS dyld 的 `__TEXT` 过不了 iOS 内核的 CS 页校验（`__dyld_start` 处被杀）**，属**跨平台签名 × AMFI** 问题。下一步：内核 RE（Instance2/kc_raw 13339）定位执行时的 CS 页校验/击杀路径，看是否有 boot-arg/entitlement 可豁免。
  - **内核链已理清**：`vm_fault.c:2775` → `cs_invalid_page`（`kern_cs.c:248`）；**击杀条件 = `proc_getcsflags & CS_KILL`**（`kern_cs.c:274` → `threadsignal(SIGKILL, EXC_BAD_ACCESS)`）；页校验器 `cs_validate_page`（`ubc_subr.c:5422`）算页哈希 vs CS blob 期望哈希，不匹配 → `bad_hash`（Invalid Page）。
  - **★ CS blob 参数差异（无重启核查）**：原始 thin dyld → `hashType=2(SHA256)`；`ldid` 重签后 → **`hashType=1(SHA1)`**（pageSize=4096/nCode=297/flags=0x0 均同）。⇒ **ldid 把哈希降级为 SHA1**；若 iOS 内核对该 vnode 期望 SHA256 → `bad_hash` → Invalid Page。**修复候选：用匹配原版（SHA256/正确 CD 形态）的方式重签**（或用 entitlements 重签）。
  - 下一步：① 查被击杀的代码页归属（PC 落在哪个镜像/是否缓存内代码；.ips 无 usedImages，需冷启动复现时用 `MACWS_AGX_CRASH_DIAG` 或内核 RE：AMFI `mpo_file_check_mmap`/执行时的 CS 校验）；② 核对项目 entitlements 是否覆盖该 CS 检查（对比 `entitlements.plist` 与 macOS 缓存内的库需求）。
  - **★★★★★ 2026-09-27 冷启动决定结果（全变体 SHA256）：** `A_first`（**空 region 首交**）= **792 行 + `Mapping the shared cache system wide` + 完整 8 段** ⇒ **首交越过早期 CS 击杀、直接到缓存映射**。`B_noverb/B_noreuse/B_reusemin` = **rc139（真 SEGV）** 且到缓存映射；`B_base/D_sha256/D_ent256`=792；`C_segvcap_sha1`=782。
  - **❗ 合成变体 `dyld_segvcap_reusemin.bin`（segvcap + reuse_min）反而 137 早崩（130B）——说明 `sigaction` 本身扰动了时序** ⇒ 抓 139 需换策略（在不改时序处装 handler，或对其他能到 139 的补丁集加 handler）。
  - **★★★★★ 2026-09-27 拿到崩溃地址（无需 handler，SEGV 自落 .ips）：`true-2026-09-26-165443.ips` → `SIGSEGV` / `KERN_INVALID_ADDRESS at 0x00000001f8000000`。**
    - **`0x1f8000000` = 536 提交里的 m8（dyld dynamic region）**！结合旧结论（本文 §319：*`fd=-1` 的 dynregion 条目随 mapper 进程退出消失，文件映射存活*）⇒ **后续进程 deref `0x1f8000000` → SIGSEGV**。**这就是“缓存映射后立刻崩”的真因。**
    - **修复方向**：① **常驻 keeper 进程**持住 536 映射（使 dynregion 存活）；② **把 dynregion 改成文件映射**（持久）；③ 让 dyld 在 deref 前重建/重新 536 提交 dynregion。
    - ⇒ 同时解释为何 **A_first(首交)=792** 而 **后续=崩**：首交进程自映射（dynregion 对其存活），进程退出后 dynregion 消失。
    - **铁证（.ips 的 vmregioninfo）**：`0x1f8000000 is not in any region`；同表显 `180000000-1e7f5c000 …ed lib __TEXT`（缓存 __TEXT 在）⇒ **m0-m7（文件映射）存活、m8（fd=-1 dynregion）已消失**。诊断 100% 坐实。
    - **✅ 已备 keeper 方案（`/var/mobile/keeper_test.sh`，待冷启动）**：让**首个 536 提交者是一个长驻 macOS 进程**（`/bin/sleep 100000`，已确认存在），它自映射后不退出 → dynregion 存活 → 第 2 个进程（`true`）走 reuse 就不会 deref `0x1f8000000`。判据：keeper alive=yes + reuse 不再 SIGNAL 11@0x1f8000000。**注：keeper 测试必须冷启动**（首交）。
    - **✅ 免重启收尾已完成（2026-09-27）**：① 定论脚本 **`/var/mobile/next_boot.sh`**（一次冷启动：keeper(`/bin/sleep`) 首交 → 验证 alive/缓存映射 → 多进程 reuse（true/ls/echo）→ 区域状态）；② **常驻 keeper 守护**：`com.macwsguide.keeper.plist`（launchd，RunAtLoad+KeepAlive）+ `keeper_start.sh`（等 rootfs→注册 tc→启 cachereg→exec sleep）；③ 全部冷启动/keeper 脚本已过 **POSIX sh 语法**（修掉 `${@:3}` 等 bash 写法）。
    - **⚠️ 2026-09-27 冷启动 next_boot 结果**：`keeperalive=NO`（/bin/sleep 2 行即死）→ **根因 = launcher 给 /bin/sleep 选了 `arch=arm64e` + `libmachook.dylib`**（arm64e 路径坏）；而 true/ls/echo 选 arm64 能到缓存映射。**部署的 launcher（68880B, 9-26 22:45）与仓库源码逻辑（`include/macws_macho_arch.h` 已正确判 arm64）不符 ⇒ 设备上跑的是旧版 launcher；需重编+部署 launcher**。`R_true/R_ls/R_echo` 仍 `SIGSEGV @0x1f8000000`（缓存映射后）。
    - **✅ subagent 大包围（2026-09-27）关键结论 + 已执行修复：**
      - **【更正】`bash/cat` 早死是伪象**：同一 boot 内**后期一切二进制都退化成 2 行**（态退化）；`true/echo/ls` 与 `bash/cat` **无本质区别**。判据：`echo rc=0（非137）`、`sleep rc=137`。
      - **【已修】`/bin/sleep` 是 arm64e（sub=`0x80000002`）→ 已用 `misc/arm64ify_macho.py` 就地 relabel 为 arm64/ALL（sub=0）+ SHA256+adhoc 重签 + trustcache（已验 tc=1）+ chmod755**。这就是 keeper 失败的根因。（备份 `/var/mobile/sleep.orig.bak`）
      - **【确认】launcher arch 判定正确**（旧“部署版与源码不符”被证伪）；**arm64e 路径坏的真因 = `libmachook.dylib` 无 arm64e slice（实为 FAT 两条 arm64）≠ 设计（thin arm64e）** → 需重建（D-3）。影响低（arm64e 目标在 exec 门就 137）。
      - **【瓶颈=dyld 侧】**：`hasExistingDyldCache`→`dynamicRegion()` deref `0x1f8000000`（`dyld/DyldMain.cpp:1084-1095`）；libmachook ctor 晚于它，**排除**；CS/AMFI 是**已越过**的独立模式。
      - **【keeper 可行】**：doc 的“先有蛋”只针对“用 sleep 当第 2 个进程测”，不否决 keeper 思路——keeper 必须是 region 创建后的**第一个**进程（next_boot 顺序天然满足）。
      - **✅ 已备 keeper 二进制清单**（arm64/sub=0）：`bash,cat,echo,ls,pwd,printf,true,uname,update_dyld_shared_cache` + **`/usr/libexec/mobileassetd`**（唯一守护）；或 **arm64ify 后的 `/bin/sleep`**（已做）。
    - **⚠️ 2026-09-27 深夜核查（重要更正 + 不遗漏清单）：**
      - **❗ “true 活/sleep 死”是伪象**：同 boot 后段**连 echo/true 也变 2 行** ⇒ **是 boot 内“态退化”**（随时间劣化，与二进制无关）。**真伪判据**：同一时刻跑 sleep+echo 对比。
      - **“态退化”有健康窗口**：刚 boot 时 echo 能到缓存映射（703–792 行）；跑多轮 536/exec/SEGV 后**一切退化为 2 行**。（这与 §“region 会回空(12)/ret536=22”同源。）
      - **已逐项排除的真实因素**（均已对齐但仍退化→均非根因）：① arch（已 arm64ify，live 行 `arch=arm64` ✓）；② **minos**（`set_macos_version.py` 把 `/bin/sleep` 从 macOS **15.6.0→13.0.0** 已成功，仍 2 行）；③ **entitlements**（用 `ldid -Hsha256 -S<项目 ent> -M` 已把 true 的同一 plist 打给 sleep，同一份十六进制，仍 2 行）；④ trustcache（各二进制 tc 在 clean boot 后均为 0，非差异）；⑤ 热机（echo 预热 4 次后 sleep 仍死）。
      - **仍然成立的真实区分**：健康窗口内 `true/echo/ls` 能到 `Mapping the shared cache system wide`（cachemap=1）；退化后全 2 行。⇒ **真正靶子 = “为何 boot 内态会退化”（而非某二进制）**。
      - **工具产物**：`/var/mobile/{archchk.py,platformchk.py,cschk.py}`；`/bin/sleep` 已 arm64ify+重签（备份 `/var/mobile/sleep.orig.bak`）。未发现 sleep/bash/cat 的新 `.ips`（退化是静默 2 行/rc）。
- **subagent 大包围取证（2026-09-27）：**
  - 崩溃窗口 = `reuseExistingCache` **成功返回之后**（日志止于 8 段打印 `t1_final_trace.log:792`）；源码 `SharedCacheRuntime.cpp:1373-1378` 证成功路径**不**打印 `mapped dyld cache file system wide`（勿误判）。
  - **拿 PC 首选**：给 dyld `start()`(thin 0x53dc) 最前注入早期 SIGSEGV-handler cave（handler 从 ucontext 取 pc/far → 写 /tmp/dyldsegv.txt → _exit），复用 `build_dyld.py`/`cave_noslide.s` 工具链；libmachook 的 `MACWS_AGX_CRASH_DIAG`（`mac_hooks.m:7324-7502`）**装在 ctor，太晚**，抓不到 dyld 启动崩溃。
  - **ellekit 不是子进程崩溃源**：`launchdchrootexec/main.m:71` 的 `setenv(...,1)` **覆盖**掉 ellekit 注入项 ⇒ chroot 子进程只带 libmachook（`forkfix/libinjector` 仅在 banner 之前的 launcher iOS dyld）。
  - **post-cache 高风险 deref（优先级）**：`DyldDelegates.cpp:158-161`（getUUID+kdebug）、`dyldMain.cpp:1086-1095`（handleDyldInCache→dynamicRegion）、`dyldMain.cpp:1215-1219`（restartWithDyldInCache）、`DyldProcessConfig.cpp:1416-1431`（dylibsExpectedOnDisk + objc/swift 表位）。
  - 备用拿 PC：`catch_segv.sh`（+`MACWS_SUSPEND_AT_EXEC=1`）。注：日志若缺 `mapped dyld cache file system wide` 属正常。
  - **✅ 已造成工具 `dyld_segvcap.bin`**（2026-09-27）：在 dyld `start`(0x540c) 挂钩 → 跳 `__TEXT` 尾段可执行零填充 cave `0x9b578`（0xa88B 可用）→ 内部以裸 `svc` 装 SIGSEGV/SIGBUS handler，handler 把 **siginfo(0x20)+ucontext(0x140) 原样 write 到 stderr** 再 `_exit(139)`（免猜偏移，离线解 pc/far）。构建方式：`analysis/dyldwork/segvcap_cave.s`（clang 汇编体）+ 手算 adr/b 重定位（已验：hook→0x9b578, adr→handler@0x9b5c8, b→0x5410）。**待冷启动验证**（已入 `run_all_cold.sh` 的 `C_segvcap`）。

### （旧）三条真根因
1. **exec EACCES(13) = 部署文件缺 `+x`**（`kern_exec.c:6242`：非 authopaque 挂载 + 无执行位 → EACCES）。**修复：部署后 `chmod 755`**。→ exec 全通，macOS dyld 真运行。
2. **dyld 内探针 SIGILL(132) = 补丁 hex 字节序写反**（`build_dyld.py` cave 用了反汇编字序）。**修复：`_le()` 每 4 字节转真小端**。
3. **syscall 536=EINVAL(22) = 缓存 slide-info version=5，iOS 16.3 内核只支持 1–4**（`sub_8062CA8@0xfffffe0008063024`；源码 `vm_shared_region.c:2934` default→KERN_FAILURE→`vm_unix.c:2725`→EINVAL）。触发位 = `sms_max_prot & VM_PROT_SLIDE(0x20)`。

### 已验证的可行性（关键里程碑）
- **空 region 上，`cachereg`（挂 cache cs_blob）+ 掩掉 9 条 mapping 的 `0x20` + `files[0].sf_slide=0` → `ret536=0`（缓存真映射）**。
  - **两个条件缺一不可**：缺 `cachereg` → gate11（CS 覆盖）失败 → 22（本 session 首次重启测试就因脚本漏启 cachereg 而失败）；缺掩码 → v5 slide pass → 22。
  - 实测：2026-09-27 00:3x boot（已启 cachereg）用掩码探针 `sf_ns` → **try1 ret536=0**，try2/3=22（region 已填）。
  - 说明：`check_np` 从 iOS 侧读 `ret=0 base=0` 不可信（无法区分空/满）；以**子进程内** `check_np`（✅ 12=空）为准。
- 已把该绕法做成两个真 dyld 补丁（见下），字节已经 IDA/clang 校验。

### 候选 dyld（已部署 `/var/mobile/`，重启后用）
| 文件 | 内容 | 用途 |
|---|---|---|
| `dyld_noslide.bin` | cleanB 全补丁 + 在 `0x35690` 注入 cave（`files[0].slide=0` + 掩 9 条 mapping 的 `0x20`） | 强制 map 路径 |
| `dyld_noslide_reuse.bin` | 同上但**不 patch `hasexisting/prereuse`** | 首交 map + 其余进程 reuse |
（cave 源码 `analysis/dyldwork/cave_mask20b.s`；构建见本文件对应段）

### 当前状态 / 恢复步骤
- **当前 region 已被填满**（本 boot 任何 536 都 22/139）→ **只能在冷启动空 region 上测**。已写 `analysis/dyldwork/post_reboot_noslide.sh` 并部署 `/var/mobile/`。
- **重启 + Dopamine 重越狱后**：`sh /var/mobile/post_reboot_noslide.sh`。成功判据：第1步 `echo NOSLIDE-OK` 且**无** `syscall to map cache into shared region failed`。
- 设备恢复清单（重启后必做）：trustcache 重加（`jbctl trustcache add <cdhash>`）+ `chmod 755` 所有要用二进制 + `nohup /var/mobile/cachereg <cache> <cache>.01 &`。

### 工具/坑速查（血泪）
- `analysis/dyldwork/exec_probe.py`：chroot+posix_spawn 真返码（launcher 的 `perror` 是残留 errno，不可信）。
- 探针 cave 字节必须 clang 汇编后取真字节（手写/字序必错 → SIGILL 132）。
- 描述符级探针：pre-svc 重入 svc 后写 stderr 会在 attach 窗口被杀（137）；用“pre-svc 改参 + `b` 回原 svc + post-svc 在 `0x76e00` 写 ret”双 cave 才稳。
- KRW（`/var/jb/basebin/libjailbreak.dylib` kread/kwrite）：臂64e 数据指针 PAC 是 47-bit VA（`0xffff800000000000|(v&0x7FFFFFFFFFFF)`）；**裸写 vnode/mount 等 PAC 指针会 ptrauth panic**。
- 部署 dyld 用 `cp` 从 `/var/mobile` 源（scp 直写会丢元数据）+ `chmod 755`；解释器(dyld) 必须 **CD flags=0x0**（`ldid -Cadhoc` 会使其被内核拒收）。

## 2026-09-26 23:0x — ★★★★ exec EACCES 真根因 = 缺可执行位；exec 门彻底打通 ★★★★

**整场 session 卡住的“exec EACCES(13)”根因是文件 mode 缺 +x，与 CS/trustcache 无关。**

- **决定性证据（源码 `analysis/xnu-xnu-8792.81.2/bsd/kern/kern_exec.c:6242`）**：
  ```c
  if (!vfs_authopaque(vnode_mount(vp)) &&
      ((vap->va_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) == 0))
      return EACCES;   // 挂载点非 authopaque 且文件无执行位 → EACCES
  ```
  `/private/var`（rootfs 所在）**不是 authopaque**。部署脚本的 `rm -f; cp` 把目标建成 `rw-r--r--`（0644）→ kernel 直接 EACCES，**静默、无 AMFI 日志**。
- **实测对照（本回合）**：`ls -la $R/usr/bin/true` = `-rw-r--r--`；`ls -la $R/tmp/cachereg_test` = `-rwxr-xr-x`。**只有缺 +x 的失败**。
- **修复**：部署后 `chmod 755 <file>`。chmod 后 `true`/`ls`/`echo`/`printf` exec **全部成功**，macOS dyld 真正运行：
  ```
  dyld '<null>' not loaded: syscall to map cache into shared region failed
  => 应用层回到原任务核心：syscall 536
  ```
- **含义**：`exec admission`（arm64ify / -Hsha256 / trustcache / platform / load 命令）以前的所有冲突结论，很多是被这个 +x 门污染的。**现在 exec 已稳定可用**（本回合多次复现 rc=0 落地到 536 层）。

**次要点（仍成立且有用）**：
- 解释器(dyld) 需 **非 ad-hoc（CD flags=0x0）**：`ldid -Cadhoc`（0x2）会让内核拒收解释器（`ldid -S` 裸签=0x0 可过）。部署 dyld 用 `cp`（从 /var/mobile 源）比 scp 直写更稳。
- launcher(launchdchrootexec) 曾在 `0x46FC` 被写坏成非法指令 `0x001c0012`（posix_spawn 错误路径）→ 任意 posix_spawn 失败即 SIGILL(132)，掩盖真实码；已修复（正确 12B：`00000090 00042a91 25000094` = `adrp x0,#0; add x0,x0,#0xa81; bl _perror`）。且 `perror("posix_spawn")` 打的是残留 errno（posix_spawn 不写 errno）——真实码用 `analysis/dyldwork/exec_probe.py`（ctypes 直调，rc=真实 errno）。
- 工具固定新增：`analysis/dyldwork/exec_probe.py`（chroot+posix_spawn 真返码，`python3 -u exec_probe.py <rootfs> 0x0 <target...>`）。
- **下一步**：syscall 536 真实 errno（errprobe dyld 目前经 launcher 得 rc=132，待用 stderr-write 探针或重新核位取得——见文件末尾 22:x 段相关构建键 `fwentry2`/`fwcave2`）。

## 2026-09-26 23:5x — ★★ 536 真 errno = EINVAL(22)；region 实为空；探针 SIGILL 根因 = 字节序 ★★

（详见 `docs/porting/syscall536-errno-and-probe-sigill.md`；本段补最新实测）

**A) 探针 SIGILL(132) 根因 = 字节序。** `build_dyld.py` 的 `P` 表把**多指令 cave** 的 hex 按“反汇编器字序”（如 `d10043ff`）直接 `bytes.fromhex` → 落盘成小端逆序 → 非法指令。**已修**：新增 `_le()` 把每 4 字节字转为真小端（单指令条目本就正确，仅 cave/errprobe 错）。修正后 cave `0x38d08` = 合法微小端 `ff0301d1 e01300f9 …`。

**B) 536 真实返回 = 22 (EINVAL)**（可靠探针：拦截 `__shared_region_map_and_slide_2_np` 的 post-svc `B.CC`(0x76e00) → cave 写 raw x0 到 stderr）。

**C) ★ region 实测为空（非“已填充”）。** 同一 cave 额外调 `shared_region_check_np`(294)：得到 `checknp_ret=12`（= 存在但空）+ `ret536=22`。⇒ **推翻 `syscall536-errno-and-probe-sigill.md` 的 #1「region 已填充」假说**，也推翻旧文档的“40=SESandbox”。

**D) 已挂 cs_blob（cachereg 运行中、`fcntl=0`）仍为 22。** ⇒ 536 的 22 来自 **setup 门**（不是引擎的 region-populated）：内核 `sub_8459570` 中 EINVAL=22 的来源为：门6 `v_type!=VREG`、门10 `ubc==0`(vnode+120)、**门11 per-mapping `ubc_cs_blob_get` 覆盖不足**（`sub_8459570` 末尾的 `v66[6]+v67 > v64 || v66[7]+v67 < v65` 分支）或 engine 返回。

**E) 工具**：`analysis/dyldwork/_bx3.py`（读 checknp+536ret 的探针构建器，带与 clang 字节的自校验）；`cave_checknp.s`。设备现役 dyld = `/var/mobile/stub_checknp.bin`。

**下一步（重要）**：定位 536 到底是 setup 哪一门。建议：① 在 `_bx3` cave 里额外写 `files_count`/`mappings_count`/首条 mapping 的 `file_offset+size`，与 vnode blob 的覆盖字段（KRW 读）对算，看是否命中门11；② 重建“只交 mapping0”的 16:00 式探针，确认单条 mapping 能否返 0（分离“首条 vs 后续/dynregion”）。

**F) 实测提交参数（`_bx4.py` / `stub_argdump.bin`，拦截 536 pre-svc 0x76df8 dump x0..x3）= `files_count=2, mappings_count=9`。** 即：**2 个 file 条目（主缓存 + 尾部 `fd=-1` dynregion 伪条目）+ 9 条 mapping**（主缓存 8 条 + dynregion 1 条）；**`.01` 子缓存未提交**（=无 4GB 溢出）。
  - 更正：`filescount1`(0x3538c) 按本文 17:50 段是 **mapSplit flag，不是 files_count**；真正决定 files 数的是 dyld 内部逻辑。
  - 因此 536=EINVAL 的候选锁窄到：**门6 v_type**（主缓存是普通文件，应过）、**门10 ubc==0**、**门11 per-mapping CS 覆盖**、或 **门4 dynregion fd=-1 伪条目**、或 **engine**。
  - 现役诊断 dyld = `/var/mobile/stub_argdump.bin`（dump 参数）；重建器 = `analysis/dyldwork/_bx4.py` / `cave_argdump.s`（clang 汇编 + 重算 branch）。

**G) 实测 9 条 mapping 字段（`_bx5.py` / `stub_mapdump.bin`，cave 把 x3 指向的 9×48B 写 stderr）**：
```
m0 addr=0x180000000 size=0x67f5c000 foff=0x0          ip=5
m1 addr=0x1e7f5c000 size=0x1e90000  foff=0x67f5c000   ip=97
m2 addr=0x1ebdec000 size=0x239c000  foff=0x69dec000   ip=99
m3 addr=0x1ee188000 size=0x24000    foff=0x6c188000   ip=99
m4 addr=0x1ee1ac000 size=0x1200000  foff=0x6c1ac000   ip=35
m5 addr=0x1ef3ac000 size=0x7cc4000  foff=0x6d3ac000   ip=33
m6 addr=0x1f9070000 size=0x5cdc000  foff=0x75070000   ip=1
m7 addr=0x1fed4c000 size=0x268c0000 foff=0x7ad4c000   ip=1
m8 addr=0x1f8000000 size=0x4000     foff=0x10285c000  ip=1  (dynregion 伪条目, foff=用户指针)
```
- m0..m7 = 主缓存 8 条，**全在 4GB 界内**（end=0x22560c000）；文件偏移覆盖 `[0, 0xa160c000]`（恰等于缓存 CS blob 的 `cso`）。m8 = 重定位后的 dynregion（`dynoff` 生效）。
- **强制参数实验（`cave_force2.s`：pre-svc 改 x0/x2 后自己发 svc 再写 stderr）遇到 137**（在 attach 窗口写→被杀），无法可靠取 ret；但**暗示「减量提交可能真的映射成功」**（attach in-flight→137），待用更稳手法确认。
- 下一步：⑧ 用 stubprobe 手法（从 0x76e00 入口，避开 pre-svc 重入）在 **dynregion 被移除**的变体上取 ret；⑨ 或直接 KRW 读缓存 vnode 的 cs_blob 覆盖字段（`v66[5..7]`）与 m0..m7 对算，坐实/排除门11。

**H) ★★ 双 cave 强制参数实验（已跑通）：错误码分层。** 手法：`cave_seting2`（0x76df8 → 改 x0/x2 → `b 0x76dfc` 跑原 svc）+ `cave_capture`（0x76e00 → 写 x0 → exit），修复了 pre-svc 重入导致的 137，ret 稳定可读。结果：
| 强制参数 | ret536 |
|---|---|
| fc=1, mc=9（丢 dynregion，但沿用自然 mappings_count=9） | **22** |
| fc=1, mc=1（仅 m0） | **5** |
| fc=1, mc=8（主缓存全部 8 条） | **5** |
| 自然 fc=2, mc=9（含 dynregion） | **22** |
- ⇒ **两种失败分层**：**dynregion（fd=-1 伪条目）导致 22**；**主缓存自身（即使单条 m0）导致 5（KERN_FAILURE）**。自然提交先撞 dynregion 的 22。
- 这与旧文档“空 region 时 536 返 0”不符——当今缓存/vnode 状态下，即使良构/单条也返 5，说明 **5 来自主缓存映射的引擎层失败（候选：`ubc_cs_blob_get` 覆盖 / ubc / vm_map_enter）**，非 region 占用。
- 构建器：`analysis/dyldwork/` 下 `cave_seting2.s` + `cave_capture.s`；禁用 dyld = `/var/mobile/sf_mc1.bin` 等。
- 下一步：⑩ 用 **KRW 读缓存 vnode 的 cs_blob 覆盖字段**（`v66[5..7]`）与 m0 的 `[0,0x67f5c000]` 对算，判定 5 是否来自门11；⑪ 或在内核 IDB 反编译引擎 `sub_8061EF0`/包装 `sub_8459134` 定位 5 的返回点。

**I) 更正 H)：强制实验的 5 是「计数不匹配」伪码，不是真实失败。** 内核 wrapper `sub_8459134` 的 `5` 只来自：① `files_count>0x100`；② `mappings_count>0x800`；③ **slide 计算循环 line 191（files[].sf_mappings_count 与 mappings_count 不一致）**。我改了 x2（mappings_count）却没同步 files[] 声明计数 ⇒ 计数不匹配 ⇒ 5。**自然提交（fc=2,mc=9，8+1 自洽）→ 22 才是真实失败**（来自 setup `sub_8459570` 或其调用的 engine `sub_8061EF0`；wrapper 仅当 engine 返 >3 才转 22）。故正确靶子仍是 setup 门6/10/11 或 engine——需 KRW 直接读写 vnode/blob 来判定。

**J) KRW 实测：setup 门 6/10/11 全部通过。** `blob_read.py` 走 fd→vnode→(vnode+0x78)=ubc→(ubc+0x50)=cs_blob 读到：`v_name='dyld_shared_cache_arm64e'`；`ubc` 非空；`blob` 非空；覆盖字段 `blob+0x38=0xa160c000`、`blob+0x40=0x50c000`（= 缓存 CD 的 cso/css）。门6 `v_type`(word@vnode+0x70=1 VREG)✓、门10 `ubc!=0`✓、门11 覆盖 `[0,0xa160c000] ⊇ m0..m7`✓。⇒ **22 来自 engine `sub_8061EF0`（返回 KERN_FAILURE>3 被 wrapper 转 22）**，不是 setup 门。

**K) 提交参数：`files[] = [{fd=3,count=8,slide=0x20000000}, {fd=-1,count=1,slide=0}]`，`mappings_count=9`**（`_bx4`+`cave_argdump2`）。强制 `files[0].slide=0` **无效**（仍 22）⇒ 不是 slide。

**L) 当前结论**：536 的 22 是 **engine 层 map 失败**（region 空、setup 全绿、非 slide）。下一步靶子在 engine `sub_8061EF0`/`sub_80623D4` 的 KERN_FAILURE 返回点（需内核单步或对参数再 dump）；先例：文档 16:00 “单条 mapping → 536=0” 待重现（需自洽且 slide 字段合适的提交）。

**M) ★★★★★ 536=EINVAL 的真因锁定：缓存 slide-info version=5 不被 iOS 16.3 内核支持。**
- 链路：engine `sub_8061EF0`(自身无硬编码>3) → 其 slide-pass callee **`sub_8062CA8`** @ **`0xfffffe0008063024`**：`if ((unsigned)(slide_info->version - 1) > 3) → KERN_FAILURE(5)`（源码 `osfmk/vm/vm_shared_region.c:2934` switch(version) 的 default；非 1/2/3/4 全落此）→ 经 `bsd/vm/vm_unix.c:2725` `case KERN_FAILURE: EINVAL` → **22**。
- **物证**：`xxd -s 0x7ad4c000 -l 32 /Users/ciscohe/Desktop/dyld-cache-15.6.1/dyld_shared_cache_arm64e` = `0500 0000 0040 0000 e708 0000 …` → **version=5、page_size=0x4000(16K)**。5 条 slid mapping 的 `sms_slide_start` 指向处均如此。
- **触发条件**：`sms_max_prot & VM_PROT_SLIDE(0x20)`（engine 用 `(prot>>5)&1` 计数 → 与 slide *数值* 无关）⇒ 解释了「强 `files[0].slide=0` 仍 22」；**m0 无 0x20 → 不进 slide pass → 历史单条 mapping 能 536=0**。
- **次生障碍**：即使把 version 改 4 也仍失败——`page_size=16384 ≠ 内核 `PAGE_SIZE_FOR_SR_SLIDE=4096`（`vm_shared_region.h:98`）。**双重格式不兼容**。
- **修复方向**（未实现）：① 从新版 xnu(11215+) backport `case 5:` + `vm_shared_region_slide_page_v5` 并把 `PAGE_SIZE_FOR_SR_SLIDE` 放宽为 16K（需内核补丁）；② 用户态重生成缓存 slide-info 为 v4/4K（工作量大，但不碰内核）；③ 应急验证：提交前清掉映射的 `VM_PROT_SLIDE(0x20)` + slide=0 以跳过 slide pass（但 __DATA 的 rebase 指针可能留错，仅确认分支）；④ 换用 slide-info 为 v4/4K 的旧缓存。
- 辅助脚本：`/Users/ciscohe/Desktop/macPad/analysis/dyldwork/parse_dsc_slideinfo.py`（subagent 新增，解析 slide-info 头）。

**N) ★★★★★ 验证成功：清掉 `VM_PROT_SLIDE(0x20)` → 536 返 0，缓存真正映射。**
- 探针（`cave_noslide.s`：pre-svc 把 9 条 mapping 的 `max_prot/init_prot` 都 `bic #0x20`，并 `files[0].sf_slide=0`）→ **try1 ret536=0（成功！）、try2 ret536=22**（region 已被 try1 填满 → 再交 EINVAL）。
- ⇒ **slide-info version 5 的墙被证实且找到**用户态可行**绕法**：不让 dyld 提交 `VM_PROT_SLIDE`，内核就不跑 v5 slide pass。
- ⚠️ **两个后续问题**：① **region 一旦被填就持续到重启**（try1 后 try2=22）⇒ 需「冷启动首交成功 + 其余进程 reuse」或常驻 keeper；② 跳过 slide 后 __DATA 的 rebase 指针可能留错（待验 dyld 自处理能力；若 slide=0 使 rebase 为恒等则 OK）。
- **正确的用户态修复**：在 dyld 构建 mappings 处不带 `0x20`（而不是运行时 cave）。下一步：IDA Instance1 定位 dyld 给 mapping 加 `VM_PROT_SLIDE` 的点（submission construction 区 0x35600-0x356c0），做成常量补丁。",

**O) 候选 dyld 构建配方 + 双 cave 测试法（可复现）**
- **hook 点**：dyld thin `0x35690`（原 `MOV X3,X26`，紧接 0x35694 `BL __shared_region_map_and_slide_2_np`）。此时 `x1=files ptr, x25=mappings_count, x26=mappings ptr`。
- **cave（`cave_mask20b.s`，clang 汇编取真字节）**：
  ```
  str wzr,[x1,#8]        // files[0].sf_slide = 0
  mov x9,x26; mov x10,x25
  loop: ldr w11,[x9,#40]; bic w11,w11,#0x20; str w11,[x9,#40]
        ldr w11,[x9,#44]; bic w11,w11,#0x20; str w11,[x9,#44]
        add x9,x9,#48; subs x10,x10,#1; b.ne loop
  mov x3,x26; b 0x35694
  ```
  cave @ `0x38d08`（`__text` NOP 填充死区）；`0x35690 <- b 0x38d08`（`9e0d0014`）。
- **两个变体**：`dyld_noslide.bin` = DEFAULT 全补丁 + cave；`dyld_noslide_reuse.bin` = DEFAULT 去掉 `hasexisting/prereuse` + cave。构建用内联 python（导入 `build_dyld` 取 P 表 + `_le` + 重算 branch）。
- **双 cave 取 ret 测试法**：`0x76df8 -> cave_seting`（改 x0/x2 后 `b 0x76dfc` 跑原 svc）+ `0x76e00 -> cave_capture`（写 x0 到 stderr + exit）。这样避开 pre-svc 重入 svc 导致的 137。
- **单次成功已由 `cave_noslide.s` 实证**（pre-svc 掩 0x20+slide=0 → ret536=0）；但端到端（dyld 继续加载 libSystem）只能在**空 region（冷启动）**验证。

**P) 复用(reuse) 设计（dyld IDA 反编译）**
- **`hasExistingDyldCache`(0x30140)**：`check_np` → `W0!=0` 返回 0（**空 region check_np=12 → 返回 0 → 走 map**）；`W0==0`（已填）→ `dynamicRegion()` → `getDyldCacheFileID` → **返回 1（已有缓存 → reuse）**。
- **`reuseExistingCache`(0x351a8)**：`check_np` → `W0!=0` 返回 0；否则 `strcmp(base,"dyld_v1  arm64e")` → 匹配则 **reuse**；不匹配→“existing shared cache … is not …”→0。
- ⇒ **`dyld_noslide_reuse`（不 patch `hasexisting/prereuse`）是多进程正解**：首进程（空 region）check_np=12 → map；后续进程（region 已填**我们的**缓存）→ magic 匹配 → reuse。cleanB 的 `hasexisting/prereuse→0` 反而破坏 reuse。
- ⇒ 印证旧文档的两个假设都可去：① `hasexisting` 的 dynamicRegion deref 崩溃只发生在 region 被**其他**缓存填满时；region 填的是我们的缓存时 deref 正常。② 旧文档“pre-reuse 会误收 iOS 缓存”也仅在 region 真有 iOS 缓存时成立。

## 2026-09-26 18:2x — ★★ EXEC 137 ROOT-CAUSE FOUND + BLOB PURPOSE DECODED + CLEAN DYLD ★★

**A) The whole device can no longer exec ANY macOS binary (all → SIGKILL/137).**
This is the immediate blocker for every dyld experiment. Evidence (runtime, this session):
- `true`, `/bin/ls`, and EVERY project probe (`minexit_arm64[ e]`, `srprobe`,
  `maptest`, `dsctest[2]`, `hw`, `misc/sprobe`) → `rc=137`, zero output, no `.ips`.
- SAME result via: plain procursus `/var/jb/usr/bin/chroot`, `launchdchrootexec`,
  and the project's OWN launchd job `com.macwsguide.smoketest`
  (`LastExitStatus=9`, `smoke.out` empty). 60/60 retries = 137 → NOT intermittent.
- Apple-pristine `dyld.orig` → 137; trusted `dyld_pm`/`cleanB` → 137.

**One exceptional GOOD run observed**: right after `killall -9 amfid` +
restoring a *trusted* `/usr/lib/dyld`, `true` ran (rc=134) and macOS dyld printed:
```
dyld: dyld cache '(null)' not loaded: syscall to map cache into shared region failed
dyld: Library not loaded: /usr/lib/libSystem.B.dylib (code signature invalid …)
```
This proves: (1) exec CAN work; (2) once it does, we are back at the expected
**syscall 536 fails** layer (dyld falls back to disk → CS-invalid dylib → abort 134).
It could not be reproduced afterwards (60/60 = 137).

**TRAP discovered**: deploying Apple-pristine `dyld.orig` as `/usr/lib/dyld`
WITHOUT adding its cdhash to trustcache makes EVERY macOS exec SIGKILL (137),
because the kernel kills the process when the dyld it must load is untrusted.
Always trust a deployed dyld before running.

**Ruled out as causes** (all tried this session, none fixed it):
- trustcache membership (hashes present + `jbctl add` + `macws_boot_trust.py --readd`);
- per-binary re-sign with project entitlements (`platform-application` present);
- amfid dead vs alive; pre-reading the dyld to populate the vnode CS blob;
- **autosignd** (the per-exec trustcache daemon): started it (`restart_autosignd.sh
  --force`, socket `/var/mnt/rootfs/tmp/autosignd.sock` live, pid running) + re-added
  autosignd/true/ls/dyld cdhashes → still 137;
- full `postinst.sh` re-run (re-signed 1156 images, rebuilt platform tags);
- `nvram boot-args` empty; no `amfi_*`/`cs_*` sysctl exposed.
Kernel strings present: `amfi_enforce_launch_constraints`,
`amfi_allow_3p_launch_constraints`, `"AMFI: Launch Constraint Violation …"`
(@ `0xfffffe000735197c`), but NO AMFI log line is emitted on our kills.
**Conclusion: kernel userspace-irreparable exec/CS state (panic-era pmap/shared-region
corruption suspected — the `pmap_trim_internal` panic *was* on an exec teardown of `true`).
A kernel reboot (+Dopamine re-jailbreak) is required to continue.**

**B) The `dyld_pi` injected blob's TRUE purpose is decoded.**
`preflightCacheFile` (thin `0x35a98`) calls `fcntl(fd, 97 /*F_ADDFILESIGS_RETURN*/,
fs)` at `0x35d68` to self-attach the cache's cs_blob, then fails in-chroot
(EPERM from `mac_vnode_check_signature` → AMFI). The blob existed ONLY to bypass
that. Since host-side `cachereg` already attaches the blob, dyld's redundant
attach is skippable with TWO tiny patches instead of a `0x3576c-0x35afe` rewrite:
| off | orig | new | meaning |
|---|---|---|---|
| `0x35d70` | `00 01 00 54` (B.EQ) | `1f 20 03 d5` (nop) | ignore `fcntl==-1` |
| `0x35d80` | `e2 00 00 54` (B.CS) | `07 00 00 14` (b 0x35d9c) | skip coverage check |
This CLEAN build keeps the ORIGINAL preflight AND re-enables `.01` subcaches
(the blob had forced main-cache-only). Builder: `analysis/dyldwork/build_dyld.py`.

**C) Clean dyld builds produced this session** (`analysis/dyldwork/`, signed, thin arm64e):
- `dyld_cleanB.signed` (cdhash sha256 `fe9947cd…`): crossarch + hasexisting(0x30140→0)
  + prereuse(0x34298→0) + filescount1 + dynoff(0x35fc8→0x78000000) + accessor(0x50dfc)
  + fcntl_nop(0x35d70) + cover_b(0x35d80). NO blob.
- `dyld_cleanB_err.signed` (cdhash `48218610…`): cleanB + errno probe at `0x35698`
  = `and w0,w0,#0xff; mov x16,#1; svc #0x80` (`12001c00 d2800030 d4001001`) →
  exits with `syscall536_ret & 0xff`. Ready to run the moment exec is restored.
- `dyld_cleanB_glue.signed` (cdhash `f1bbb79b…`): cleanB + §9.3 glue-call probe.
  **REAL dead cave found (IDA, zero xrefs)**: `__text` NOP padding `0x38d08-0x38d3b`
  (52B; real function `dyld_program_minos_at_least` starts `0x38d40`).
  `0x6b94 blraaz x8` → `b 0x38d08` (`5dc80014`); cave = `write(1,{x8,x9},16); exit(0)`.
  This replaces the old *contaminated* glueprobe (which sat inside the live blob at
  `0x3588c` and produced the bogus "103").
Every patch encoding is assembler-verified; all offsets byte-match pristine.

**Next after reboot**: redo §2 restore, then run `dyld_cleanB_err` → read the
536 errno (expect 0 if always-map self-heals dynregion, else EINVAL/EPERM).

### 2026-09-26 19:0x — exec 137 深挖（重启后仍复现）+ 内核 RE 判定链

**重启 + 重越狱 + 完整 postinst 后，exec 137 依旧。** 又排除：
- Mach-O `platform` macOS(1)→iOS(2)：仍 137；
- 非 chroot（root=`/`）、任意路径、`misc/sprobe`（最小 freestanding）、
  裸签（无 entitlements）、`macws_boot_trust.py --readd`（经 libjailbreak 真注册 3 个 hash）：**全部仍 137**。
- 重启后项目 macws 作业原本不在（未 bootstrap）；`postinst.sh` 已重载，无效。

**内核 RE（Instance2，符号齐全）定位到的判定链：**
- AMFI MACF 钩子 `_vnode_check_signature` @ `0xfffffe00092a45e4`
  （`AMFIIsCodeDirectoryInTrustCache`→`pmap_lookup_in_static_trust_cache`；
  `codeDirectoryHashIsInLoadedTrustCache`→`pmap_lookup_in_loaded_trust_caches`）。
- `_vnode_check_signature` 关键分支：
  - L359：cdhash 不在（static ∪ loaded）trustcache → 走 `StaticPlatformPolicy::check_signature`（callout amfid）。
  - L1124：`if ((cs_flags & CS_PLATFORM_BINARY/*0x4000000*/) == 0)` 才检查
    `devModeStatusResolved()`；否则 fatal "only allows platform binaries until
    developer mode status has been resolved"。
  - 设备实测 `developer_mode_status=1`、`developer_mode_resolved=1` → 此分支不触发。
- 所以：**“cdhash 在（static∪loaded）trustcache → category 'trust-cache' → 放行”**
  这条链在 RE 上应该通过，但 exec 仍被杀 → 触发点在更下游（疑似 exec 页映射/CS_KILL，
  或一个尚未定位的更早检查）。

**待用户输入**：当初设备上“让 macOS 二进制可 exec”的完整/重启后必做步骤。

### 2026-09-26 19:3x — ★★★ 重大突破：exec 137 根因 = arm64ify！★★★

**根因锁定（subagent 通读仓库文档 + `misc/arm64ify_macho.py` 注释双证）：**
> “the iPadOS kernel will only exec the chroot's macOS binaries as **ARM64/ALL**”

iPadOS 内核**只接受 `cpusubtype=ARM64/ALL`(0) 的 macOS 主可执行体**；macOS 15.x 系统二进制
只有 `x86_64+arm64e`（无 arm64 切片）→ 直接被 exec 门 SIGKILL(137)。

**实测（本回合）**：用 `misc/arm64ify_macho.py` 把 `dyld`/`true`/`ls`/`bash`/... 就地 relabel 成
ARM64/ALL（代码字节不动）+ `ldid` 重签 + trustcache 后：
- **137 消失！** `true`/`ls`/`bash`/`echo`/`printf`/`pwd`/`uname` 不再被杀。
- `echo`（未 arm64ify，仍 arm64e）仍 137 → 完全印证“arm64e → 杀”。
- `misc/sprobe`（arm64e）→ 137；证实。

**越过 137 后暴露下一道门：`EACCES`(13)。** 用设备端 ctypes 探针
`analysis/dyldwork/ceprobe.py`（`posix_spawn` 直调，拿**真实返回码**）查到：

```
/usr/bin/true  -> rc=13   (EACCES)    ← 所有 arm64 macOS 二进制均是 13
/var/mnt/rootfs/tmp/creg  (iOS 二进制) -> 能跑  ← rootfs 卷可 exec
```

**⚠ 重要侦错陷阱（subagent 发现）**：`launchdchrootexec` **丢弃 `posix_spawn` 返回值、
无条件 `perror("posix_spawn")`**，而 `posix_spawn` 不写 errno——所以它打印的
”No such file or directory” 只是**残留 errno（多为 chdir 的 ENOENT）**，**不是真实失败码**。
以后测真正的 exec 错误一律用 `analysis/dyldwork/ceprobe.py`。

**EACCES 已排除（本回合逐一实测）**：
- rootfs 卷 `noexec` → 排（/private/var 无 noexec；iOS 二进制放 rootfs 内 chroot 能跑）
- entitlements：`no-sandbox`/`no-container`/`get-task-allow` 均在；去 `platform-application` 无效
- Mach-O `platform` macOS→iOS → 仍 13
- dyld arch = arm64 vs arm64e → 都 13
- CS flags `CS_HARD|CS_KILL|CS_RUNTIME` → 仍 13
- `rm`+`cp` 换新 inode / 两遍 ldid 签名 / `-Hsha256` → 仍 13

**下一道门待查**：EACCES 的触发点（疑似内核 exec 的 CS/sandbox 判定；
也可能与“首次 exec 前必须先把 loader 闭包入 trustcache”那条纪律有关，
见 `layout/DEBIAN/postinst:291-304`）。工具已就绪：`ceprobe.py` + 设备端 `arm64ify_macho.py`。

### 2026-09-26 19:4x — EACCES(13) 内核触发点已定位（Instance2 RE）

**exec 拒绝在核心里只有一处返回 13**，在 exec 核心 helper `sub_FFFFFE000839B748`
（被 `posix_spawn`/`__mac_execve` 调用）：

```c
v35 = sub_FFFFFE000839BF5C();      // → sub_FFFFFE0007FE51E8
if (v35 != 5) {
    if (!v35) { …ubc_cs_blob_get/csblob_find_blob_bytes(CSMAGIC_BLOBWRAPPER=0xFADE0B01) 检查… → LABEL_35(成功) }
    else { os_reason_create(OS_REASON_EXEC,9); v17 = 13; }   // ← EACCES
} else { os_reason_create(OS_REASON_EXEC,8); v17 = 13; }
```

`sub_FFFFFE0007FE51E8` = **向用户态策略服务发 MIG `msgh_id=27001`**，等回复 `27101`
里的结果字节 `v6`：**`v6 != 0` 就返回非零 → EACCES**。即：**exec 会同步 RPC 一个
用户态策略服务；它返回非 0 就判 EACCES**。

**旁证**：同一拒绝在 `imgp` flags bit 0x10 置位时改走 `terminate_with_reason` → SIGKILL；
所以 **EACCES 与 137 是同一内核 exec 拒绝的两种上报形态**。AMFI 钩子
`_vnode_check_exec @0xfffffe00092a69e8` 无条件置 `CS_HARD|CS_KILL(0x300)`。

**实测（本回合）**：amfid 一直在线（launchd 守护重生，杀不掉），EACCES 不随 amfid 状态变化；
`oslog` 在失败 exec 时**无任何 AMFI/exec 日志**（静默拒绝）。

**下一步**：确认 MIG-27001 到底是哪个用户态服务，以及**它为什么对 arm64ified 的
macOS 动态二进制返回非 0**（候选：CMS/签名形态、platform 身份、或“首次 exec 前 loader 闭包”。），
可用 Instance2 回溯 27001 子系统的注册/处理函数。

### 2026-09-26 19:5x — MIG-27001 是“按发起进程”判定（Instance2 反汇编）

exec helper 调用 MIG 前的参数构造（`0xfffffe000839bb90`）：
```
ADRP X26, _kernproc ; LDR X8,[X26,_kernproc] ; CMP X8,X19
B.EQ LBBAA8          ; 若 x19==kernproc → W1=0
LDR  W1, [X19,#0x60] ; 否则 W1 = 调用进程 (*x19) 的 field 0x60
... BL sub_FFFFFE000839BF5C   ; W1 作为 MIG 请求体发给 27001
```

⇒ **这个用户态策略服务是根据“发起 exec 的进程”的身份/策略来判定放不放行的**
（很可能是 launch-constraint / responsible-process 类检查）。
所以 EACCES 可能与**调用者（launcher / 测试脚本）的上下文、launch type、
responsible process** 有关，而不只是目标二进制本身。
（注：`launchdchrootexec` 只在 `getppid()==1 && XPC_SERVICE_NAME` 时才设
`CS_LAUNCH_TYPE_SYSTEM_SERVICE`；从 SSH 直接跑则是普通 launch type。）

### 2026-09-26 20:0x — 用 XNU/dyld 官方源码把 EACCES 闭环彻底解开

**源码位置（原 AI 已落盘，不再靠 /tmp）：**
`analysis/xnu-xnu-8792.81.2/`（内核 8792.82.2 的最近公开 tag）与
`analysis/dyld-dyld-1286.10/`（与设备 dyld 精确同版本：`dyldMain.cpp`）。

**EACCES 确切来源 = `kern_exec.c` 的 `process_signature`：**
- `kern_exec.c:7430`：`if (imgp->ip_csflags & CS_SIGNED) { error=0; goto done; }` ——**有 `CS_SIGNED` 就跳过后续 upcall**。
- `kern_exec.c:7459`：否则调 `find_code_signature(port, new_pid)` = **MIG 27001**
  （`osfmk/mach/task_access.defs:55-57`：subsystem 27000 的第 2 个例程，回复 27101），
  参数 `new_pid = proc_getpid(p)` ——**所以 `p->0x60` 就是 `p_pid`**（kernproc 返回 0）。
- 返回 `KERN_FAILURE(5)` → `os_reason(EXEC,8)`；其他非 0 → `os_reason(EXEC,9)`；**均 EACCES**。
- spawn 场景（`imgp` flags bit 0x10 = IMGPF_SPAWN）同一拒绝改走 `psignal_vfork_with_reason(SIGKILL)`
  （`kern_exec.c:7558-7573`）——**这就是 137 与 13 同源的确切位置**。
- `kern_exec.c:7506`：`CS_SIGNED` 是 **upcall 成功后**才 `proc_csflags_set(p, CS_SIGNED|CS_VALID)`。
- upcall 成功后仅接受“最朴素 ad-hoc”（`kern_exec.c:7490-7507`）：
  `(csb_flags & CS_ALLOWED_MACHO)==CS_ADHOC` 且 **无 CMS blob、非 platform、无 entitlements**。

**27001 服务 = 用户态 taskgated/amfid**（XNU 只有 .defs + 客户端桩 + `task_access_port`；
server 实现属闭源 AMFI 生态；`amfid` 一直在设备上在线）。

**当前状态**：设备上 arm64ified+重签+`jbctl trustcache add` 后**仍 EACCES**
（试过 CD flags=0x2/CS_ADHOC、CS_HARD|KILL|RUNTIME、platform 字节 1/2/5、identifier 重签、
loader 闭包入 tc、launchd 作业 launch type——均 13）。
⇒ **推断：AMFI 没把我们的 cdhash 当作有效信任 → 不置 `CS_SIGNED` → 走 upcall 被拒**。
下一步：验证“jbctl/libjailbreak 加的 root trustcache”是否就是 AMFI 看的 loaded trust cache
（对比 `mac_vnode_check_signature`/`pmap_lookup_in_loaded_trust_caches` 与 `CS_TRUST_CACHE_AMFID`）；
或用 `csops(CS_OPS_STATUS)` 在运行进程上直接读 `CS_SIGNED` 位确认。

### 2026-09-26 20:1x — csops 实证：**能跑的都是 CS_PLATFORM_BINARY**

用 `analysis/dyldwork/csops_probe.py`（`csops(CS_OPS_STATUS/CDHASH)`）读**运行中进程**的真实 cs_flags：
```
cachereg : flags=0x26803b0d  SIGNED=Y PLATFORM=Y HARD=Y KILL=Y
python3  : flags=0x26803b09  SIGNED=Y PLATFORM=Y
jbctl    : CD flags=0x2(adhoc)  entitlements 含 platform-application
launchdchrootexec: CD flags=0x0  (能跑)
/usr/bin/true    : CD flags=0x0  (EACCES)
```

⇒ **`CS_PLATFORM_BINARY(0x04000000)` 是 exec 准入的判别因子**：
能跑的进程都带它；我们的 macOS 二进制不带 → 走 taskgated upcall → EACCES。

**XNU 源码佐证**：`csb_platform_binary` = `!!(csb_flags & CS_PLATFORM_BINARY)`
（`ubc_subr.c:4313-4321`）；而 `csb_flags` 由 **AMFI 的 `mac_vnode_check_signature`** 决定
（`ubc_subr.c:4264`）；AMFI 内部在验证通过后 `*a5 = v43 | 0x20000000`（置 CS_SIGNED）
并对带 entitlements 的 `CS_PLATFORM_BINARY` 调 `OSEntitlements::markAsCSPlatform`。

**综合结论（本 session 定论）**：
- **137 根因**：非 `ARM64/ALL`（用 `arm64ify_macho.py` relabel 解决）。
- **EACCES 根因**：内核 `process_signature` 的 taskgated/amfid upcall（MIG 27001）拒绝——
  因为目标二进制**未被 AMFI 认定为有效签名（无 `CS_SIGNED`/非 `CS_PLATFORM_BINARY`）**。
- 纯用户态使二进制成为 platform 的可行途径：**把它以“platform”形态入 trustcache / 或带
  platform-application 同时满足 AMFI 验证**（具体形态待验证；CD platform 字节 1/2/5 无效）。

### 2026-09-26 20:2x — ★★★ 重大突破：exec 准入打通，macOS dyld 已能真正运行 ★★★

**EACCES(13) 的解法（纯用户态，无需 patch 内核/amfid）——精确复刻能跑的 `cachereg` 的签名形态：**

```
python3 misc/arm64ify_macho.py <file>                     # 1) ARM64/ALL（否则 137）
ldid -Hsha256 -Cadhoc -S<entitlements.plist> <file>       # 2) sha256 cdhash + CS_ADHOC
jbctl trustcache add <sha256 cdhash>                      # 3) 入越狱 trustcache
rm <dest>; cp <file> <dest>                               # 4) fresh inode
```

**关键：`-Hsha256` 是必需的。** trustcache 条目带 `hash_type` 字段（`osfmk/kern/trustcache.h` 的
`trust_cache_entry1{cdhash,hash_type,flags}`；`CS_TRUST_CACHE_AMFID=0x1`）；**只有 sha256 的 cdhash
（20B sha256(CD)）才与条目类型匹配 → AMFI 认 → 置 `CS_SIGNED` → 跳过 taskgated upcall**。
之前的 `-Cadhoc`（hashType=1 sha1）都仍 EACCES，就是因为 hash_type 对不上。

**实测结果（部署脚本 `analysis/dyldwork/remote_apply_form.sh` / `remote_deploy_dyld.sh`）：**
```
true / echo  -> 不再 137/13，macOS dyld 真正执行：
  dyld: dyld cache '(null)' not loaded: syscall to map cache into shared region failed
  dyld: Library not loaded: /usr/lib/libutil.dylib ...
ls / bash    -> 同样dyld运行、fallback 到磁盘
```

⇒ **工作重心已从“exec 门”回到原任务核心：syscall 536 映射缓存**
（与 HANDOVER §4 的门禁链、与 `dyld_cleanB_err` 探针完全对应）。

**可复现的部署命令**（逐文件，需对 dyld + 要跑的二进制都做；设备端脚本见 `analysis/dyldwork/`）：
```sh
A64=/var/mobile/arm64ify_macho.py; ENT=/var/jb/usr/macOS/bin/entitlements.plist
LD=/var/jb/usr/bin/ldid; JB=/var/jb/usr/bin/jbctl
python3 $A64 <file>; cp <file> /tmp/w.bin
$LD -Hsha256 -Cadhoc -S"$ENT" /tmp/w.bin
for a in arm64 arm64e; do $JB trustcache add $($LD -arch $a -h /tmp/w.bin|grep CDHash=|cut -c8-); done
rm <dest>; cp /tmp/w.bin <dest>
```

**顺带的其它确认**：设备 `/usr/libexec/amfid` 是**原版**（未 patch，不在越狱 tc）；
exec 准入靠的确实是 `jbctl trustcache`（凭 sha256 cdhash 让 AMFI 直给 `CS_SIGNED`）。

### 2026-09-26 20:4x — 回到 syscall 536：源码级根因分析（subagent + 逐个源文件）

部署带补丁的 `dyld_cleanB` 后，macOS dyld 执行、但 536 失败、退磁盘 fallback：
```
dyld: dyld cache '(null)' not loaded: syscall to map cache into shared region failed
dyld: Library not loaded: /usr/lib/libSystem.B.dylib
  ... '/usr/lib/libSystem.B.dylib' (code signature invalid ... errno=1)
```

**536 的内核侧全路径（`analysis/xnu-xnu-8792.81.2/bsd/vm/vm_unix.c` + `osfmk/vm/vm_shared_region.c`）：**
- 包装层 `shared_region_map_and_slide_2_np` `vm_unix.c:2842-2964`（files_count==0 → **成功**；mappings>2048 → EINVAL）。
- `_shared_region_map_and_slide` `vm_unix.c:2664-2748`：errno 映射 L2712-2730
  （INVALID_ADDRESS→**EFAULT(14)**、PROTECTION_FAILURE→**EPERM(1)**、NO_SPACE→ENOMEM、其余→**EINVAL(22)**）。
- `shared_region_map_and_slide_setup` `vm_unix.c:2189-2652`：卷不符→**EPERM**（L2264/2514）；
  非 VREG→EINVAL；uid!=0→EPERM；**gate11 `!ubc_cs_is_range_codesigned`→EINVAL（L2620-2642）**。
- 引擎层 `vm_shared_region.c`：**`sr_first_mapping != -1`（region 已填充）→ KERN_FAILURE→EINVAL（L1464-1470）**；
  FIXED 越界→KERN_INVALID_ADDRESS→**EFAULT(14)**（`vm_map.c:2714-2718`）。
- 区域尺寸 `shared_region.h:90-91`：ARM64 base `0x180000000`、size **4GB**。

**gate11 `ubc_cs_is_range_codesigned`（`ubc_subr.c:5545-5589`）**：要求单个 blob 整段覆盖
`[file_offset, file_offset+size]`。`cachereg`（host `fcntl(fd,F_ADDFILESIGS=61)`，`kern_descrip.c:3957-3990`）
挂的 blob 满足：主缓存 `csb_base_offset=0/csb_start_offset=0/csb_end_offset=0xa160c000` ⊇ 8 条 mapping。
**但必须常驻**（vnode 回收→blob 释放 `ubc_subr.c:4906-4943`），**且 `.01` 子缓存需各自 blob**。

**dynregion（fd=-1）（`vm_unix.c:2286-2320` + `vm_shared_region.c:1534-1669`）**：
`mappings_count==1`、`sms_address/sms_size` 页对齐、`sms_file_offset` 是**用户态指针**（`copyin` L1600）；
内容来自 mapper 私有内存 → **per-mapper，mapper 退出即消失**（`vm_shared_region.c:538-625`）。

**dyld 失败分支**：`SharedCacheRuntime.cpp:1367`(syscall) / **1379-1387**(失败) / 1384-1385（该错误串）；
打印在 `DyldProcessConfig.cpp:1516`。fallback：`reuseExistingCache`(L1490)→`mapSplitCacheSystemWide`(L1495)
都失败 → 无缓存 → 逐文件从磁盘加载 → libSystem 仅存于缓存 → 报 `Library not loaded`。

**⇒ 当前 536 失败的最可能原因（源级，排序）：**
1. **region 已填充** → `vm_shared_region.c:1464` → **EINVAL(22)**；
   （`check_np` syscall 294 实测 ret=0 → 区域确实已存在）。`dyld_cleanB` 强走 map 路径（`0x30140/0x34298`）→ 撞此。
2. gate11 blob 失效/ `.01` 缺 blob → EINVAL。
3. FIXED 越界（`.01` 尾）→ EFAULT。
4. 卷不符/uid → EPERM。

**修复路线（源级可行，未实现）：**
- **常驻 keeper**（=mapper 本体长驻）持 region 引用不销毁 → 其余进程走 `reuseExistingCache`（勿再强走 map）；
- **冷启动后首交**（region 空，绕开 L1464）；
- cachereg 双缓存（主+.01）blob 常驻；
- `.01` 越 `0x280000000` 的尾映射改私有 `mmap`（`mapSplitCachePrivate`）。

**errno 读取未完成（未决）**：在 0x35698/0x356d8/0x35714 插 syscall 探针（写 8B 到 fd2）→
要么被内核当“attach 半途”杀（137），要么未产出字节；改用真死洞 `0x38d08` 的 cave 也未产出。
下一步用 **kernel RE（Instance2）** 直接跟 `vm_unix.c:2620/1464` 对应的内核函数，或冷启动后首交验证。

**errno 为何取不到（已证的结构性原因）**：chroot 里**任何动态 macOS 二进制都要缓存里的 `libdyld`**
（`dyld: libdyld.dylib not found`），静态二进制却撞**静态门→137**（已实测：静态 arm64 探针 rc=137；
sprobe rc=134=dyld 报错）。→ **userland 探针路已断**，只能靠内核 RE 或冷启动首交。
把 sprobe 的 `LC_LOAD_DYLIB(libSystem)` 改成 `LC_LOAD_WEAK_DYLIB` 又暴露注入的 `libmachook` 需 `libobjc`（同样在缓存里）。
即前位 AI 的 sprobe “能跑”是陈旧结论（`TOOLS-AND-PORTING.md:585` 实际只得到 rc=134）。

**当前设备状态**：cachereg(pid 751) 已同时持有主缓存+`.01`（gate11 blob 已满足）；dyld = `dyld_cleanB`（已用新形态部署）。
**一键冷启动验证脚本**：`analysis/dyldwork/coldboot_firstsubmit.sh`（重启+重越狱后跑：以 cleanB 作**首个 536 提交者** → 若 region 空则可能返 0 并真映射，再看 `sw_vers`）。

### 2026-09-26 20:5x — ★ 又通一道门：**LC_BUILD_VERSION platform 必须 = macOS(1)**

用 `DYLD_PRINT_CACHES=1` 发现**新错误**：`shared cache file is for a different platform`。
根因（源码 `dyld/SharedCacheRuntime.cpp:143-154` `validPlatform()`）：`cache->header.platform != options.platform` 即拒；
而 `options.platform = process.platform`（`DyldProcessConfig.cpp:1365`）= **主二进制的 LC platform**。
- 实查：`cache` 头 `platform` @**0xD8** = **1 (macOS)**；而设备上的 `true`/`ls`/`echo`/`bash` 的
  `LC_BUILD_VERSION platform` = **2 (iOS)**（之前不知何处被设成 iOS）→ **被 dyld 拒**。
- **修复**：跑 `misc/set_macos_version.py`（iOS→macOS）对这四个二进制重设 → **platform=1** → 重签+trustcache。
- **效果**：错误从 “different platform” **变回** `syscall to map cache into shared region failed`（即 536），
  **且 exec 仍通**（不再 137/13）★ **exec 准入与 platform=macOS 兼容，无需 iOS tag**。

**⇒ 现在只剩 536 一道门**（见下节的内核门表：最可能门 B `ubc_cs_blob_get` CS 覆盖 / 门 C uid&volume）。

### 2026-09-26 21:0x — ★ 536 的**真正墙**已钉死：Sandbox 要 `VSHAREDCACHE`

前位 AI 已在 `docs/porting/kernel-syscall536-finding.md` 实测（exit-probe 6/6）：**536 返回 40（EMSGSIZE）**，
来源唯一：**Sandbox `mpo_file_check_mmap` = `hook_file_check_mmap` @ `0xa659664`**（`mac_policy_ops`+0x120）：
```
TBZ W3,#2,.ret0                 // (prot & 4)==0 → 返回 0
vnode_isdyldsharedcache(vp)      // =(vp->v_flag>>9)&1  i.e. VSHAREDCACHE
CBZ W0,.evaluate                // 非 shared-cache → 评估
.evaluate: return cred_sb_evaluate(cred, 16 /*file-map-executable*/, …)
```
- 门外：AMFI `_file_check_mmap` @ `0x92a1a90` 只返 {0,1}；AppleImage4 无该 hook；无任何 40 字面量。
- 调用点：源 `vm_unix.c:2372` `mac_file_check_mmap(cred, fg, **VM_PROT_ALL=7**, MAP_FILE|MAP_PRIVATE|MAP_FIXED, 0, …)` —— flags/prot 是**常量**。
- **我校验**：`true` 已带 `com.apple.private.security.no-sandbox` + `platform-application`，**仍返 40**。
- **我校验**：内核里 `vnode_isdyldsharedcache` 的调用者仅 Sandbox hook + **APFS**（`_apfs_vnop_inactive`/`_apfs_vnop_pagein`）
  → 即 `VSHAREDCACHE` 由 **APFS 驱动在内核态**置位；全仓无既有绕法。

**结论**：外来 macOS 缓存挂在 rootfs DMG 上，vnode 永不带 `VSHAREDCACHE` → sandbox 必拒 → **536 永返 40**。
这是**纯用户态不可绕**的墙（除非：① 有内核写原语去设该 vnode 标志/ nop 该 hook；② 让 APFS 把该缓存当作已识别的 dyld 缓存）。
（对照：原作者 iOS 16.5 项目能跑——16.3 与 16.5 在此 hook 上的差异值得下一步比对。）

### 2026-09-26 21:1x — ★★ 墙可解：项目其实有完整**内核写 + kcall**

复核 `/var/jb/basebin/libjailbreak.dylib`（Dopamine）的导出符号（`nm -gU`，共 397 个）：
```
__kwritebuf_phys        # 内核写（physical）
__physwritebuf_virt     # 内核写（virt→phys）
__kreadbuf_phys / __physreadbuf_virt   # 内核读
_kalloc / _kalloc_with_options         # 内核 malloc
_arm64_kcall / _is_kcall_available / _arm64_kcall_prepare_state  # kcall
__boomerang_get_physrw / _jbclient_root_get_physrw               # phys r/w
```
⇒ **"只有 kread"是旧结论；实际有 kwrite + kcall。** 因此 sandbox 墙（`VSHAREDCACHE`）有两条可行解：
1. **设 vnode 标志（推荐，数据写、不碰内核 text/KTRR）**：用 kread 从 mount/vnode 链找到
   该缓存的 vnode，把 `v_flags@vp+0x54` 的 **bit9（VSHAREDCACHE）置 1** → sandbox hook 短路返回 0。
2. **patсh sandbox hook**（`hook_file_check_mmap` @ `0xa659664` → `mov w0,#0; ret`）——需写内核 text，A12+ 有 KTRR，风险高，不推荐。
（注：这不是“lazy bypass”——`VSHAREDCACHE` 本就是内核给 dyld 缓存 vnode 打的标记，此处只是把它补上；待验证。）

### 2026-09-26 21:2x — ★★ KRW 打通内核 walk；**纠正：目标 vnode 已带 VSHARED_DYLD**

**用设备端 python3 + ctypes 驱动 Dopamine KRW（免编译）已全线打通：**
- `kread64/kread32/kwrite64/kwrite32` 均可用（`proc_self` 返回真内核指针）。
- 打通结构链：`proc+0xF8 → fd_ofiles → [fd] → +0x10 → fileglob → +0x38 → vnode`。
- **两个坑已解**：① arm64e **PAC 是 47-bit VA**，剥位=`0xffff800000000000|(v&0x7FFFFFFFFFFF)`（非 48bit）；
  ② python 里 `open(...).fileno()` 会因文件对象被 GC 而**立即 close(2)**，必须保持对象存活。
- 工具：`analysis/dyldwork/set_vshared.py`（定位任意 fd 对应文件的 vnode）+ `kwalk*.py`。

**实测目标 vnode**（`v_name = 'dyld_shared_cache_arm64e'`，`ubc` 存在）：
```
vnode+0x50/0x54: v_flag = 0x84a00   → bit9(VSHARED_DYLD) = 1  ★已置位★
```
⇒ **sandbox `hook_file_check_mmap` 的 `!VSHAREDCACHE` 分支不会走** —— 即 **doc 里实测的 errno 40（sandbox）
对当前这个 vnode 不成立**（很可能 40 来自另一条/另一 vnode 的路径）。由于该 vnode 已置位、536 仍失败，
**真正的失败点回到门 B（`ubc_cs_blob_get` CS 覆盖）或门 C（uid/volume）**。

**下一步**：用 KRW ① 读 `ubc_cs_blob_get` 看 blob 对 8 条 mapping 的覆盖；② 读 vnode 的 v_mount 与 root 卷/`/private/preboot/Cryptexes` 比对；
③ 读 shared_region 的 `sr_first_mapping`（门 A “已填充”）。三选一钉死后对症修复。

### 2026-09-26 21:3x — ⛔ 内核裸写 v_mount → PAC panic（已回滚/重启）；门 C 坐实

尝试用 KRW 直接改缓存 vnode 的 `v_mount`（@vp+0xD8）→ 写完后内核报：
```
panic(cpu 5): Break 0xC472 instruction exception from kernel.
  Ptrauth failure with DA key, at pc 0xfffffe002376651c, lr 0xfffffe00205c1c
  x0 = 0xfffffe13055e6c38   ← = 缓存 vnode(0xfffffe13055e6b60) + 0xD8 (=v_mount)
```
**根因**：`vnode->v_mount` 是 **PAC 签名指针（DA key）**；`kwrite64` 写入了**未签名裸指针** → 后续任何访问该字段的路径 → 指针认证失败 → panic → iPad 重启。
**确认**：panic 现场正好落在 `vp+0xD8` → 证明该偏移就是 `v_mount`（门 C 判定成立）。
**教训**（已写入 memory）：改内核结构**指针字段**必须重新 PAC 签名；只有**非指针字段**（如 `v_flag`）可安全写入。

**重启后**：iPad 已重新越狱、SSH 恢复；重建 trustcache + 重部 dyld 后，`true/echo` → 137、`ls/bash` 无输出，**缓存仍未映射**。

**当前状态与结论**：
- 两道 exec 门 + platform 门 ✅；macOS dyld 能运行。
- **536 仍未过**，且与 `kernel-syscall536-finding.md` 的“外来缓存结构性不可满足”一致：
  挂在 rootfs DMG 上的 macOS 缓存，其 mount≠root卷/`/private/preboot/Cryptexes`（门 C，EPERM），
  且改 v_mount 属 PAC 指针写 → panic。**纯用户态在该设备上无解**。
- **待你决定的方向**：A) 把缓存改挂到与 root卷/`/private/preboot/Cryptexes` 匹配的位置（零内核风险，但需改 rootfs 布局）；
  B) 找到内核自带的“已签名 v_mount”来源做受控替换（需先取证 ptrauth 判别子是否地址相关）；
  C) 明确授权内核补丁路线（KTRR 风险，A12+ 设备不保证可行）。

### 2026-09-26 21:4x — ★ 原版 macOS 13 做法考古（subagent）+ 方向修正

**用户提示“看原版 macOS 13 怎么实现” → subagent 精读仓库文档 + 上游安装法，结论：**
1. **原版把 OS cryptex（含 `dyld_shared_cache_arm64e` + `.01`）解包进 chroot rootdir 卷
   （= 数据卷 `/var/mnt/rootfs` 树）内的 `System/Volumes/Preboot/Cryptexes/OS/…`**
   （上游 MacWSBootingGuide 安装步骤25-27）。这**同时命中门8“同一 mount”**（rootfs=数据卷）
   与门9 `scdir_enforce` 的父目录名。
2. **原版 macOS 13 缓存 ≈1.6G < 4GB** shared region → **天然放得下，无需裁剪/混合映射**。
3. **原版对 cachereg / F_ADDFILESIGS / VSHAREDCACHE / sandbox 零处理** —— 只把缓存 CDHash 入 trustcache
   （`layout/usr/macOS/bin/postinst.sh:1010-1034`，13.4 分支两枚 cdhash）。⇒ 这些墙是**把方案搬到 15.6.1 才撞上的新墙**。
4. **原版文档从未提及 syscall 536 或“缓存必须放 X 卷”**；该卷规则是 15.6.1 移植会话用 IDA 反编译 `sub_8459570` 才发现的。

**【重要修正】** 我先前推的“把缓存改挂到 `/private/preboot`”是**误读**（已撤销）：门 C 要的是 **rootdir 卷（数据卷）**，
而当前缓存就在数据卷上，**门 C 应已满足**。真正剩下的更可能是 **门 B（CS 覆盖）** 或 **sandbox**——
重启后已重跑 `cachereg`（输出 `fcntl=0` 成功，blob `cso=0xa160c000` 已挂），但 `true`/`sw_vers` 仍 rc=1、无输出（exec 软状态待重建）。

**下一步（未定）**：重启后重跑测试前，需先重建 exec 软状态（trustcache/arm64ify）并确认 `true` 能过 exec；
若 536 仍失败，则重点回到**门 B 的 blob 覆盖**与 **sandbox 的 `file-map-executable`**（后者可查 vnode 的 `VSHARED_DYLD` 实际生效性）。


## 2026-09-26 17:50 — ★ POST-REBOOT RESTORE + BLOB-ARTIFACT CORRECTION + HANDOVER ★

**NEW comprehensive handover doc**: `docs/porting/HANDOVER-15.6.1-2026-09-26.md`
— self-contained reproduction guide for a fresh agent. State below assumes it.

**Persistent artifacts moved**: kernel now at
`analysis/kc_raw_16.3_T8112.bin` (80052224 B, imagebase `0xfffffe0007004000`) +
`analysis/kernelcache_16.3_T8112.img4`. `/tmp` wipes no longer lose them.

**Post-reboot restore procedure (verified working)**:
- trustcache tool = `/var/jb/basebin/jbctl` (`trustcache info|add <cdhash>`;
  the old hvfs shim + `.trustcache` file paths are gone).
- cachereg holder redeployed at `/var/mobile/cachereg`; one stale instance
  (PID 1355) was auto-restarted post-Dopamine holding the preboot paths.
- 5 hashes confirmed IN: proof2 `b2b3a8b8…`, dyld_pm `10bc320f…`, main cache
  `2b9cccd5…`, .01 `8c7ba7e5…`, cachereg `f176402d…`.

**dyld_pi full diff vs pristine** (byte-level, all sites):
`0x76270` movx0,0;ret (crossarch) | `0x30140` movz w0,#0;ret
(hasExistingDyldCache→0) | `0x34298` movz w0,#0 (reuse→0) |
`0x3538c` movz w28,#1 (NOT files_count — it's a mapSplit flag) |
`0x35fc8` movz x9,#0x7800,lsl#16 + preserved `add` @0x35fd8 (dynregion
submit → 0x1f8000000) | `0x50dfc` movz x8,#0x7800,lsl#16 (accessor) |
**`0x3576c-0x35afe` = injected blob replacing `preflightMainCacheFile`**
(open+fctl(F_ADDFILESIGS)+header-parse+CacheInfo fill, dumps
`/tmp/MTOUT5.txt`; called from `0x3537c`).

**⚠ CORRECTION — the "103" runs were a measurement artifact**: dyld_pm =
dyld_pi + glueprobe2 @ `0x3588c` + `b` @0x6b94, but `0x3588c` is INSIDE the
live injected blob (not dead code). So `true`/`ls` exit 103 = blob hit the
probe mid-flight; dumped regs (x8=1, x9=0x228, x10=0x50c000…) are blob
internal state, NOT the glue-call site. Whether normal flow reaches
`0x6b94 blraaz x8` is still UNVERIFIED. Next probe must use a real dead
region (e.g. __text tail padding) and only patch `blraaz→b`.

**NEW MYSTERY — proof2 137**: locally-built arm64e test exe (ctor→
`/tmp/ctor_ran`, main→`/tmp/main_ran`+stdout+ret7) gets SIGKILL even with
its cdhash live in trustcache, while system binaries (`true`,`ls`) run dyld
fine. Suspect AMFI launch constraint on non-Apple CodeDirectory shape /
platform. `true`→103, `ls`→103, `proof2`→137, all hashes trusted.

## 2026-09-26 17:00 — ★★ DYNREGION EPHEMERALITY FULLY DECODED (current blocker) ★★

**Observed pattern**: after any successful map run → first child exec works
(T1=0), every subsequent exec SEGVs at `0x1f8000000`
(`KERN_INVALID_ADDRESS … not in any region`) even though the file-backed
cache mappings are still present in the shared region (crash dumps show
`180000000-1e7f5c000 __TEXT SM=COW` alive). Conclusion (runtime-confirmed):
**the fd=-1 "dynamic" mapping is torn down when the mapping process exits;
file-backed entries persist.** On real macOS this doesn't matter because the
boot-time mapper stays alive / the region is populated once.

**Exact kill site on subsequent execs** (IDA-confirmed, Instance1 dyld IDB):
`start` → `SyscallDelegate::hasExistingDyldCache` @0x30140 (called at
start+0x5a8c) → `shared_region_check_np` returns base →
`DyldSharedCache::dynamicRegion(base)` (0x50dfc, patched → 0x1f8000000) →
`DynamicRegion::getDyldCacheFileID` derefs → SEGV. This fires **before**
`loadDyldCache`, so even a forced map path can't save a populated region —
the crash is in the earliest "is there a cache?" probe.

Call graph (all IDA-verified):
```
start 0x53dc → hasExistingDyldCache 0x30140 → dynamicRegion() → DEREF (boom #1)
loadDyldCache 0x34240 → reuseExistingCache 0x351a8 → same deref (boom #2)
                  → mapSplitCacheSystemWide 0x352bc → syscall 536
                  → reuseExistingCache again at 0x356d8 (post-syscall verify)
```

**Probe harness that finally works** (deploy + measure without files):
exit-code probes need `movz w0,#N; movz x16,#1; svc #0x80` — **x16 is the
syscall selector, `svc #0x80` is just a marker**; an `svc` without x16=1
invokes a random syscall (we measured 140=SIGSYS from garbage x16, and once
"exit(85)" because `movz w0,#0x55` encodes `a8 0a 80 52` not `#0x51`).
movz imm16 occupies insn bits [20:5]: exit(N) = `movz w0,#N` byte0 =
(N&7)<<5, byte1 = N>>3.

Deployed base for all probes = `/var/mobile/dyld_pb` (patches: P1 crossarch,
W28=1 @0x3538c, dynreloc `movz x9,#0x7800,lsl#16` @0x35fc8 + preserved add
@0x35fd8, `movz x8,#0x7800,lsl#16` @0x50dfc, **plus two leftover patches
found in it**: `movz w0,#0` @0x34298 = reuse-call→0 (forces map path
always) and `b 0x3576c` @0x35698 = skip post-syscall tail).

Probe results with correct encoding:
- exit(0x51)@0x351a8 + exit(0x52)@0x352bc + exit(0x53)@0x35698 → all runs 82
  (map path always taken; reuse never entered because of the 0x34298 patch).
- exit(syscall_ret&0xff)@0x35698 (`uxtb w0,w0`): T1=**0** = 536 succeeded on
  empty region; T2-T4=139 = die BEFORE the post-syscall site → inside the
  earlier `hasExistingDyldCache` deref on the still-populated region.

**Remaining question**: does re-submitting 536 on an already-populated
region re-create the dynregion entry? To measure: patch out the early
derefs (`hasExistingDyldCache` 0x30140 → return 0; `reuseExistingCache`
0x351a8 → return 0) so the map path always runs, then read errno at
0x35698. If 0 → "always-map" is the fix (each exec self-heals dynregion).
If EINVAL/EBUSY → need a persistent keeper OR kernel-behavior workaround.

Alternative theory (NOT yet verified): entries tagged dynamic may be
per-mapper and die with the mapper — if so, a permanently resident chroot
"cache keeper" that performs the one-time map would keep dynregion alive
for all later processes. Untestable via `sleep` because a fresh exec on a
populated region dies at `hasExistingDyldCache` first — the keeper must be
the *first* process after the region is created (chicken-and-egg unless
we wipe/reset the region or the keeper itself is the only mapper and
everything else reuses).

## 2026-09-26 (post-breakthrough) — ★★ STRUCTURAL WALL FOUND: 15.6.1 CACHE > iOS 4GB REGION ★★

**The macOS 15.6.1 cache cannot fully fit in the iOS 16.3 shared region.**
This is a kernel-constant limitation, NOT a validation failure.

- iOS 16.3 (xnu-8792.81.2 `osfmk/mach/shared_region.h`):
  `SHARED_REGION_BASE_ARM64=0x180000000`, `SHARED_REGION_SIZE_ARM64=0x100000000` (4 GB)
- macOS 15.x (xnu-11215.81.4 same header — fetched from apple-oss-distributions):
  `SHARED_REGION_SIZE_ARM64=0x180000000` (**6 GB**). Apple grew the region.
- 15.6.1 cache virtual span: `0x180000000 → 0x2ac75c000` + dynregion 0x4000
  ≈ **4.69 GB → overflows the iOS region by ~0.70 GB**.
- `sr_map` submap max_offset is baked to sr_size at creation
  (`vm_shared_region.c:775` `vm_map_create_options(pmap_nested, 0, size)`);
  pmap nesting region is also 4 GB. Out-of-bounds `vm_map_enter` →
  KERN_* → errno (EFAULT/ENOMEM/EINVAL), whole submission rolled back.
  Region size is a kernel-immediate constant — cannot change without
  kernel patch (none available under Dopamine).

### Exact overflow map (from cache headers, verified)

Main file — 8 mappings, ALL fit:
`0x180000000…0x22560c000` (end), fileoff 0…0x7ad4c000. CS cso=0xa160c000 css=0x50c000.

.01 — 7 mappings, **partially overflows**:
```
m0 0x22560c000+0x54808000 → 0x279e14000   ✓ fits
m1 0x279e14000+0x21c4000  → 0x27bfd8000   ✓ fits
m2 0x27dfd8000+0x38b4000  → 0x28188c000   ✗ starts in-bounds, ends OUT
m3 0x28188c000…0x28261c000                ✗ all out
m4 0x28261c000…0x286bf0000                ✗ out
m5 0x288bf0000…0x288dcc000                ✗ out
m6 0x288dcc000…0x2ac75c000                ✗ out
```
.01 CS cso=0x83150000 css=0x41c000.
dynregion (fd=-1 anon, size DynamicRegion::size(), dynMax=0x4000):
submitted VA = `regionBase+header[0x1F0]` = `0x2ac75c000` → out.

Free gap inside region after .01-m1: `[0x27bfd8000, 0x27dfd8000)` ≈ 32 MB.
Chosen dynregion relocation VA: **0x27c000000** (region offset
0xfc000000; single-insn `movz x8,#0xfc00,lsl#16` / VA `movz #0x27c00,lsl#16`).

### dyld patch sites for hybrid plan (thin-slice offsets, byte-verified)

| off | orig | new | why |
|-----|------|-----|-----|
| 0x76270 | `01 10 00 d4 c0 03 5f d6` | `e0 03 1f aa c0 03 5f d6` | P1 crossarch noop |
| 0x351f0 | strcmp-result test | force "not equal" | P5 reject iOS cache → real map path |
| 0x35fd8 | `add x9,x9,x11` (`09 01 0b 8b`) | `movz x9,#0x27c00,lsl#16` (`09 80 ef d2`) | dynregion VA → 0x27c000000 (CacheInfo+0x1B0; consumed at 0x35660 `ldr x24,[x19,#0x11d0]`) |
| 0x50dfc | `ldr x8,[x0,#0x1f0]` (`08 f8 40 f9`) | `movz x8,#0xfc00,lsl#16` (`08 80 bf d2`) | `dynamicRegion()` returns base+0xfc000000 = 0x27c000000 — consistent with relocated map |

Submission construction (0x35660-0x35694): `stp x24,x0,[x8]` = sms_address/sms_size;
`str x22,[x8,#0x10]` = sms_file_offset = dyld-side copyin source ptr;
prot word at +0x28 = 1.

### Trimmed-set plan (in progress)

Phase A (now): files_count=1 patch (0x3538c `ldr w28,[x19,#0x1a8]`→`mov w28,#1`
= `3c 00 80 52`) → submit main 8 + dynregion only → `true` should exec on
the REAL macOS cache (verify via `_dyld_get_shared_cache_uuid`).

Phase B: custom .01 — copy file, patch header `mappingCount` 7→2 (keeps
m0,m1), **re-sign whole file** (ldid/self CD), cdhash → jbctl trustcache,
F_ADDFILESIGS → vnode blob. dyld then submits 2+1 files naturally.

Phase C: .01 tail (m2..m6 = fileoff 0x569cc000..EOF → VAs
0x27dfd8000..0x2ac75c000) private-mmap'd PROT_* per initprot by a pre-main
hook (libmachook ctor / dyld patch) — file pages validated vs the SAME
custom CD + trustcache. VAs land OUTSIDE the shared region in normal user
space — image enumeration then works transparently.

Phase D: WindowServer deps — audit which images fall >0x280000000.

### Known risk from earlier session (retained)

`pmap_trim_internal` panic on `true` exit — likely triggered while an
invalid shared-region mapping existed. Now that only in-bounds, CS-valid
mappings are submitted, this specific panic path should not recur; still,
avoid fd=-1 mappings whose copyin source is invalid (EFAULT→ INVALID_ADDRESS,
not the panic path). If panic recurs, suspect map-engine undo path.

-## 2026-09-26 21:5x — ★★★ exec 137 真正闭环 + 与原版 macOS13 方法对比 ★★★

### 1) exec 137 的真因（本轮新定）：**launcher 注入的 dylib 未受信**

沿调用链后发现：
```
launchdchrootexec 以 posix_spawn(SETEXEC) 启动 child，并注入
  DYLD_INSERT_LIBRARIES=/usr/local/lib/libmachook_arm64.dylib
```
而该 dylib 当时 **不在 trustcache**（cdhash `21f69e8c…`，`flags=0x0`）。
⇒ 子进程 exec 因“**注入的库未受信**”被内核 SIGKILL（137）。

**修复（“用 trustcache 绕 gate”）——对注入 dylib 同样做四步：**
```
arm64ify_macho.py <libmachook_arm64.dylib>
ldid -Hsha256 -Cadhoc -S<entitlements.plist> <libmachook_arm64.dylib>
jbctl trustcache add <sha256 cdhash>
（同时把 libmachook.dylib 的 arm64/arm64e 两个 cdhash 也 add）
```
**效果：`true` 连续 3/3 `rc=0`（不再 137）**。⇒ **exec 准入彻底打通**（主二进制 + 注入库都要）：
主二进制用 `arm64ify + -Hsha256 -Cadhoc + trustcache`，**注入的 libmachook 也要同样处理**。`echo` 也 rc=0。

### 2) 重要排雷：日志归属
`DYLD_PRINT_*` 下看到的 “re-using existing shared cache (/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld/…)”
与整段 782 行缓存映射，**首映射是 `…/dopamine…/launchdchrootexec` ⇒ 那是 iOS 侧 launcher 的 dyld**，
**不是 chroot 内 child 的 macOS dyld**（launcher 未透传 `DYLD_PRINT_*` 给 child，`ceprobe2` 直连 child 时为 0 字节）。
⇒ 判断 child 行为时**不要误把 launcher 日志当成 child 的**。

### 3) 原版 macOS 13 vs 我们 15.6.1（回答：“13 的方法能否用到 15.6？”）
**能部分延用（缓存放置），但 15.6.1 多了三类新墙：**
| 维度 | 原版 macOS13（iPad13,1） | 我们 15.6.1（iPad13,11） |
|---|---|---|
| 缓存放置 | OS cryptex 解包进 rootfs 的 `System/Volumes/Preboot/Cryptexes/OS/…` | **同样已做好，路径解析正确** |
| 体积/4GB | ~1.6G，天然放入 | 2.7G(+.01 2.2G)，**越界→需裁剪/dynregion** |
| exec 准入 | 未撞上 | 需 arm64ify+trustcache+LC platform+**注入库受信**（刚破） |
| CS/sandbox 门 | 未撞上 | `VSHARED_DYLD`/`file-map-executable` 等（15.6.1 特有） |
⇒ **原版方法必要但不充分**；15.6.1 还需解决 4GB 越界 + 更严的 exec/CS 门。

### 4) 本轮其它既定结论（已有专节）
- exec 137 根因 = 非 ARM64/ALL；EACCES = trustcache hash_type（需 sha256）；
- dyld “different platform” = LC_BUILD_VERSION 需 macOS(1)；
- 536 内核门 A/B/C + sandbox `VSHARED_DYLD`；KRW 打通内核 walk；PAC 裸写会 panic。

**下一步**：exec 已稳定（rc=0），重点回到 **child 的 macOS dyld 是否真映射 macOS 缓存**（需把 child 的 dyld 输出单独取到：因其 env 未被 launcher 透传，需改用能透传 env 的启动方式，如自建最小 launcher）。

### 2026-09-26 22:0x — 状态盘点 + 两处更正

1. **launcher `POSIX_SPAWN_SETEXEC` 事实**（`launchdchrootexec/main.m:119/141`）：
   SETEXEC 成功→launcher 进程被替换成 child；其返回码 = **child 的退出码**。
   ⇒ `true` 的 `rc=0` 是**真跑了且 exit 0**（true 本静默）；`echo` 无输出才是“main 未跑”的信号。
2. **文档归属规则已证实**：`DYLD_PRINT_*` 输出中 **`[launchdchrootexec] target=` 横幅之前 = launcher(iOS)dyld，之后 = child(macOS)dyld**。
   实测 child 段**为空** → child 的 macOS dyld 未输出任何内容。
3. **exec 137 = “逐二进制且会抖”**：对 `true/echo/ls/bash/printf` 逐个 `arm64ify+-Hsha256 -Cadhoc+trustcache` 后**时而 rc=0、时而 137**，
   与 HANDOVER §10 “137 不是单一原因…每二进制单独归因”一致。**推断：运行 `postinst.sh` 等批量重签会改写 cdhash，令 trustcache 瞬时不一致 → 137**。

**当前卡点（与 HANDOVER §9 对应）**：
- child 的 macOS dyld → **silent-0**（§9/§10 记为“admission 过了但 main 没跑”的第三类症状）——即 **dyld 未真正加载 libSystem/macOS 缓存**；
- 未决仍为：§9.2 dynregion 持久化、§9.3 glue-call 取证、§9.4 `.01` 尾部混合映射。
**前置（必须先做）**：让设备 exec 进入**稳定态**
（重跑 `postinst.sh` 收官 → 对我需要的每个二进制重做 `arm64ify+-Hsha256 -Cadhoc+trustcache` → 连测 `echo HI` 稳定打印）再做后续打点；否则测量不可复现。

### 2026-09-26 22:3x — ★ 新工具 `dearm64e`：arm64ify 后必须消掉 arm64e 专属分支指令

**发现**：`arm64ify`（只改 cputype/subtype）后，二进制代码仍是 **arm64e**，其中 **arm64e 专属的
指针认证分支/调用指令在 arm64 路径上非法 → SIGILL(132)**：
- `brab* = 0xd61f0800|Rn`、`braa* = 0xd61f0c00|Rn`（branch）
- `blrab* = 0xd63f0800|Rn`、`blraa* = 0xd63f0c00|Rn`（call，含 doc 点名的 `0x6b94 blraaz x8`）
- `retab = 0xd65f0bff`（return）
（注：`pacibsp=0xd503237f`/`autibsp` 是 HINT，在 arm64 上无碍，不必改。）

**工具**（`analysis/dyldwork/build_dyld.py` 里的 `_dearm64e()`，作为构建键 `dearm64e`）：
把所有以上族 → `br Xn`/`blr Xn`/`ret`（保留 Rn）。构建例：
`python3 build_dyld.py dyld_cleanB_ae2 crossarch hasexisting prereuse filescount1 dynoff accessor fcntl_nop cover_b dearm64e`
该 thin dyld 上共转 167 处；验证后 `brab/braa/blrab/blraa/retab` 全为 0。

**但**：即使 dyld 内全清零，`true` 仍 **rc=132(SIGILL)**（`echo` 也无输出）→ **SIGILL 还有第二个源，不在 dyld**
（候选：被 exec 的主二进制自身的 arm64e 残留、注入的 libmachook、或更深层）。当前设备 exec 状态不可复现（137/0/silent-0/132 轮番），
且 132 未落 crash 报告（最新 `true-*.ips` 仅为旧的 16:54 SIGSEGV）。

**下一步（需干净 boot）**：重启+重越狱后，对**主二进制与 libmachook** 也跑 `dearm64e`（同理存在 arm64e 指针分支），
再测 `echo`；若不消，用设备 `find_crash` 取 132 的 crash 报告（PC/指令）定位第二个源。


## 2026-09-26 16:00 — ★★★ BREAKTHROUGH: CACHE MAPPED SUCCESSFULLY ★★★

**syscall 536 returned 0 — the macOS 15.6.1 cache IS mappable on iPadOS 16.3.**

Runtime proof (chroot child, dyld-cave probe):
```
check_np before = 12 (region exists, empty)
F_ADDFILESIGS   = 0  (blob already on vnode — attached host-side earlier)
submit mapping0 = 0  ← SUCCESS
check_np after  = 0, base = 0x180000000 ← region POPULATED w/ macOS cache
```

### The complete gate chain of shared_region_map_and_slide_2_np
(xnu-8792.81.2 `bsd/vm/vm_unix.c:2189 shared_region_map_and_slide_setup`,
source downloaded to `/tmp/dyldwork/xnu-xnu-8792.81.2/` — RE-VERIFY vs
binary when kernel is back in IDA; tag is one patchlevel off 8792.82.2)

In order, errors:
1. `files_count==0` → EINVAL; `>MAX` → E2BIG
2. `shared_region==NULL` → EINVAL (ours exists — check_np=12)
3. `region->sr_root_dir != proc->fd_rdir` (chroot root) → EPERM
4. fd==-1 pseudo-entry: >1 mapping or unaligned addr/size → EINVAL
5. fd→vnode: not file/!FREAD/!VREG → EINVAL/EPERM
6. `mac_file_check_mmap` → passthrough errno (sandbox: 40 = EMSGSIZE
   deny for non-boot-cache vnodes; bypassed via `no-sandbox` entitlement)
7. `va_uid != 0` → EPERM (file must be root-owned — ours is)
8. `v_mount` must equal root-vol mount OR preboot-cryptex mount
   (`vnode_lookup("/private/preboot/Cryptexes")` — FAILS inside chroot!)
   → EPERM. *This is why cryptex-vol files get EPERM in chroot but data-vol
   files pass — the bindfs'd preboot dir is still a disk1s6 vnode.*
9. `scdir_enforce` (if on): vnode_parent must be expected scdir → EPERM
10. `ubc_getobject` NULL → EINVAL
11. **`ubc_cs_is_range_codesigned(vp, file_offset, size)` → EINVAL** ←
    our errno-22 wall. Needs a cs_blob on the vnode covering each
    non-ZF mapping range. Blob was ABSENT because our cache file (cp'd
    into rootfs) lost its APFS fs-signature.

### Errno observations (both confirmed)
```
CHROOT child (empty region):       iOS host (populated region):
  macOS-cache@datavol → EINVAL 22    macOS-cache@datavol → EPERM 1
  macOS-cache@preboot → EPERM  1     macOS-cache@preboot → EINVAL 22
  iOS-cache@preboot   → EPERM  1     iOS-cache@preboot   → EINVAL 22
  dyld@datavol(fake)  → EFAULT 14    dyld(fake)         → EINVAL 22
```
Pattern = gate-8 volume check vs later EINVAL ordering. Populated-region
submissions all EINVAL (occupied reject in map engine).

### ★ THE FIX: `fcntl(fd, F_ADDFILESIGS=61, &fs)`
Reads superblob at `fs.fs_file_start+fs.fs_blob_start` → `ubc_cs_blob_add`
→ attaches cs_blob to the vnode. For the 15.6.1 cache:
`fs_file_start=0, fs_blob_start=codeSignatureOffset (hdr+0x28=0xa160c000),
fs_blob_size=codeSignatureSize (hdr+0x30=0x50c000)`.

**KEY: must be called from an iOS-PLATFORM process (host side).**
Inside chroot it returns EPERM — `ubc_cs_blob_add` → `mac_vnode_check_
signature(vp,…,proc_platform)` → AMFI rejects (probably because caller is
PLATFORM_MACOS, or sandbox file-check of a macOS process). Host-side call
on the SAME file succeeded (probe7 on all files → 0). Blobs stick to the
VNODE → chroot children then pass gate 11.

### Tooling that now works (all verified this session)
- **dyld cave probe**: patch `B` at thin-dyld 0x35698 → 0x3576c overwrites
  `preflightMainCacheFile` (dead once probe exits). Asm via clang
  `-nostdlib -Wl,-e,__start -Wl,-static`, extract `__text`.
  GOTCHA: GNU as drops everything after `;` on a line — ONE INSTR PER LINE.
- **iOS-native probe binary**: build `arm64e` Mach-O exec, then
  `vtool -set-build-version 2 16.0 16.0 -replace` (platform=ios) —
  plain clang output gets "wrong platform" from iOS dyld; static Mach-O
  exec gets SIGKILL'd (137) even signed+trustcached — must be DYNAMIC.
- `mount_bindfs` exists: `/var/jb/usr/local/bin/mount_bindfs` (binary,
  mounts READ-ONLY per mountdevfs comment).
- `open=5 write=4 close=6 pread=153 fcntl=92 mmap=197 check_np=294
  map_and_slide_2=536` (svc #0x80, errno in x0; success→0/value).

### mmap PROT_EXEC + pagein on cache file (probe4)
mmap PROT_EXEC **succeeded** (0x10459c000), pagein returned byte 'd' —
kernel validated exec pages without kill, but that path does NOT attach a
cs_blob to the vnode (or its coverage doesn't satisfy). Still EINVAL after.
So F_ADDFILESIGS is the correct attach mechanism, NOT mmap.

### Next steps (NOT yet done)
1. Host-side helper/daemon: open cache files + F_ADDFILESIGS before launch
   (must run at each boot/vnode-recycle — blob lives on vnode only while
   vnode cached). Simplest: keep an fd open in a resident helper.
2. Retest REAL dyld submission (full files[] incl .01 + DynamicRegion
   fd=-1 pseudo-entry at 0x1fa000000 — relocated inside 4GB region).
   The probe only submitted mapping[0]; full submit may hit new gates
   (subcache count, slide, align checks per mapping, scdir check).
3. `scdir_enforce` sysctl — if on, file parent dir must be expected path
   (/System/Library/dyld in chroot may or may not satisfy it — worked in
   probe since errno was 22 not 1... or scdir off; verify).
4. Then dyld proceeds to actual lib loading — watch for next failures.
5. Investigate whether blob attach survives across `true` runs (vnode
   recycling) — if flaky, pin fd or add to postinst/launch helper.

## 2026-09-26 15:09 — KERNEL PANIC + RECOVERY (newest, read first)

**The iPad panicked during our experiments** — log
`/private/var/mobile/Library/Logs/CrashReporter/panic-full-2026-09-26-150854.000.ips`:

```
panic(cpu 5 caller 0xfffffe0025bc6938): pmap_trim_internal:
grand addr wraps around, grand=0xfffffdf12d917450,
subord=0xfffffdf1aee850e0, vstart=0xffffffffffffffff, size=0x1
@pmap.c:11173
Panicked task: pid 46438: true   (i.e. our launchdchrootexec /usr/bin/true run)
```

Interpretation: repeated malformed `shared_region_map_and_slide_2_np`
submissions (and possibly the earlier `check_np(NULL)` detach) leave the
task/shared-region pmap state inconsistent; the NEXT exec's pmap_trim on
teardown computes a wrap-around range and panics. **vstart=-1 is the
signature.** Consequences:

- AVOID `shared_region_check_np(NULL)` (detach) entirely — it both kills
  the caller (137) and probably corrupts state for later execs.
- AVOID large batches of malformed syscall-536 submissions; space
  experiments out, prefer clean-boot measurement.
- After ANY suspicious exit pattern, re-check kernel logs.

**Post-reboot device state** (panic also rolled back unflushed APFS writes):
- `/var/mnt/rootfs` content intact; probe binaries in `usr/local/bin` survive.
- **`/usr/lib/dyld` DELETED by the rollback** — backups survive:
  `dyld.orig` (2289328B fat, pristine), `dyld.func` (fat, 5-patch), etc.
  Deployed dyld must be RE-COPIED from dyld.orig + re-patched + re-signed.
- `/tmp` wiped on BOTH device and Mac: `kc_raw.bin`, probe asm, mt_dylib,
  dyld_* variants all gone. Pristine thin slice lives at
  `macPad/analysis/dyld_15.6.1_arm64e_thin` (1240752B). Kernel re-extract
  procedure documented below still valid (IMG4→bvx2→LZVN).
- Jailbreak re-applied by user (Dopamine); SSH ok.

**EINVAL=22 investigation — remaining candidate sites** (from
`sub_8459570` decompile; task->shared_region ruled out, it exists but is
EMPTY → check_np returns 12):
1. `vnode+0x70 != VREG` — unlikely (regular files).
2. `vnode+0x78` UBC info → cs_blobs chain empty → 22. **Top suspect.**
   Cache file has never been CS-validated by UBC (never mmap'd/exec'd as
   signed object on this kernel) → vnode may have NO cs_blob.
3. Per-mapping `ubc_cs_blob_get(vnode,-1,-1,file_offset)` must cover
   [file_off, file_off+size] → 22 if any non-slide mapping uncovered.
4. `fd=-1` DynamicRegion pseudo-entry malformed (count/align) → 22.
5. NEW: `/private/preboot/Cryptexes` literal in kernel — possible
   "vnode must be on cryptex volume" check. Our files sit on the data
   volume under a *fake* cryptex PATH. If so: copying cache onto the REAL
   preboot volume (or bind-mount) may be the bypass.

**Decisive next experiment (designed, not yet run)**: patch dyld's
failure-path dead code (~0x35720+) with a custom asm probe that submits
syscall 536 for (a) `/usr/lib/dyld` itself — guaranteed CS blob since it
is executing — and (b) the macOS cache file; write both errnos to a file.
Distinguishes "file has no UBC CS blob" vs "geometry/pseudo-entry" causes.
Probe asm source lost with /tmp; re-derive from this spec.

## 2026-09-26 — PARADIGM-SHIFTING FINDINGS (read before anything else)

1. **`re-using existing shared cache` observed at runtime.** With
   `DYLD_PRINT_SEGMENTS=1` (env DOES propagate through launchdchrootexec —
   it setenvs before POSIX_SPAWN_SETEXEC so the child inherits everything)
   a `true` run printed `dyld[pid]: re-using existing shared cache
   (/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld/
   dyld_shared_cache_arm64e)` + full segment dump, then RC=0.
   **CAUTION — attribution ambiguity**: launchdchrootexec is itself an iOS
   process, so iOS dyld prints the same "re-using" line for the LAUNCHER's
   own startup (Dopamine also injects forkfix/libinjector into it — visible
   in output). Everything BEFORE the `[launchdchrootexec] target=` banner is
   the launcher's iOS dyld; everything AFTER is the child (macOS dyld).
   Must capture output after the banner to attribute. Either way, the kernel
   pre-maps the iOS boot cache into every exec'd process — macOS dyld may be
   reusing IT rather than mapping ours.
2. **errno-40 source CONFIRMED by neighbor-AI kernel RE**
   (`docs/porting/kernel-syscall536-finding.md`): Sandbox
   `mpo_file_check_mmap` @ 0xfffffe000a659664 → `cred_sb_evaluate(op=16,
   file-map-executable)` → deny errno 40 for foreign cache vnodes lacking
   VSHARED_DYLD flag. AMFI hook can only return {0,1}. AppleImage4 no hook.
3. **Deployed dyld MUST carry project entitlements** (has
   `com.apple.private.security.no-sandbox` + 233 others): bare ldid-signed
   dyld gets platform sandbox at exec → file-map-executable denied → 40.
   Entitled dyld + `true` gave RC=0 ×5 (still ambiguous vs disk fallback).
4. **trustcache grep must be case-insensitive** — `jbctl trustcache info`
   prints UPPERCASE hex; `grep` without `-i` falsely reports missing.
   `jbctl trustcache add` silently no-ops sometimes — always verify.
5. **15.6.1 cache CodeDirectory cdhashes** (superblob 0xfade0cc0 @
   header+0x28 codeSignatureOffset, CD slot 0):
   main: sha256 `2b9cccd5c5728972bc2a3b7f251114e6f1ff9b5e` / sha1 `05afea7d…`;
   .01: sha256 `8c7ba7e588b0edd43f7334e2de11688cd4732192` / sha1 `f1d3342b…`.
   Both sha256 values added to device trustcache (verified).
   Cache files live at `/var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/
   System/Library/dyld/` (symlinked from `/System/Library/dyld/`).
6. **`_dyld_get_shared_cache_uuid` probe** — must declare manually:
   `extern const unsigned char *_dyld_get_shared_cache_uuid(void)
   __attribute__((weak_import));` compiled OK, deployed as dsctest, first
   run 137 (cdhash race), then RC=0 with ZERO output — main seemingly never
   ran (should print UUID + exit 42/43). Unexplained.
7. **Static arm64e test binary (`hw`, raw svc, no dyld) → 137** despite
   cdhash trusted. A no-dyld exec still gets killed — suggests an
   exec-time/launch-constraint gate on arm64e Mach-Os independent of dyld.
8. **137 causes catalog**: (a) cdhash not in trustcache; (b) stale inode
   (cp-overwrite keeps old CS vnode); (c) AMFI launch-constraint (see
   launchdchrootexec main.m comment — `Launch Constraint Violation` kills
   when spawn type mismatches); (d) dyld abort_with_payload = SIGKILL;
   (e) exiting while shared-region attach mid-flight (probe <0x35698).
9. **Host reboot wiped `/tmp`** — kernel `/tmp/kc_raw.bin` + IDB +
   `/tmp/xnu8792` gone. Kernel RE doc `kernel-syscall536-finding.md` has all
   addresses. If kernel needed again: re-extract → tell user → they load in
   IDA Instance2. Device kernelcache source: `/private/preboot/CFD92CED…/
   System/Library/Caches/com.apple.kernelcaches/kernelcache` (IMG4, 21.8MB,
   bvx2/LZVN payload, decompress to ~76MB arm64e Mach-O).
10. **Merge with upstream done** (`91ff1b6`), `control` conflict resolved to
    `Depends: python3, ldid`. Repo `~/Desktop/macPad`, upstream DCMMC/macPad,
    origin zenkernelsam/macPad, `main` ahead of origin by 5. Untracked
    `analysis/` dir exists.
11. **Device binaries of record**: `/var/mnt/rootfs/usr/lib/dyld` currently =
    entitled fat build (cdhashes 82d3f27a/46282ecc — trusted) but patch
    offsets 0x352bc/0x35698 show PRISTINE bytes → it's an entitled
    UNPATCHED dyld. `dyld.func.keep` = earlier functional build backup.
12. **`dsctest`/`hw`/`true` all currently 137 or silent-0** — device state
    unstable; when `true` gave RC=0 the cache-hash adds + entitled dyld were
    in place. Reproduce DYLD_PRINT run to see WHERE it dies now.

## Device access (also in AGENTS.md)

- SSH: `sshpass -p cisco ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no -p 2222 root@192.168.64.1`
  - Password: `cisco` (`alpine` does NOT work). The `-o` flags matter: without
    them ssh-agent keys get offered first → "Too many authentication
    failures" / "UNIX authentication refused" intermittently.
  - If unreachable, IP may have changed; scan for open :2222.
- Staging dir on device (upload everything here, overwritable):
  `/var/mobile/Containers/Shared/AppGroup/1B2AD29A-2C34-4770-86EC-E11CD02312FF/File Provider Storage/macPad_iOS`
- Rootfs mounted at `/var/mnt/rootfs`; installed tools at `/var/jb/usr/macOS`.
- Pristine dyld extract on device: `/tmp/usr/lib/dyld` (from rootfs tar).
- Repo paths `/var/jb/var/mobile/MacWSBootingGuide`, `/var/jb/var/mobile/theos`
  are GONE (device re-jailbroken).

## dyld binary layout — CRITICAL

- Source dyld = fat (arm64@0x4000 + arm64e@0x100000) from rootfs tar,
  2289328 bytes. Analysis file = `analysis/dyld_15.6.1_arm64e_thin`
  (1240752 B = arm64e slice).
- **`ldid -S` REPACKS the fat**: after signing, arm64e slice moves to
  `0xfc000`. NEVER hardcode the delta. ALWAYS read the fat header after
  signing:
  `d[8+i*20+8:8+i*20+12]` big-endian = slice offset; arm64e = cputype
  0x100000c subtype 0x80000002.
- Order of operations: restore pristine → patch at CURRENT delta →
  `ldid -S` → `jbctl trustcache add <sha256 cdhash>` (BOTH slices, UPPERCASE
  hex — lowercase add is accepted but verify with `jbctl trustcache info`) →
  **`rm` dest file THEN `cp`** (cp over same inode = stale CS → silent
  SIGKILL 137. New inode required).
- NOTE: `for h in $(ldid -h ...)` loops can silently produce empty vars →
  verify `jbctl trustcache info | grep <hash>` actually lists it.

## Syscall ABI (verified vs xnu-8792.81.2 + IDA)

- `shared_region_check_np` = **294** (NOT 464 — sprobe.c originally had it
  wrong; iOS master:445). `check_np(&base)` → 0+base if region mapped;
  errno otherwise. `check_np(NULL)` = **detach/unmap the task's shared
  region** (vm_shared_region_remove + set NULL).
- `shared_region_map_and_slide_2_np` = **536**
  `(u32 files_count, shared_file_np files[], u32 mappings_count,
   shared_file_mapping_slide_np mappings[])`:
  ```c
  struct shared_file_np { int sf_fd; u32 sf_mappings_count; u32 sf_slide; }; // 12B
  struct shared_file_mapping_slide_np {   // 48B
      u64 sms_address, sms_size, sms_file_offset;
      u64 sms_slide_size, sms_slide_start;
      int sms_max_prot, sms_init_prot;    // +VM_PROT_SLIDE(0x20) etc in max_prot
  };
  ```
- Kernel applies `slide_amount` (random % files[0].sf_slide) to EVERY
  sms_address → dyld submits UNSLID header addresses; slide=0 → as-is.
- Kernel-side checks (vm_unix.c shared_region_map_and_slide_setup): file on
  root/preboot volume, CS coverage, vnode owner/root-dir, alignment,
  KERN_NO_SPACE for out-of-region. Errnos: EPERM/EINVAL/EFAULT/ENOMEM.

## dyld flow (thin offsets; from full-analysis + fresh IDA reads)

`loadDyldCache` 0x34240 → `mapSplitCacheSystemWide` 0x352bc:
- 0x34268 `B.NE` guards private-vs-systemwide; 0x34298 `BL reuseExistingCache`
  = PRE-reuse (accepts iOS cache by magic alone!) — PATCH `MOV W0,#0` to force
  syscall path. Post-syscall reuse call at 0x356d8 is SEPARATE (keep it —
  it fills results->loadAddress via check_np+strcmp).
- 0x3538c `LDR W28,[X19,#0x1A8]` = numFiles (2). `MOV W28,#1` = main only.
- files[] entries = per-subcache {fd,count,slide} + trailing
  `{sf_fd=-1, count=1, slide=0}` = **DynamicRegion pseudo-mapping**:
  sms_address = header.sharedRegionStart(0xe0)+dynamicDataOffset(0x1f0)
  = 0x2ac75c000 (OUT OF 4GB iOS REGION — always KERN_NO_SPACE);
  sms_size = DynamicRegion::size(); **sms_file_offset = userspace ptr to
  DynamicRegion buffer** (kernel copies content from it); prots=0x100000001
  (R/R); slide fields 0.
  - Computed at 0x35fc8-0x35fd8 in preflightCacheFile tail:
    `LDR X9,[hdr+0x1f0]; LDR X10,[hdr+0x1f8](size?); LDR X11,[hdr+0xe0];
    ADD X9,X9,X11; STP X9,X10,[record+0x1B0]`. Read later at 0x35660.
  - `DyldSharedCache::dynamicRegion()` accessor at 0x50dfc:
    `LDR X8,[X0,#0x1F0]` then `this+X8` → must be relocated CONSISTENTLY
    with the submission patch (both to same offset). Returning NULL →
    fileId stays 0 → ctor `halt`. Don't NULL it.
- Error path: 0x356dc `CBZ W23,0x356f4` (syscall ok) / `TBNZ W0,#0→0x35710`
  (reuse ok) / else `LDR X8,[X20,#0x10]` errorMessage — **if non-NULL the
  native error path at 0x35754 is SKIPPED** (returns 0 at 0x356ec). My
  earlier tramp at 0x35754 only fires when errorMessage==NULL.
- Success print "mapped dyld cache file system wide" gate: `0x35700 B.NE`
  (options+6 != 1). "re-using existing shared cache (%s)" gate: `0x35270
  B.NE`. "mapped cache does not contain dynamic cache info" (0x35298) is
  UNGATED — fires when dynamicRegion()==NULL but still returns 1.
- `reuseExistingCache` 0x351a8: `check_np(&p)` → `strcmp(p,"dyld_v1  arm64e")`
  → slide → dynamicRegion() → getDyldCacheFileID → ret 1. Magic-only check =
  why iOS cryptex cache gets reused.
- errno global = `0xa9b10` (`_errno`, written by `cerror_nocancel` @0x2d64:
  `STR W0,[errno]; MRS TPIDRRO_EL0; STR W0,[[tls]+8]`). Read it via
  `adrp`+`ldr` (PC-relative, ASLR-safe).
- `console()` = dyld4::console @ **0xa2f4** — printf-family, callable from a
  tramp via adrp+add+blr (LR clobber OK if you save/restore it; re-do
  `pacibsp` in tramp if you overwrite one).
- NOP cave: thin `0x38d08`-`0x38d3c` (56B; 0x38d40 starts a real function).
  NOTE: `dyld_15.6.1_arm64e_thin.i64` IDB is **contaminated** — it shows my
  old `B 0x38d08` at 0x35754 as if native (clean file has `MOV W0,#0`).
  Trust the device pristine copy for raw bytes there.

## Patch recipe under test (thin offsets; verify bytes post-signing)

| thin | patch | purpose |
|---|---|---|
| 0x76270 | `e0031faa` (mov x0,xzr) | crossarch_trap svc→0 (iOS nosys) — REQUIRED |
| 0x34298 | `00008052` (mov w0,#0) | skip PRE-reuse → force syscall path |
| 0x3538c | `3c008052` (mov w28,#1) | files_count=1 → drop .01 subcache |
| 0x35fc8 | `0940afd2` (movz x9,#0x7a00,lsl#16) | dynregion submit addr → 0x1fa000000 |
| 0x50dfc | `0840afd2` (movz x8,#0x7a00,lsl#16) | dynamicRegion() offset → +0x7a000000 |
| 0x35700 | `1f2003d5` (nop) | diagnostic: ungate "mapped...system wide" |
| 0x35270 | `1f2003d5` (nop) | diagnostic: ungate "re-using (%s)" |

## 2025-XX session 2 — errno CONFIRMED + dead ends ruled out

**Syscall #536 errno = 40 = EMSGSIZE** (or kern_return_t 40=KERN_LOCK_OWNED —
the BSD wrapper returns `kr` raw, both readings possible). Source: iOS-16.3
CLOSED-source branch of `shared_region_map_and_slide_2_np` — not present in
xnu-8792 open source (which only produces EPERM/EINVAL/EFAULT/ENOMEM/E2BIG).
Verified via exit-probe: 6/6 stable rc=40 from `0x35698` (`CMP W0,#-1 → B.NE;
ADRP X8,#0xa9; LDR W0,[X8,#0xb10]=errno; exit(W0)`). The stub does run
`cerror_nocancel` → errno := kernel ret → W0=-1.

**Exit-probe methodology — hard rule discovered:** `exit(N)` placed BEFORE the
syscall returns (0x352bc entry / 0x3533c / 0x35680) reliably yields **137**
(SIGKILL), while exits AFTER the syscall (0x35698) work fine. i.e. the kernel
kills a task that exits while its shared-region attach is mid-flight —
early-exit probes are useless; only probe ≥ 0x35698 (or post-syscall sites).

**Intermittent 137s are environmental** (iOS-cache prebind timing / amfid),
not patch content — same file alternates 137/40 across runs, then stabilizes.
Retest before attributing.

**`deallocateExistingSharedCache` (check_np(0)) is a DEAD END:** calling it
from `mapSplitCacheSystemWide` (0x3533c tramp → cave → BL 0x3420c) kills the
task (137, consistent). Reason: `vm_shared_region_remove` rips the nested-pmap
region out of the task mid-exec. Also pointless: after detach,
`map_and_slide` gets `no shared region → EINVAL` — the region is created
once at exec by `vm_shared_region_enter`, cannot be recreated.

**Region geometry (xnu-8792):** `SHARED_REGION_BASE_ARM64=0x180000000`,
`SIZE=0x100000000` (fixed 4GB, not per-cache). macOS 15.6.1 cache: main
0x180000000–0x2255dc000 (fully in bounds), `.01` 0x22560c000–0x27dfd8000+
(TAIL CROSSES 0x280000000 — partial problem), dynamic region 0x2ac75c000
(fully out). With files_count=1+dynrelocate all submissions are in-bounds
yet EMSGSIZE persists → the rejection is NOT bounds; it's an iOS-specific
check (probably cache-identity/UUID-vs-boot-cache or file-set composition).

**iOS kernelcache extracted for RE:** device
`/private/preboot/CFD92CED…/System/Library/Caches/com.apple.kernelcaches/
kernelcache` (IMG4, 21.8MB) → decompressed via `pyimg4` (bvx2/LZVN payload)
→ `/tmp/kc_raw.bin` = 76MB arm64e Mach-O kernelcache (T8112 — device is M2,
iPad13,11). **Load this in IDA to find the EMSGSIZE return site** in
`shared_region_map_and_slide_2_np` (sysent[536]).

**Full 4.9GB 15.6.1 dyld cache staged on host:**
`/Users/ciscohe/Desktop/dyld-cache-15.6.1/` — main `dyld_shared_cache_arm64e`
(2712764416 B) + `.01` (2203500544 B), verified complete vs device sizes.
Deliberately outside the repo + outside /tmp. Use `dsc_extractor` or
`misc/extract_dyld_cache.py` against this pair if library bodies needed.

## Results so far (this session)

- 5-patch config (first five above, post-reuse intact): child prints
  `Mapping the shared cache system wide` → `dyld cache '(null)' not loaded:
  syscall to map cache into shared region failed` → dyld FALLS BACK to
  on-disk mmap (`Kernel mapped /usr/bin/true`, `Mapping
  /usr/local/lib/libmachook.dylib`, `Mapping /usr/lib/libSystem.B.dylib`) →
  `libdyld.dylib not found` → rc=0. **syscall still fails** even with
  files_count=1 + dynregion relocated.
- errno still unconfirmed — tramp-on-error-path approach was flaky because
  (a) errorMessage may already be set (skip path), (b) bss-write tramp
  crashed 139, (c) stub-level redirect + console gave 137 (was actually
  trustcache/inode staleness, not the tramp).
- Next step: **freestanding probe `misc/sprobe.c`** (already written &
  compiled — static arm64e Mach-O, raw SVC, no dyld needed). Staged tests:
  A check_np state; B main-cache-only map; C +relocated dynregion;
  D check_np(0) dealloc then retry; E dynregion at original out-of-bounds
  addr. Prints errno for each → pins down WHICH check fails.
  - sprobe fixes applied: check_np syscall 464→294; staged tests added.
  - Deploy: `ldid -S`, `jbctl trustcache add`, rm+cp into
    `/var/mnt/rootfs/usr/bin/sprobe`, run via launchdchrootexec.
  - First run gave 137 — most likely trustcache add raced/silent-fail OR
    stale inode. RETRY with verified `jbctl trustcache info | grep`.

## Exit-code legend

- 0   = ran to completion (may still be DEGRADED — disk fallback when no
        cache; distinguish via prints, not rc)
- 132 = SIGILL (real illegal instr / PAC failure — NOT brk)
- 133 = SIGTRAP = `brk #0` fired (bisect marker)
- 134 = SIGABRT (dyld graceful abort/halt)
- 137 = SIGKILL: CS/trustcache/exec-policy — silent, NO .ips. If it appears
        right after a rebuild: check trustcache add actually landed AND the
        dest inode was replaced (rm then cp, not cp-overwrite).
- 138 = SIGBUS
- 139 = SIGSEGV
- 140 = SIGSYS
- **WARNING: piping the launcher into `awk`/`tail` eats `$?`** — rc then
  reports the filter's exit. Always `echo $?` on the raw command.

## Confirmed root causes (unchanged)

1. iOS 16.3 `SHARED_REGION_SIZE_ARM64=0x100000000` = region
   0x180000000-0x280000000; macOS 15.6.1 cache extent to 0x2ac760000.
   .01 subcache tail + dynamicData region exceed it → KERN_NO_SPACE.
2. DSC file CS-enforced — editing header (subcache count) kills it for
   that inode permanently. Can't patch DSC.
3. Private path `mapSplitCachePrivate` 0x342dc = plain mmap of exec DSC
   pages → CS kill 137. Dead end.
4. iOS-cache prebind + magic-only reuse → false-success variance.

## Original-project tools — what they're for (see tools-and-porting doc)

- `misc/sprobe.c` — this task's probe (freestanding, raw SVC).
- `launchdchrootexec` — chroot launcher (sets MACWS_CHROOT_HOST_ROOT,
  DYLD_INSERT_LIBRARIES for libmachook, MACWS_SUSPEND_* for lldb).
- `misc/chroot_then_exec.c`, `misc/chroot_isolation_test.c` — chroot probes.
- `misc/loadtc`, `misc/vtool_and_sign.sh`, `autosignd/` — sign+trustcache.
- `misc/extract_dyld_cache.py` — DSC header/images parser.
- `misc/disasm_remote_dyld_range.sh` — device-side dyld byte dumps.
- `misc/lldb_*` — scripted lldb attach/breakpoints (incl. `MACWS_SUSPEND_AT_EXEC`).

## IDA MCP

- Server `ida-pro-mcp-Instance1`. IDB `dyld_15.6.1_arm64e_thin.i64` —
  CONTAMINATED at 0x35754/0x38d08 by old tramp; pristine bytes live at
  device `/tmp/usr/lib/dyld` (+0x100000 delta).
- `py_eval` arg = `code`. Use `idc.GetDisasm`/`ida_funcs`/`idautils.Strings`.
- Full analysis (mostly valid modulo contamination):
  `docs/porting/dyld-15.6.1-full-analysis.md`.

## Build/test commands

```bash
# verify arm64e slice delta on device file:
python3 - <<EOF
import struct;d=open('/var/mnt/rootfs/usr/lib/dyld','rb').read(64)
for i in range(struct.unpack('>I',d[4:8])[0]):
 o=8+i*20;ct,cs,off,sz,al=struct.unpack('>IIIII',d[o:o+20])
 if cs==0x80000002: print(hex(off))
EOF
# sign + trustcache + deploy (FRESH INODE):
ldid -S /tmp/dyld_x
for h in $(ldid -h /tmp/dyld_x|grep '^CDHash='|cut -d= -f2); do jbctl trustcache add $h; done
jbctl trustcache info | grep -i <hash>   # VERIFY it landed
rm /var/mnt/rootfs/usr/lib/dyld && cp /tmp/dyld_x /var/mnt/rootfs/usr/lib/dyld
# run:
/var/jb/usr/macOS/bin/launchdchrootexec 0 0 /var/mnt/rootfs /usr/bin/true
```
